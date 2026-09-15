; Primitives
(string) @string
(comment) @comment
(number) @number

; 1. VARIABLES (Fallback for all other IDs)
; This MUST be placed before the specific ID matches below. 
; In Neovim's Tree-sitter implementation, queries evaluated later in the file
; overwrite earlier ones for the same node. If this was at the bottom, 
; it would erase your keywords and decorators.
(id) @variable

((id) @keyword
 (#any-of? @keyword "return" ))

((id) @function.macro
 (#lua-match? @function.macro "^@"))

; 4. DECLARATIONS
; Leverage the specific fields you defined in your grammar.js `decl` rule
(decl type: (id) @type)
(decl name: (id) @variable.builtin) 

; 5. FUNCTION CALLS
; Highlight method calls via dot or colon (e.g., a:__add -> __add is a function)
(op 
  operator: [":" "."] 
  right: (id) @function.method)

; Highlight implicit function calls with parenthesized blocks
(op 
  left: (id) @function.call 
  right: (pblock))

; 6. OPERATORS
[
  "as" "'"
  "~" "=" "+" "-" "*" "/" "%" "|>"
  "<" ">" "<=" ">=" "==" "!="
  "and" "or" "band" "bor" "bxor" "shr" "shl"
] @operator

; 7. DELIMITERS & BRACKETS
[ "(" ")" "[" "]" "{" "}" ] @punctuation.bracket
[ ";" "," "." ":" ] @punctuation.delimiter
