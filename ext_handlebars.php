<?hh

const HANDLEBARS_COMPILER_FLAG_NONE = 0;
const HANDLEBARS_COMPILER_FLAG_USE_DEPTHS = (1 << 0);
const HANDLEBARS_COMPILER_FLAG_STRING_PARAMS = (1 << 1);
const HANDLEBARS_COMPILER_FLAG_TRACK_IDS = (1 << 2);
const HANDLEBARS_COMPILER_FLAG_KNOWN_HELPERS_ONLY = (1 << 4);
const HANDLEBARS_COMPILER_FLAG_COMPAT = (1 << 0);
const HANDLEBARS_COMPILER_FLAG_ALL = (1 << 4) - 1;

/**
 * Get the last error that occurred.
 * 
 * @return string|null
 */
<<__Native>>
function handlebars_error(): ?string;

/**
 * Tokenize a template and return an array of tokens
 *
 * @param string $tmpl
 * @return array
 */
<<__Native>>
function handlebars_lex(string $tmpl): array;

/**
 * Tokenize a template and return a readable string representation of the tokens
 *
 * @param string $tmpl
 * @return string
 */
<<__Native>>
function handlebars_lex_print(string $tmpl): string;

/**
 * Parse a template and return the AST
 *
 * @param string $tmpl
 * @return array|false
 */
<<__Native>>
function handlebars_parse(string $tmpl): mixed;

/**
 * Parse a template and return a readable string representation of the AST
 * 
 * @param string $tmpl
 * @return string|false
 */
<<__Native>>
function handlebars_parse_print(string $tmpl): mixed;

/**
 * Compile a template and return the opcodes 
 * 
 * @param string $tmpl
 * @param integer $flags
 * @param array $knownHelpers
 * @return array|false
 */
<<__Native>>
function handlebars_compile(string $tmpl, int $flags = 0, ?array $knownHelpers = NULL): mixed;

/**
 * Compile a template and return a readable string representation of the opcodes 
 * 
 * @param string $tmpl
 * @param integer $flags
 * @param array $knownHelpers
 * @return string|false
 */
<<__Native>>
function handlebars_compile_print(string $tmpl, int $flags = 0, ?array $knownHelpers = NULL): mixed;

/**
 * Get the libhandlebars version
 * @return string
 */
<<__Native>>
function handlebars_version(): string;

