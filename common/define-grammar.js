/**
 * Octave grammar for tree-sitter.
 *
 * Octave is the language this describes and MATLAB is admitted alongside it:
 * wherever a construct has two spellings, both are accepted and the node type
 * is the same, so a consumer never has to ask which dialect it was reading.
 *
 * Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Binding power, loosest first.  Octave binds ^ tighter than unary minus, so
   -2^2 is -4, and a postfix transpose tighter still. */
const PREC = {
  or: 1,
  and: 2,
  bitor: 3,
  bitand: 4,
  compare: 5,
  range: 6,
  add: 7,
  multiply: 8,
  unary: 9,
  power: 10,
  postfix: 11,
  call: 12,
};

/* One definition, two parsers.  `lenient` is the grammar an editor wants: it
   accepts Octave and the MATLAB spelling of everything that has two.
   `strict` accepts only what Octave owns, so a file that passes is one MATLAB
   cannot read.  See README.md. */

/* Octave's reserved words, from `iskeyword ()`.  The scanner refuses to lex
   any of them as a name, which is what makes `end = 5` the syntax error it is
   in Octave.  After a dot they are ordinary field names, though, as
   `finfo.function` in core's `vectorize.m` is. */
const KEYWORDS = [
  'break', 'case', 'catch', 'classdef', 'continue', 'do', 'else', 'elseif',
  'end', 'end_try_catch', 'end_unwind_protect', 'endarguments', 'endclassdef',
  'endenumeration', 'endevents', 'endfor', 'endfunction', 'endif',
  'endmethods', 'endparfor', 'endproperties', 'endspmd', 'endswitch',
  'endwhile', 'for', 'function', 'global', 'if', 'otherwise', 'parfor',
  'persistent', 'return', 'spmd', 'switch', 'try', 'until', 'unwind_protect',
  'unwind_protect_cleanup', 'while',
];

