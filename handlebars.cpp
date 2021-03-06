
#include <string>
#include <talloc.h>

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/ext/std/ext_std_variable.h"
#include "hphp/runtime/base/variable-serializer.h"
#include "hphp/runtime/base/variable-unserializer.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/ext/ext_closure.h"
#include "hphp/util/string-vsnprintf.h"
#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/execution-context.h"

extern "C" {
#include "handlebars.h"
#include "handlebars_ast.h"
#include "handlebars_ast_list.h"
#include "handlebars_ast_printer.h"
#include "handlebars_compiler.h"
#include "handlebars_context.h"
#include "handlebars_memory.h"
#include "handlebars_opcodes.h"
#include "handlebars_opcode_printer.h"
#include "handlebars_token.h"
#include "handlebars_token_list.h"
#include "handlebars_token_printer.h"
#include "handlebars.tab.h"
#include "handlebars.lex.h"
int handlebars_yy_parse (struct handlebars_context * context);
}

#define HBS_STR(x) #x
#define HBS_HHVM_CONST_INT(y, x) HPHP::Native::registerConstant<KindOfInt64>(StaticString(y).get(), x);

namespace HPHP {

static const char * HANDLEBARS_VERSION = "0.3.2";
static std::string handlebars_last_error;
static HPHP::Class * s_HandlebarsExceptionClass = nullptr;
static HPHP::Class * s_HandlebarsCompileExceptionClass = nullptr;
static HPHP::Class * s_HandlebarsLexExceptionClass = nullptr;
static HPHP::Class * s_HandlebarsParseExceptionClass = nullptr;

static Array hhvm_handlebars_ast_node_to_array(struct handlebars_ast_node * node);

static ObjectData * AllocHandlebarsExceptionObject(Class * cls, const Variant& message) {
  ObjectData* inst = ObjectData::newInstance(cls);
  TypedValue ret;
  {
    /* Increment refcount across call to ctor, so the object doesn't */
    /* get destroyed when ctor's frame is torn down */
    CountableHelper cnt(inst);
    g_context->invokeFunc(&ret, cls->getCtor(), make_packed_array(message), inst);
  }
  tvRefcountedDecRef(&ret);
  return inst;
}

static char ** hhvm_handlebars_known_helpers_from_variant(struct handlebars_context * ctx, const Variant & knownHelpers) {
    if( !knownHelpers.isArray() ) {
        return NULL;
    }
    Array knownHelpersArray = knownHelpers.toArray();

    long count = knownHelpersArray.length();

    // Count builtins >.>
    for( const char ** ptr2 = handlebars_builtins; *ptr2; ++ptr2, ++count );

    // Allocate array
    char ** ptr;
    char ** known_helpers;
    ptr = known_helpers = handlebars_talloc_array(ctx, char *, count + 1);

    // Copy in known helpers
    for (ArrayIter iter(knownHelpersArray); iter; ++iter) {
          const Variant& value(iter.secondRefPlus());
          if( value.isString() ) {
              *ptr++ = (char *) handlebars_talloc_strdup(ctx, value.toCStrRef().toCppString().c_str());
          }
    }

    // Copy in builtins
    for( const char ** ptr2 = handlebars_builtins; *ptr2; ++ptr2 ) {
        *ptr++ = (char *) handlebars_talloc_strdup(ctx, *ptr2);
    }

    // Null terminate
    *ptr++ = NULL;

    return known_helpers;
}

static void hhvm_handlebars_operand_array_append(struct handlebars_operand * operand, Array & arr) {
    switch( operand->type ) {
        case handlebars_operand_type_null:
            arr.append(Variant());
            break;
        case handlebars_operand_type_boolean:
            arr.append((bool) operand->data.boolval);
            break;
        case handlebars_operand_type_long:
            arr.append((int64_t) operand->data.longval);
            break;
        case handlebars_operand_type_string:
            arr.append(String(operand->data.stringval));
            break;
        case handlebars_operand_type_array: {
            Array current;
            char ** tmp = operand->data.arrayval;
            for( ; *tmp; ++tmp ) {
                current.append(String(*tmp));
            }
            arr.append(current);
            break;
        }
    }
}

static Array hhvm_handlebars_opcode_to_array(struct handlebars_opcode * opcode) {
    Array current;
    Array args;
    const char * name = handlebars_opcode_readable_type(opcode->type);
    short num = handlebars_opcode_num_operands(opcode->type);

    current.add(String("opcode"), String(name));

    // coerce to array
    args.append(0);
    args.pop();

    if( num >= 1 ) {
        hhvm_handlebars_operand_array_append(&opcode->op1, args);
    }
    if( num >= 2 ) {
        hhvm_handlebars_operand_array_append(&opcode->op2, args);
    }
    if( num >= 3 ) {
        hhvm_handlebars_operand_array_append(&opcode->op3, args);
    }

    current.add(String("args"), args);

    return current;
}

static Array hhvm_handlebars_opcodes_to_array(struct handlebars_opcode ** opcodes, size_t count) {
    Array current;
    size_t i;
    struct handlebars_opcode ** pos = opcodes;

    // coerce to array
    current.append(0);
    current.pop();
    
    for( i = 0; i < count; i++, pos++ ) {
        current.append(hhvm_handlebars_opcode_to_array(*pos));
    }

    return current;
}

static Array hhvm_handlebars_compiler_to_array(struct handlebars_compiler * compiler) {

    Array current;
    Array children;
    size_t i;

    // coerce to array
    children.append(0);
    children.pop();

    // Opcodes
    current.add(String("opcodes"), hhvm_handlebars_opcodes_to_array(compiler->opcodes, compiler->opcodes_length));

    // Children
    for( i = 0; i < compiler->children_length; i++ ) {
        struct handlebars_compiler * child = *(compiler->children + i);
        children.append(hhvm_handlebars_compiler_to_array(child));
    }

    current.add(String("children"), children);

    // Add depths
    long depths = compiler->depths;
    int depthi = 0;
    Array zdepths;

    // coerce to array
    zdepths.append(0);
    zdepths.pop();

    while( depths > 0 ) {
        if( depths & 1 ) {
            zdepths.append(depthi);
        }
        depthi++;
        depths = depths >> 1;
    }

    current.add(String("depths"), zdepths);

    // Return
    return current;
}

static Array hhvm_handlebars_ast_list_to_array(struct handlebars_ast_list * list) {
    Array current;

    struct handlebars_ast_list_item * item;
    struct handlebars_ast_list_item * tmp;

    if( list == NULL ) {
        return current;
    }

    handlebars_ast_list_foreach(list, item, tmp) {
        current.append(hhvm_handlebars_ast_node_to_array(item->data));
    }

    return current;
}

static Array hhvm_handlebars_ast_node_to_array(struct handlebars_ast_node * node) {
    Array current;

    if( node == NULL ) {
        return current;
    }

    current.add(String("type"), HPHP::String::FromCStr(handlebars_ast_node_readable_type(node->type)));

    if( node->strip > 0 ) {
        Array strip;
        strip.add(String("left"), (bool) (node->strip & handlebars_ast_strip_flag_left));
        strip.add(String("right"), (bool) (node->strip & handlebars_ast_strip_flag_right));
        strip.add(String("openStandalone"), (bool) (node->strip & handlebars_ast_strip_flag_open_standalone));
        strip.add(String("closeStandalone"), (bool) (node->strip & handlebars_ast_strip_flag_close_standalone));
        strip.add(String("inlineStandalone"), (bool) (node->strip & handlebars_ast_strip_flag_inline_standalone));
        strip.add(String("leftStripped"), (bool) (node->strip & handlebars_ast_strip_flag_left_stripped));
        strip.add(String("rightStriped"), (bool) (node->strip & handlebars_ast_strip_flag_right_stripped));
        current.add(String("strip"), strip);
    }


    switch( node->type ) {
        case HANDLEBARS_AST_NODE_PROGRAM: {
            if( node->node.program.statements ) {
                current.add(String("statements"),
                        hhvm_handlebars_ast_list_to_array(node->node.program.statements));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_MUSTACHE: {
            if( node->node.mustache.sexpr ) {
                current.add(String("sexpr"),
                    hhvm_handlebars_ast_node_to_array(node->node.mustache.sexpr));
            }
            current.add(String("unescaped"), (bool) node->node.mustache.unescaped);
            break;
        }
        case HANDLEBARS_AST_NODE_SEXPR: {
            if( node->node.sexpr.hash ) {
                current.add(String("hash"),
                        hhvm_handlebars_ast_node_to_array(node->node.sexpr.hash));
            }
            if( node->node.sexpr.id ) {
                current.add(String("id"),
                        hhvm_handlebars_ast_node_to_array(node->node.sexpr.id));
            }
            if( node->node.sexpr.params ) {
                current.add(String("params"),
                        hhvm_handlebars_ast_list_to_array(node->node.sexpr.params));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_PARTIAL:
            if( node->node.partial.partial_name ) {
                current.add(String("partial_name"),
                        hhvm_handlebars_ast_node_to_array(node->node.partial.partial_name));
            }
            if( node->node.partial.context ) {
                current.add(String("context"),
                        hhvm_handlebars_ast_node_to_array(node->node.partial.context));
            }
            if( node->node.partial.hash ) {
                current.add(String("hash"),
                        hhvm_handlebars_ast_node_to_array(node->node.partial.hash));
            }
            break;
        case HANDLEBARS_AST_NODE_RAW_BLOCK: {
            if( node->node.raw_block.mustache ) {
                current.add(String("mustache"),
                        hhvm_handlebars_ast_node_to_array(node->node.raw_block.mustache));
            }
            if( node->node.raw_block.program ) {
                current.add(String("program"),
                        hhvm_handlebars_ast_node_to_array(node->node.raw_block.program));
            }
            if( node->node.raw_block.close ) {
                current.add(String("close"), String(node->node.raw_block.close));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_BLOCK: {
            if( node->node.block.mustache ) {
                current.add(String("mustache"),
                        hhvm_handlebars_ast_node_to_array(node->node.block.mustache));
            }
            if( node->node.block.program ) {
                current.add(String("program"),
                        hhvm_handlebars_ast_node_to_array(node->node.block.program));
            }
            if( node->node.block.inverse ) {
                current.add(String("inverse"),
                        hhvm_handlebars_ast_node_to_array(node->node.block.inverse));
            }
            if( node->node.block.close ) {
                current.add(String("close"),
                        hhvm_handlebars_ast_node_to_array(node->node.block.close));
            }
            current.add(String("inverted"), node->node.block.inverted);
            break;
        }
        case HANDLEBARS_AST_NODE_CONTENT: {
            if( node->node.content.string ) {
                current.add(String("string"),
                    String(node->node.content.string));
            }
            if( node->node.content.original ) {
                current.add(String("original"),
                    String(node->node.content.original));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_HASH: {
            if( node->node.hash.segments ) {
                current.add(String("segments"),
                        hhvm_handlebars_ast_list_to_array(node->node.hash.segments));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_HASH_SEGMENT: {
            if( node->node.hash_segment.key ) {
                current.add(String("key"),
                    String(node->node.hash_segment.key));
            }
            if( node->node.hash_segment.value ) {
                current.add(String("value"),
                        hhvm_handlebars_ast_node_to_array(node->node.hash_segment.value));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_ID: {
            if( node->node.id.parts ) {
                current.add(String("parts"),
                        hhvm_handlebars_ast_list_to_array(node->node.id.parts));
            }
            current.add(String("depth"), (int64_t) node->node.id.depth);
            current.add(String("is_simple"), (int64_t) node->node.id.is_simple);
            current.add(String("is_scoped"), (int64_t) node->node.id.is_scoped);
            if( node->node.id.id_name ) {
                current.add(String("id_name"),
                    String(node->node.id.id_name));
            }
            if( node->node.id.string ) {
                current.add(String("string"),
                    String(node->node.id.string));
            }
            if( node->node.id.original ) {
                current.add(String("original"),
                    String(node->node.id.original));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_PARTIAL_NAME: {
            if( node->node.partial_name.name ) {
                current.add(String("name"),
                        hhvm_handlebars_ast_node_to_array(node->node.partial_name.name));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_DATA: {
            if( node->node.data.id ) {
                current.add(String("id"),
                        hhvm_handlebars_ast_node_to_array(node->node.data.id));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_STRING: {
            if( node->node.string.string ) {
                current.add(String("string"),
                    String(node->node.string.string));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_NUMBER: {
            if( node->node.number.string ) {
                current.add(String("number"),
                    String(node->node.number.string));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_BOOLEAN: {
            if( node->node.boolean.string ) {
                current.add(String("boolean"),
                    String(node->node.boolean.string));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_COMMENT: {
            if( node->node.comment.comment ) {
                current.add(String("comment"),
                    String(node->node.comment.comment));
            }
            break;
        }
        case HANDLEBARS_AST_NODE_PATH_SEGMENT: {
            if( node->node.path_segment.separator ) {
                current.add(String("separator"),
                    String(node->node.path_segment.separator));
            }
            if( node->node.path_segment.part ) {
                current.add(String("part"),
                    String(node->node.path_segment.part));
            }
            break;
        }

        case HANDLEBARS_AST_NODE_INVERSE_AND_PROGRAM:
            break;
        case HANDLEBARS_AST_NODE_NIL:
            break;
    }

    return current;
}

/* {{{ proto string handlebars_error(void) */

static inline Variant hhvm_handlebars_get_last_error() {
    Variant ret;
    if( handlebars_last_error.length() ) {
        ret = HPHP::String::FromCStr(handlebars_last_error.c_str());
    }
    return ret;
}

Variant HHVM_FUNCTION(handlebars_error) {
    return hhvm_handlebars_get_last_error();
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, getLastError) {
    return hhvm_handlebars_get_last_error();
}

/* }}} handlebars_error */
/* {{{ proto mixed handlebars_lex(string tmpl) */

static inline Array hhvm_handlebars_lex(const String& tmpl) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());

    struct handlebars_token_list * list = handlebars_lex(ctx);

    Array ret;
    struct handlebars_token_list_item * el = NULL;
    struct handlebars_token_list_item * tmp = NULL;
    handlebars_token_list_foreach(list, el, tmp) {
        struct handlebars_token * token = el->data;
        Array child;
        child.add(String("name"), HPHP::String::FromCStr(handlebars_token_readable_type(token->token)));
        child.add(String("text"), HPHP::String::FromCStr(token->text));
        ret.append(child);
    }

    handlebars_context_dtor(ctx);

    return ret;
}

Array HHVM_FUNCTION(handlebars_lex, const String& tmpl) {
    return hhvm_handlebars_lex(tmpl);
}

Array HHVM_STATIC_METHOD(HandlebarsNative, lex, const String& tmpl) {
    return hhvm_handlebars_lex(tmpl);
}

/* }}} handlebars_lex */
/* {{{ proto mixed handlebars_lex_print(string tmpl) */

static inline String hhvm_handlebars_lex_print(const String& tmpl) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());

    struct handlebars_token_list * list = handlebars_lex(ctx);
    char * output = handlebars_token_list_print(list, 0);

    String ret = HPHP::String::FromCStr(output);

    handlebars_context_dtor(ctx);

    return ret;
}

String HHVM_FUNCTION(handlebars_lex_print, const String& tmpl) {
    return hhvm_handlebars_lex_print(tmpl);
}

String HHVM_STATIC_METHOD(HandlebarsNative, lexPrint, const String& tmpl) {
    return hhvm_handlebars_lex_print(tmpl);
}

/* }}} handlebars_lex_print */
/* {{{ proto mixed handlebars_parse(string tmpl) */

static inline Variant hhvm_handlebars_parse(const String& tmpl, bool exceptions) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());

    handlebars_yy_parse(ctx);

    Variant ret;
    if( ctx->error != NULL ) {
        ret = false;
        handlebars_last_error.assign(handlebars_context_get_errmsg(ctx));
	    if( exceptions ) {
            throw Object(AllocHandlebarsExceptionObject(s_HandlebarsParseExceptionClass, handlebars_last_error));
        }
    } else {
        ret = hhvm_handlebars_ast_node_to_array(ctx->program);
    }

    handlebars_context_dtor(ctx);

    return ret;
}

Variant HHVM_FUNCTION(handlebars_parse, const String& tmpl) {
    return hhvm_handlebars_parse(tmpl, false);
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, parse, const String& tmpl) {
    return hhvm_handlebars_parse(tmpl, true);
}

/* }}} handlebars_parse */
/* {{{ proto mixed handlebars_parse_print(string tmpl) */

static inline Variant hhvm_handlebars_parse_print(const String& tmpl, bool exceptions) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());
    handlebars_yy_parse(ctx);

    Variant ret;
    if( ctx->error != NULL ) {
        ret = false;
        handlebars_last_error.assign(handlebars_context_get_errmsg(ctx));
	    if( exceptions ) {
            throw Object(AllocHandlebarsExceptionObject(s_HandlebarsParseExceptionClass, handlebars_last_error));
        }
    } else {
        char * output = handlebars_ast_print(ctx->program, 0);
        ret = HPHP::String::FromCStr(output);
    }

    handlebars_context_dtor(ctx);

    return ret;
}

Variant HHVM_FUNCTION(handlebars_parse_print, const String& tmpl) {
    return hhvm_handlebars_parse_print(tmpl, false);
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, parsePrint, const String& tmpl) {
    return hhvm_handlebars_parse_print(tmpl, true);
}

/* }}} handlebars_parse_print */
/* {{{ proto mixed handlebars_compile(string tmpl[, long flags[, array knownHelpers]]) */

static inline Variant hhvm_handlebars_compile(const String& tmpl, int64_t flags, const Variant& knownHelpers, bool exceptions) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    struct handlebars_compiler * compiler = handlebars_compiler_ctor(ctx);
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());

    handlebars_compiler_set_flags(compiler, flags);

    char ** known_helpers_arr = hhvm_handlebars_known_helpers_from_variant(ctx, knownHelpers);
    if( known_helpers_arr ) {
        compiler->known_helpers = (const char **) known_helpers_arr;
    }

    // Parse
    handlebars_yy_parse(ctx);

    Variant ret;
    if( ctx->error != NULL ) {
        ret = false;
        handlebars_last_error.assign(ctx->error);
	    if( exceptions ) {
            // @todo this should probably be a ParseException
            throw Object(AllocHandlebarsExceptionObject(s_HandlebarsParseExceptionClass, handlebars_last_error));
        }
        goto error;
    }

    handlebars_compiler_compile(compiler, ctx->program);
    if( compiler->errnum ) {
        ret = false;
        if( compiler->error ) {
            handlebars_last_error.assign(ctx->error);
	        if( exceptions ) {
                throw Object(AllocHandlebarsExceptionObject(s_HandlebarsCompileExceptionClass, handlebars_last_error));
            }
        }
        goto error;
    }

    ret = hhvm_handlebars_compiler_to_array(compiler);

error:
    handlebars_context_dtor(ctx);
    return ret;
}

Variant HHVM_FUNCTION(handlebars_compile, const String& tmpl, int64_t flags, const Variant& knownHelpers) {
    return hhvm_handlebars_compile(tmpl, flags, knownHelpers, false);
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, compile, const String& tmpl, int64_t flags, const Variant& knownHelpers) {
    return hhvm_handlebars_compile(tmpl, flags, knownHelpers, true);
}

/* }}} handlebars_compile */
/* {{{ proto mixed handlebars_compile_print(string tmpl[, long flags[, array knownHelpers]]) */

static inline Variant hhvm_handlebars_compile_print(const String& tmpl, int64_t flags, const Variant& knownHelpers, bool exceptions) {
    struct handlebars_context * ctx = handlebars_context_ctor();
    struct handlebars_compiler * compiler = handlebars_compiler_ctor(ctx);
    struct handlebars_opcode_printer * printer = handlebars_opcode_printer_ctor(ctx);
    ctx->tmpl = handlebars_talloc_strdup(ctx, tmpl.toCppString().c_str());

    handlebars_compiler_set_flags(compiler, flags);

    char ** known_helpers_arr = hhvm_handlebars_known_helpers_from_variant(ctx, knownHelpers);
    if( known_helpers_arr ) {
        compiler->known_helpers = (const char **) known_helpers_arr;
    }

    // Parse
    handlebars_yy_parse(ctx);

    Variant ret;
    if( ctx->error != NULL ) {
        ret = false;
        handlebars_last_error.assign(ctx->error);
	    if( exceptions ) {
            // @todo this should probably be a ParseException
            throw Object(AllocHandlebarsExceptionObject(s_HandlebarsParseExceptionClass, handlebars_last_error));
        }
        goto error;
    }

    handlebars_compiler_compile(compiler, ctx->program);
    if( compiler->errnum ) {
        ret = false;
        if( compiler->error ) {
            handlebars_last_error.assign(ctx->error);
	        if( exceptions ) {
                throw Object(AllocHandlebarsExceptionObject(s_HandlebarsCompileExceptionClass, handlebars_last_error));
            }
        }
        goto error;
    }

    handlebars_opcode_printer_print(printer, compiler);
    ret = HPHP::String::FromCStr(printer->output);

error:
    handlebars_context_dtor(ctx);
    return ret;
}

Variant HHVM_FUNCTION(handlebars_compile_print, const String& tmpl, int64_t flags, const Variant& knownHelpers) {
    return hhvm_handlebars_compile_print(tmpl, flags, knownHelpers, false);
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, compilePrint, const String& tmpl, int64_t flags, const Variant& knownHelpers) {
    return hhvm_handlebars_compile_print(tmpl, flags, knownHelpers, true);
}

/* }}} handlebars_compile_print */
/* {{{ proto mixed handlebars_version(void) */

String HHVM_FUNCTION(handlebars_version) {
    return String(handlebars_version_string());
}

Variant HHVM_STATIC_METHOD(HandlebarsNative, version) {
    return String(handlebars_version_string());
}

/* }}} handlebars_version */

static class HandlebarsExtension : public Extension {
    public:
    HandlebarsExtension() : Extension("handlebars", HANDLEBARS_VERSION) {}

    virtual void moduleInit() {
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_NONE", handlebars_compiler_flag_none);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_USE_DEPTHS", handlebars_compiler_flag_use_depths);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_STRING_PARAMS", handlebars_compiler_flag_string_params);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_TRACK_IDS", handlebars_compiler_flag_track_ids);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_KNOWN_HELPERS_ONLY", handlebars_compiler_flag_known_helpers_only);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_COMPAT", handlebars_compiler_flag_compat);
    	HBS_HHVM_CONST_INT("Handlebars\\COMPILER_FLAG_ALL", handlebars_compiler_flag_all);

        HHVM_FE(handlebars_error);
        HHVM_FE(handlebars_lex);
        HHVM_FE(handlebars_lex_print);
        HHVM_FE(handlebars_parse);
        HHVM_FE(handlebars_parse_print);
        HHVM_FE(handlebars_compile);
        HHVM_FE(handlebars_compile_print);
        HHVM_FE(handlebars_version);

        HHVM_STATIC_ME(HandlebarsNative, getLastError);
        HHVM_STATIC_ME(HandlebarsNative, lex);
        HHVM_STATIC_ME(HandlebarsNative, lexPrint);
        HHVM_STATIC_ME(HandlebarsNative, parse);
        HHVM_STATIC_ME(HandlebarsNative, parsePrint);
        HHVM_STATIC_ME(HandlebarsNative, compile);
        HHVM_STATIC_ME(HandlebarsNative, compilePrint);
        HHVM_STATIC_ME(HandlebarsNative, version);

        loadSystemlib();

    	s_HandlebarsExceptionClass = Unit::lookupClass(StaticString("Handlebars\\Exception").get());
    	s_HandlebarsCompileExceptionClass = Unit::lookupClass(StaticString("Handlebars\\CompileException").get());
    	s_HandlebarsLexExceptionClass = Unit::lookupClass(StaticString("Handlebars\\LexException").get());
    	s_HandlebarsParseExceptionClass = Unit::lookupClass(StaticString("Handlebars\\ParseException").get());
    }
} s_handlebars_extension;

HHVM_GET_MODULE(handlebars)

}
