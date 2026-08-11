; Primitives
(string) @string
(number) @number

; Catch your intrinsic/macro calls (anything starting with @)
((id) @function.macro
 (#match? @function.macro "^@"))

; Catch pseudo-keywords that currently parse as IDs
((id) @keyword
 (#match? @keyword "^(fn|return|struct)$"))

; Variables (fallback for all other IDs)
(id) @variable

; Highlight method calls via dot or colon (e.g., a:__add -> __add is a function)
(op 
  operator: [":" "."] 
  right: (id) @function.method)

; Highlight implicit function calls with parenthesized blocks
(op 
  left: (id) @function.call 
  right: (pblock))

; Operators
[
  "~" "=" "+" "-" "*" "/" "%" "|>"
  "<" ">" "<=" ">=" "==" "!="
  "and" "or" "not" "band" "bor" "bnot" "bxor" "shr" "shl"
] @operator

; Delimiters & Brackets
[ "(" ")" "[" "]" "{" "}" ] @punctuation.bracket
[ ";" "," "." ":" ] @punctuation.delimiter