module.exports = function defineGrammar (dialect) {

const strict = (dialect === 'strict');

return grammar ({
  name: strict ? 'octave_strict' : 'octave',

  word: $ => $.identifier,

  /* A newline reaches this only where the scanner declined to make it a
     statement terminator, which is inside parentheses. */
  extras: $ => [/[ \t\r\n]/, $.comment, $.line_continuation],

  /* The scanner decides whether a break ends a statement, is whitespace
     inside parentheses, or separates rows inside brackets. */
  /* The delimiters are the scanner's, so that it can keep the bracket stack,
     but they stay ordinary literals in the tree and so remain queryable. */
  externals: $ => [
    $._newline,
    $._transpose,
    $.command_argument,
    $.end_index,
    /* Only the lenient grammar has an argument validation block, so only it
       declares the keyword.  The scanner's token order follows, which is why
       its enum is switched on the same flag. */
    ...(strict ? [] : [$._arguments_keyword]),
    '(', ')', '[', ']', '{', '}',
    /* Lexed by the scanner, so a word Octave reserves never becomes a name. */
    $.identifier,
  ],

  /* `[a b]` is a matrix until an `=` proves it a multi-assignment target, and
     the parser cannot know which until it has read past the bracket. */
  conflicts: $ => [
    [$._assignable, $._non_range_expression],
    /* `a:b:c` is one range with a step and `a:b` is one without: which it is
       shows only after the second colon. */
    [$._expression, $.range_expression],
    /* `[val, key] = s` is a loop head after `for` and an assignment
       anywhere else, and the two read alike until the keyword is seen. */
    [$.assignment, $._loop_head],
    /* Whether a statement after a separator continues the run or is the last
       one shows only past its end.  Strict does not need it: requiring the
       function terminator removes the state where it arose. */
    ...(strict ? [] : [[$._block]]),
    /* A definition is both a statement and a top-level item, and which it is
       shows only from where it sits. */
    [$.if_statement, $._non_range_expression],
    [$.while_statement, $._non_range_expression],
    [$.elseif_clause, $._non_range_expression],
    ...(strict ? [] : [
      /* A declaration's parts are all optional, so which one a token begins
         shows only past it. */
      [$.argument_declaration],
      /* `arguments` opens a block or names a variable; the scanner decides
         from what follows, and the parser must admit both readings. */
      [$.arguments_block, $._non_range_expression],
    ]),
    [$._top_level, $._statement],
  ],

  rules: {
    /* Statements are separated, never merely adjacent.  Allowing two to sit
       side by side makes `1:2:10` read as a range, a stray colon and a
       number, and it is what let a transpose be mistaken for a string. */
    source_file: $ => seq (
      repeat ($._terminator),
      repeat (seq ($._top_level, repeat1 ($._terminator))),
      optional ($._top_level),
    ),

    _top_level: $ => choice (
      $.function_definition, $.classdef_definition, $._statement,
    ),

    /* ---------------------------------------------------------------- lexis */

    /* One token, both markers, line and block alike.  A block's markers must
       stand alone on their line but for whitespace, which is what the manual
       requires and what this enforces; a consumer that needs to tell a block
       from a line reads the token's second character. */
    comment: _ => token (choice (
      ...(strict
          ? [seq ('#', /[^\r\n]*/), seq ('%!', /[^\r\n]*/)]
          : [seq (choice ('%', '#'), /[^\r\n]*/)]),
      seq (strict ? '#{' : choice ('%{', '#{'), /[ \t]*\r?\n/,
           repeat (choice (/[^%#\r\n][^\r\n]*\r?\n/,
                           /[ \t]*\r?\n/,
                           /[%#][^}\r\n][^\r\n]*\r?\n/,
                           /[%#]\r?\n/)),
           /[ \t]*/, strict ? '#}' : choice ('%}', '#}')),
    )),

    /* `...` takes a comment after it; a lone backslash continues a line only
       at the end of one, being left division anywhere else. */
    line_continuation: _ => token (choice (
      seq ('...', /[^\r\n]*/, /\r?\n/),
      seq ('\\', /[ \t]*/, /\r?\n/),
    )),

    _terminator: $ => choice (';', ',', $._newline),

    /* A digit separator is an underscore: `20_000` is twenty thousand. */
    number: _ => token (choice (
      /(\d[\d_]*\.?[\d_]*|\.\d[\d_]*)([eEdD][+-]?\d+)?[ij]?/,
      /0[xX][0-9a-fA-F_]+/,
      /0[bB][01_]+/,
    )),

    /* A single-quoted string doubles the quote to escape it; a double-quoted
       string also takes backslash escapes, which MATLAB has no equivalent of
       and which the grammar therefore admits only there. */
    string: $ => choice ($.single_quoted_string, $.double_quoted_string),

    single_quoted_string: _ => token (seq (
      "'", repeat (choice (/[^'\r\n]/, "''")), "'",
    )),

    double_quoted_string: _ => token (seq (
      '"', repeat (choice (/[^"\\\r\n]/, seq ('\\', /(.|\r?\n)/), '""')), '"',
    )),

    /* ----------------------------------------------------------- statements */

    _statement: $ => choice (
      $.function_definition,
      /* Octave has no argument validation block and errors on one, so only
         the lenient grammar admits it. */
      ...(strict ? [] : [$.arguments_block]),
      $.if_statement,
      $.for_statement,
      $.while_statement,
      $.do_statement,
      $.switch_statement,
      $.try_statement,
      $.unwind_protect_statement,
      $.break_statement,
      $.continue_statement,
      $.return_statement,
      $.declaration,
      $.assignment,
      $.command_statement,
      $.expression_statement,
    ),

    expression_statement: $ => $._expression,

    /* `hold on` is `hold ('on')`; the scanner decides where one begins. */
    command_statement: $ => seq (
      field ('name', $.identifier), repeat1 ($.command_argument),
    ),

    assignment: $ => choice (
      prec.right (seq (field ('left', $._assignable), '=',
                       field ('right', choice ($._expression, $.assignment)))),
      seq (field ('left', $.multi_assignment_target), '=',
           field ('right', $._expression)),
      seq (field ('left', $._assignable),
           field ('operator',
                  choice ('+=', '-=', '*=', '/=', '^=', '&=', '|=',
                          '.*=', './=', '.^=')),
           field ('right', $._expression)),
    ),

    multi_assignment_target: $ => seq (
      '[', optional (seq ($._target_element, repeat (seq (optional (','),
                                                          $._target_element)))),
      ']',
    ),

    _target_element: $ => choice ($._assignable, $.ignored_output),

    ignored_output: _ => '~',

    _assignable: $ => choice ($.identifier, $.index_expression,
                             $.cell_index_expression, $.field_expression),

    declaration: $ => prec.left (seq (
      choice ('global', 'persistent'),
      repeat1 (choice ($.identifier,
                       seq ($.identifier, '=', $._expression))),
    )),

    break_statement: _ => 'break',
    continue_statement: _ => 'continue',
    return_statement: _ => 'return',

    /* ------------------------------------------------------------- blocks */

    /* A body may share the header's line only where the condition is
       parenthesised, as `if (c) x = 1; endif` is: the closing bracket is
       then what ends the condition, so the parser cannot go on extending it
       across the body.  Without that restriction it does exactly that. */
    if_statement: $ => seq (
      'if',
      choice (
        seq (field ('condition', $._expression), $._block),
        seq (field ('condition', $.parenthesized_expression), $._inline_block),
      ),
      repeat ($.elseif_clause),
      optional ($.else_clause),
      $._end_if,
    ),

    /* `elseif` continues the chain and ends the whole statement when it is
       taken.  `else if` is not another spelling of it: it is an `else` whose
       branch opens with a nested `if`, which takes its own terminator and
       after which the branch goes on.  They are different syntax, so the
       grammar keeps them apart and `else if` falls out of `else_clause`. */
    elseif_clause: $ => seq (
      'elseif',
      choice (
        seq (field ('condition', $._expression), $._block),
        seq (field ('condition', $.parenthesized_expression), $._inline_block),
      ),
    ),

    /* A keyword-only header has no condition to go on extending, so a body
       on its line raises no ambiguity at all. */
    else_clause: $ => seq ('else', choice ($._block, $._inline_block)),

    for_statement: $ => seq (
      choice ('for', 'parfor'),
      choice (seq ($._loop_head), seq ('(', $._loop_head, ')')),
      $._block,
      $._end_for,
    ),

    _loop_head: $ => seq (
      field ('variable', choice ($._assignable, $.multi_assignment_target)),
      '=', field ('range', $._expression),
    ),

    while_statement: $ => seq (
      'while',
      choice (
        seq (field ('condition', $._expression), $._block),
        seq (field ('condition', $.parenthesized_expression), $._inline_block),
      ),
      $._end_while,
    ),

    do_statement: $ => seq (
      'do', $._block, 'until', field ('condition', $._expression),
    ),

    switch_statement: $ => seq (
      'switch', field ('value', $._expression),
      repeat ($._terminator),
      repeat ($.case_clause),
      optional ($.otherwise_clause),
      $._end_switch,
    ),

    case_clause: $ => seq ('case', field ('value', $._expression), $._block),

    otherwise_clause: $ => seq ('otherwise',
                               choice ($._block, $._inline_block)),

    try_statement: $ => seq (
      'try', $._block,
      optional ($.catch_clause),
      $._end_try,
    ),

    catch_clause: $ => seq (
      'catch', optional (field ('error', $.identifier)), $._block,
    ),

    unwind_protect_statement: $ => seq (
      'unwind_protect', $._block,
      'unwind_protect_cleanup', $._block,
      $._end_unwind_protect,
    ),

    /* Every block ender is accepted in its Octave spelling and, unless the
       grammar is strict, as a bare `end`.  The strict forms are wrapped in a
       `seq`: a hidden rule whose whole body is one terminal is folded away
       by tree-sitter 0.22.6 and stops being required, so the terminator
       silently becomes optional.  Measured, not guessed. */
    _end_if: _ => strict ? seq ('endif') : choice ('endif', 'end'),
    _end_for: _ => strict ? choice ('endfor', 'endparfor')
                         : choice ('endfor', 'endparfor', 'end'),
    _end_while: _ => strict ? seq ('endwhile') : choice ('endwhile', 'end'),
    _end_switch: _ => strict ? seq ('endswitch') : choice ('endswitch', 'end'),
    _end_try: _ => strict ? seq ('end_try_catch') : choice ('end_try_catch', 'end'),
    _end_unwind_protect: _ => strict ? seq ('end_unwind_protect') : choice ('end_unwind_protect', 'end'),
    _end_function: _ => strict ? seq ('endfunction') : choice ('endfunction', 'end'),

    /* A block opens on a terminator, so `if a (b)` is a call in the condition
       and never a condition followed by a statement. */
    /* A block opens on a terminator.  A body sharing the header's line, as
       `if (c) x = 1; endif` does, is not parsed.  Allowing it needs four more
       conflict declarations, and with them the parser resolves the ambiguity
       the wrong way: it keeps extending the condition across the body rather
       than ending it at the bracket, so the construct still fails and every
       ordinary `if (cond)` gains a way to be read wrongly.  Two files in
       1952 are not worth that. */
    /* A comment line leaves a terminator of its own, so a block may open on
       any number of them. */
    /* A body opening straight onto its statement, which only a parenthesised
       condition admits. */
    _inline_block: $ => prec.right (
      repeat1 (seq ($._statement, repeat1 ($._terminator))),
    ),

    _block: $ => prec.right (seq (
      repeat1 ($._terminator),
      repeat (seq ($._statement, repeat1 ($._terminator))),
      optional ($._statement),
    )),

    /* ----------------------------------------------------------- classdef */

    classdef_definition: $ => seq (
      'classdef',
      optional (field ('attributes', $.attribute_list)),
      field ('name', $.identifier),
      optional (seq ('<', field ('superclasses', $.superclass_list))),
      repeat (choice ($._terminator, $.properties_block, $.methods_block,
                      $.events_block, $.enumeration_block)),
      $._end_classdef,
    ),

    /* `properties (Access = private)` and `properties(Dependent = true)`
       alike: the attribute list is optional and its spacing is free. */
    attribute_list: $ => seq (
      '(',
      optional (seq ($.attribute, repeat (seq (',', $.attribute)))),
      ')',
    ),

    attribute: $ => seq (
      field ('name', $.identifier),
      optional (seq ('=', field ('value', $._attribute_value))),
    ),

    _attribute_value: $ => choice (
      $.identifier, $.string, $.metaclass, $.attribute_value_list,
    ),

    attribute_value_list: $ => seq (
      '{',
      optional (seq ($._attribute_item, repeat (seq (',', $._attribute_item)))),
      '}',
    ),

    _attribute_item: $ => choice ($.metaclass, $.identifier, $.string),

    metaclass: $ => seq ('?', $._function_name),

    superclass_list: $ => seq (
      $._function_name, repeat (seq ('&', $._function_name)),
    ),

    properties_block: $ => seq (
      'properties',
      optional (field ('attributes', $.attribute_list)),
      repeat (choice ($._terminator, $.property)),
      $._end_properties,
    ),

    property: $ => seq (
      field ('name', $.identifier),
      optional (seq ('=', field ('default', $._expression))),
    ),

    methods_block: $ => seq (
      'methods',
      optional (field ('attributes', $.attribute_list)),
      repeat (choice ($._terminator, $.function_definition)),
      $._end_methods,
    ),

    events_block: $ => seq (
      'events',
      optional (field ('attributes', $.attribute_list)),
      repeat (choice ($._terminator, $.identifier)),
      $._end_events,
    ),

    enumeration_block: $ => seq (
      'enumeration',
      optional (field ('attributes', $.attribute_list)),
      repeat (choice ($._terminator, $.enumerator)),
      $._end_enumeration,
    ),

    enumerator: $ => seq (
      field ('name', $.identifier),
      optional (seq ('(', optional ($._argument_list), ')')),
    ),

    _end_classdef: _ => strict ? seq ('endclassdef') : choice ('endclassdef', 'end'),
    _end_properties: _ => strict ? seq ('endproperties') : choice ('endproperties', 'end'),
    _end_methods: _ => strict ? seq ('endmethods') : choice ('endmethods', 'end'),
    _end_events: _ => strict ? seq ('endevents') : choice ('endevents', 'end'),
    _end_enumeration: _ => strict ? seq ('endenumeration') : choice ('endenumeration', 'end'),

    /* ------------------------------------------------ argument validation */

    ...(strict ? {} : {

    arguments_block: $ => seq (
      $._arguments_keyword,
      optional (field ('attributes', $.attribute_list)),
      repeat (choice ($._terminator, $.argument_declaration)),
      'end',
    ),

    argument_declaration: $ => seq (
      field ('name', choice ($.identifier, $.argument_field)),
      optional (field ('size', $.argument_size)),
      optional (field ('class', $._function_name)),
      optional (field ('validators', $.validator_list)),
      optional (seq ('=', field ('default', $._expression))),
    ),

    /* `opts.Name` declares a name-value argument. */
    argument_field: $ => seq ($.identifier, '.', $.identifier),

    argument_size: $ => seq (
      '(', $._size_item, repeat (seq (',', $._size_item)), ')',
    ),

    _size_item: $ => choice ($.number, $.colon),

    validator_list: $ => seq (
      '{',
      optional (seq ($._validator, repeat (seq (',', $._validator)))),
      '}',
    ),

    _validator: $ => choice ($.index_expression, $._function_name),

    }),

    /* ---------------------------------------------------------- functions */

    /* Right-associative: with no `endfunction`, the body runs on to the next
       `function` or to the end of the file, which is what Octave does. */
    function_definition: $ => prec.right (seq (
      'function',
      optional (seq (field ('output', $._function_output), '=')),
      field ('name', $._function_name),
      optional (field ('parameters', $.parameter_list)),
      $._block,
      /* Strict requires the terminator.  Octave accepts a function with none,
         but strict is not bound by what Octave accepts: where a construct has
         a choice of spelling it requires Octave's own, and a function body
         may be closed by `endfunction`, by `end`, or by nothing at all. */
      ...(strict ? [$._end_function] : [optional ($._end_function)]),
    )),

    /* A function name is identifiers and dots, never a general expression:
       `ns.f` and `Class.method` are names, not field accesses. */
    /* A method may be named for a reserved word: `end` is overloadable and
       `datatypes` overloads it on every array class. */
    _function_name: $ => choice (
      $.identifier,
      $.qualified_name,
      alias (choice (...KEYWORDS), $.identifier),
    ),

    qualified_name: $ => prec (PREC.call, seq (
      $.identifier, repeat1 (seq ('.', $.identifier)),
    )),

    _function_output: $ => choice ($.identifier, $.multi_assignment_target),

    parameter_list: $ => seq (
      '(', optional (seq ($._parameter, repeat (seq (',', $._parameter)))), ')',
    ),

    /* `varargin` and `varargout` are conventions, not keywords: they are
       ordinary names, assigned to and indexed like any other. */
    _parameter: $ => choice (
      $.identifier,
      $.ignored_output,
      seq (field ('name', $.identifier), '=', field ('default', $._expression)),
    ),

    /* -------------------------------------------------------- expressions */

    _expression: $ => choice ($._non_range_expression, $.range_expression),

    /* A range's own parts are never ranges, which is what keeps `a:b:c` one
       range with a step rather than two ranges nested. */
    _non_range_expression: $ => choice (
      $.identifier,
      $.number,
      $.string,
      $.matrix,
      $.cell,
      $.anonymous_function,
      $.function_handle,
      $.parenthesized_expression,
      $.unary_expression,
      $.binary_expression,
      $.postfix_expression,
      $.index_expression,
      $.cell_index_expression,
      $.field_expression,
      $.superclass_reference,
      $.metaclass,
      $.colon,
      $.end_index,
    /* Only the lenient grammar has an argument validation block, so only it
       declares the keyword.  The scanner's token order follows, which is why
       its enum is switched on the same flag. */
    ...(strict ? [] : [$._arguments_keyword]),
    ),

    parenthesized_expression: $ => seq (
      '(', choice ($._expression, $.assignment), ')',
    ),

    colon: _ => ':',

    unary_expression: $ => prec (PREC.unary, seq (
      field ('operator', choice (...(strict ? ['-', '+', '!']
                                          : ['-', '+', '!', '~']),
                                 '++', '--')),
      field ('argument', $._expression),
    )),

    /* The scanner decides whether a quote is a transpose or opens a string,
       by the whitespace before it and the bracket it sits in. */
    postfix_expression: $ => prec (PREC.postfix, seq (
      field ('argument', $._expression),
      field ('operator', choice ($._transpose, ".'", '++', '--')),
    )),

    binary_expression: $ => choice (
      ...[
        ['||', PREC.or], ['|', PREC.bitor],
        ['&&', PREC.and], ['&', PREC.bitand],
        ['==', PREC.compare], ['!=', PREC.compare],
        ...(strict ? [] : [['~=', PREC.compare]]),
        ['<', PREC.compare], ['<=', PREC.compare],
        ['>', PREC.compare], ['>=', PREC.compare],
        ['+', PREC.add], ['-', PREC.add],
        ['*', PREC.multiply], ['/', PREC.multiply], ['\\', PREC.multiply],
        ['.*', PREC.multiply], ['./', PREC.multiply], ['.\\', PREC.multiply],
      ].map (([operator, precedence]) => prec.left (precedence, seq (
        field ('left', $._expression),
        field ('operator', operator),
        field ('right', $._expression),
      ))),
      ...[['^', PREC.power], ['.^', PREC.power]].map (
        ([operator, precedence]) => prec.right (precedence, seq (
          field ('left', $._expression),
          field ('operator', operator),
          field ('right', $._expression),
        ))),
    ),

    /* One range, flat: `a:b:c` is a start, a step and a stop, never a range
       of a range.  The operands are non-ranges, which is what makes the flat
       reading the only valid one; the conflict below is the lookahead LR
       needs to see which of the two forms it has. */
    range_expression: $ => prec.left (PREC.range, choice (
      prec.dynamic (2, seq (
        field ('start', $._non_range_expression), ':',
        field ('step', $._non_range_expression), ':',
        field ('stop', $._non_range_expression),
      )),
      prec.dynamic (1, seq (
        field ('start', $._non_range_expression), ':',
        field ('stop', $._non_range_expression),
      )),
    )),

    index_expression: $ => prec.dynamic (1, prec (PREC.call, seq (
      field ('value', $._expression), '(', optional ($._argument_list),
      ')',
    ))),

    cell_index_expression: $ => prec (PREC.call, seq (
      field ('value', $._expression), '{', optional ($._argument_list), '}',
    )),

    field_expression: $ => prec (PREC.call, seq (
      field ('value', $._expression), '.',
      field ('field', choice ($._field_name, $.dynamic_field)),
    )),

    /* A reserved word is an ordinary field name after a dot.  Aliased so the
       tree shape does not change for a consumer. */
    _field_name: $ => choice (
      $.identifier,
      alias (choice (...KEYWORDS), $.identifier),
    ),

    dynamic_field: $ => seq ('(', $._expression, ')'),

    _argument_list: $ => seq ($._argument, repeat (seq (',', $._argument))),

    _argument: $ => choice ($._expression, $.ignored_output, $.assignment),

    /* `this@Base (x)` and `subsref@ns.Class (s)` reach a superclass method,
       which MATLAB spells the same way. */
    superclass_reference: $ => prec (PREC.call, seq (
      field ('object', $.identifier), '@', field ('class', $._function_name),
    )),

    function_handle: $ => seq ('@', $._function_name),

    anonymous_function: $ => seq (
      '@', field ('parameters', $.parameter_list),
      field ('body', $._expression),
    ),

    /* ------------------------------------------------- matrices and cells */

    matrix: $ => seq ('[', optional ($._rows), ']'),

    cell: $ => seq ('{', optional ($._rows), '}'),

    _rows: $ => seq (
      $.row, repeat (seq (choice (';', $._newline), optional ($.row))),
    ),

    row: $ => seq (
      $._matrix_element, repeat (seq (optional (','), $._matrix_element)),
      optional (','),
    ),

    _matrix_element: $ => $._expression,
  },
});

};
