; Definitions and references for code navigation.
;
; Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
; SPDX-License-Identifier: GPL-3.0-or-later

(function_definition
  name: (identifier) @name) @definition.function

(function_definition
  name: (qualified_name) @name) @definition.function

(classdef_definition
  name: (identifier) @name) @definition.class

(methods_block
  (function_definition
    name: (identifier) @name) @definition.method)

(properties_block
  (property
    name: (identifier) @name) @definition.property)

(index_expression
  value: (identifier) @name) @reference.call

(function_handle
  (identifier) @name) @reference.call

(superclass_list
  (identifier) @name) @reference.class
