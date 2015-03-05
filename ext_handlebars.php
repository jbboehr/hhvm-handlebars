<?hh
<<__Native>>
function handlebars_error(): ?string;

<<__Native>>
function handlebars_lex(string $tmpl): array;

<<__Native>>
function handlebars_lex_print(string $tmpl): string;

<<__Native>>
function handlebars_parse(string $tmpl): mixed;

<<__Native>>
function handlebars_parse_print(string $tmpl): mixed;

<<__Native>>
function handlebars_compile(string $tmpl, int $flags = 0, ?array $knownHelpers = NULL): mixed;

<<__Native>>
function handlebars_compile_print(string $tmpl, int $flags = 0, ?array $knownHelpers = NULL): mixed;
