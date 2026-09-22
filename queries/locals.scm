; Scopes, definitions and references.
;
; Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
; SPDX-License-Identifier: GPL-3.0-or-later

(function_definition) @local.scope
(anonymous_function) @local.scope

(function_definition
  parameters: (parameter_list (identifier) @local.definition))

(assignment
  left: (identifier) @local.definition)

(multi_assignment_target
  (identifier) @local.definition)

(for_statement
  variable: (identifier) @local.definition)

(declaration (identifier) @local.definition)

(identifier) @local.reference
