; Highlighting for Octave.
;
; Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
; SPDX-License-Identifier: GPL-3.0-or-later

; Literals

(comment) @comment @spell
(number) @number
(string) @string

; Definitions

(function_definition
  name: (identifier) @function)

(function_definition
  name: (qualified_name) @function)

(function_definition
  parameters: (parameter_list (end_index) @keyword

(identifier) @variable.parameter))

(ignored_output) @variable.parameter

(classdef_definition
  name: (identifier) @type)

(superclass_list (identifier) @type)
(superclass_list (qualified_name) @type)

(metaclass) @type

(property
  name: (identifier) @property)

(attribute
  name: (identifier) @attribute)

(enumerator
  name: (identifier) @constant)

; Uses

(index_expression
  value: (identifier) @function.call)

(cell_index_expression
  value: (end_index) @keyword

(identifier) @variable)

(field_expression
  field: (identifier) @property)

(function_handle
  (identifier) @function)

(function_handle
  (qualified_name) @function)

(end_index) @keyword

(identifier) @variable

; Operators and punctuation

[
  "+" "-" "*" "/" "\\" ".*" "./" ".\\" "^" ".^"
  "==" "~=" "!=" "<" "<=" ">" ">="
  "&&" "||" "&" "|" "!" "~"
  "=" "+=" "-=" "*=" "/=" "^=" "&=" "|=" ".*=" "./=" ".^="
  "++" "--" ".'" ":" "@" "<" "?"
] @operator

[
  "(" ")" "[" "]" "{" "}"
] @punctuation.bracket

[
  "," ";" "."
] @punctuation.delimiter

; Keywords

[
  "if" "elseif" "else" "endif"
  "switch" "case" "otherwise" "endswitch"
] @keyword.conditional

[
  "for" "parfor" "endfor" "endparfor"
  "while" "endwhile"
  "do" "until"
] @keyword.repeat

[
  "try" "catch" "end_try_catch"
  "unwind_protect" "unwind_protect_cleanup" "end_unwind_protect"
] @keyword.exception

[
  "function" "endfunction"
] @keyword.function

; `break`, `continue` and `return` are whole statements whose only token the
; grammar folds in, so the node is the keyword.
(return_statement) @keyword.return
(break_statement) @keyword
(continue_statement) @keyword

[
  "classdef" "endclassdef"
  "properties" "endproperties"
  "methods" "endmethods"
  "events" "endevents"
  "enumeration" "endenumeration"
] @keyword.type

[
  "global" "persistent"
] @keyword.modifier

"end" @keyword
