# tree-sitter-octave

A [tree-sitter](https://tree-sitter.github.io/) grammar for GNU Octave.

Octave is the language this describes and MATLAB is admitted alongside it.
Wherever a construct has two spellings, both parse and the node type is the
same, so a consumer never has to ask which dialect it was reading:

| Octave | MATLAB | Node |
|---|---|---|
| `#` comment, `#{ #}` block | `%` comment, `%{ %}` block | `comment` |
| `!`, `!=` | `~`, `~=` | `unary_expression`, `binary_expression` |
| `endfunction`, `endif`, `endfor`, `endwhile`, `endswitch`, `end_try_catch`, `end_unwind_protect` | `end` | the enclosing block |

`elseif` and `else if` are **not** two spellings of one thing and the grammar
does not treat them as such: `elseif` continues the chain and ends the whole
statement when it is taken, while `else if` is an `else` whose branch opens
with a nested `if`, which takes its own terminator and after which the branch
goes on.  They parse as `elseif_clause` and as an `else_clause` holding an
`if_statement`.

The constructs MATLAB has no equivalent of are first-class rather than bolted
on: `do ... until`, `unwind_protect`, compound assignment, `++` and `--`, and
backslash escapes in double-quoted strings.

## Why it exists

It is the syntactic half of `devtools.lsp` in the
[`devtools`](https://github.com/pr0m1th3as/octave-devtools) package, which
supplies the semantic half from a live interpreter.  The generated `parser.c`
and the tree-sitter runtime are vendored into that package at release, so a
user needs a C compiler and nothing else.  Nothing here depends on Octave, and
the grammar is usable by any editor that speaks tree-sitter.

## Two parsers, one definition

`common/define-grammar.js` is the grammar, taking the dialect as an argument.
Two parsers are generated from it, and **only the first is an editor
grammar**:

| Variant | Where | What it accepts |
|---|---|---|
| **lenient** | the repository root | Octave, and the MATLAB spelling of everything that has two.  This is what an editor maps `.m` to, what carries the queries, and what `devtools.lsp` vendors. |
| **strict** | `strict/` | only what Octave owns, so a file that passes is one MATLAB cannot read.  A checker, not an editor grammar: no queries, never registered with an editor. |

**The rule is exact: strict accepts what Octave accepts and MATLAB rejects.**
Lenient is the union of the two languages, strict Octave the difference.  So
where a construct has a choice of spelling, strict requires Octave's own, and
where both languages accept a form it is not in strict at all.

Strict therefore rejects `%` comments, `%{ %}` blocks, `~` as negation, `~=`,
a bare `end` closing any block, a function with no terminator, since a
function may be closed by `endfunction`, by `end` or by nothing and only the
first is Octave's own, and MATLAB's `arguments` block, which Octave does not
run at all.

Two carve-outs, both because the spelling is the only one there is.  `~` stays
legal as an ignored output, `[~, i] = max (v)` having no alternative in either
language.  And `%!` stays legal, since `octave/scripts/testfun/test.m` tests
`strncmp (ln, "%!", 2)` and accepts no other BIST marker, so rejecting it
would fail every package file that has tests.

Run over the packages here, strict flags **20 files**: 17 in `statistics` and
3 in `csg-toolkit`, between them 34 `%` comments and three bare `end`
terminators, one of them `if (1 < MaxIter), end`.  `datatypes`, `drafting`
and `devtools` are clean.

## Building

```
tree-sitter generate --no-bindings   # grammar.js -> src/parser.c
(cd strict && tree-sitter generate --no-bindings)
tree-sitter test                     # 56 corpus tests, on the lenient parser
tools/dialect-test.py                # 58 assertions the variants must differ on
tree-sitter parse FILE               # print the tree for one file
make                                 # libtree-sitter-octave.so, .a and the .pc
```

`tree-sitter test` cannot express "parses here, fails there", which is the
whole content of a dialect rule, so `tools/dialect-test.py` carries those
cases from `test/dialect/`.  Each case names the verdict it expects from each
variant.  Without it the two variants drift and nobody notices.

The scanner is shared: `common/scanner.h` holds it, and each variant's
`src/scanner.c` defines `TS_LANG` before including it, since tree-sitter
derives the exported names from the grammar's name.  `TS_STRICT` also switches
the token enum, because strict declares one external fewer.

**`--no-bindings` is not optional.**  The bare command writes Node, Go,
Python, Rust and Swift bindings, which do nothing unless published to four
registries and are regenerated every time if not suppressed.  The C binding
and the `Makefile` are kept, because they build a shared library with no
publishing at all, which is how a grammar reaches Emacs, Nix and Debian: a
Debian grammar package ships these generated sources and depends on `gcc`,
`make` and `pkgconf` to build the object locally.

## Queries

`queries/highlights.scm` for syntax highlighting, `queries/tags.scm` for
definitions and references, and `queries/locals.scm` for scopes.  The
transpose operator is the scanner's token and is hidden, so it is the one
operator a highlight query cannot reach.

## Where it stands

Measured by parsing whole trees and counting a file as failing if it holds any
`ERROR` node anywhere, which is stricter than recovering the file's top-level
definition:

| Corpus | Files | Clean |
|---|---|---|
| `statistics` | 751 | 100 per cent |
| core `scripts` | 1044 | 100 per cent |
| `datatypes`, `csg-toolkit`, `drafting` | 157 | 100 per cent |
| **total** | **1952** | **100 per cent** |

Measured on the MATLAB side too, against the 68 MATLAB probe files kept here
for checking behaviour against the real thing: **100 per cent**.  That corpus
is what caught `?handle` as a metaclass literal outside an attribute list, and
`which f -all`, where Octave tells a flag from a subtraction by the space
after the sign rather than the one before it.

For comparison, the grammar an editor reaches for today when it opens a `.m`
file, `acristoffers/tree-sitter-matlab` 1.3.1, recovers a file's own top-level
definition in 4 per cent of the package files here.

A clean parse is necessary and not sufficient: a tree can be wrong without
holding an `ERROR`.  Statements are therefore required to be separated rather
than merely adjacent, so that syntax this grammar does not model fails loudly
instead of parsing into something plausible.  That rule is what makes
`1:2:10` one range rather than a range, a stray colon and a number, and it is
what would have caught the transpose read as a string.

## What the scanner is for

Four things the grammar cannot decide on its own, all in `src/scanner.c`:

- **Whether a newline ends a statement.**  At the top level it does, inside
  parentheses it is whitespace, which is what lets an expression break after
  an operator with no `...`, and inside brackets it separates rows.  The parse
  state cannot answer this, since tree-sitter reports an external token as
  valid wherever it could recover with one.
- **Whether a quote is a transpose or opens a string.**  Octave decides by the
  space before it and only inside brackets, so `[a' b']` transposes and
  `[a 'txt']` concatenates.
- **What a word is.**  The scanner lexes every word itself and decides among
  four outcomes: the last subscript, the keyword opening an argument
  validation block, an ordinary name, or a word Octave reserves, which it
  declines and leaves to the grammar's own token.  That last one is the point:
  Octave reserves `end`, so `end = 5` is a syntax error there, and a grammar
  that lets the word fall back to a name accepts it.  One lexer rather than
  several, because a block that steps over a prefix and then declines does not
  reliably leave the position where it found it, which showed up as `abc`
  parsing as an error followed by `bc`.
- **Whether `arguments` opens MATLAB's validation block or names a
  variable** (lenient only).  It opens a block only on its own line or before an attribute
  list; used any other way it is an ordinary Octave name, and the grammar is
  not allowed to reserve the word.
- **Whether `end` closes a block or is the last subscript.**  Inside a
  bracket it is a subscript, since no block can open there; a longer word
  beginning `end`, such as `endif`, is left to the grammar.
- **Where a command begins.**  `hold on` is `hold ('on')` when a space
  separates the name from a word that cannot continue an expression, and only
  at the top level, since inside brackets a space separates elements.
- **The bracket stack** the other three need, which is why the delimiters are
  the scanner's tokens.  They stay ordinary literals in the tree, so a query
  can still reach them.

## Reserved words

`iskeyword ()` on Octave 11.3.0 is the list, less `__FILE__` and `__LINE__`,
which are keywords there but behave as values.  The scanner refuses to lex any
of them as a name, so `end = 5`, `otherwise = 5` and `until = 5` fail as they
do in Octave.

Two positions take them back, because Octave does:

- **After a dot they are ordinary field names.**  `finfo.function` is in
  core's `vectorize.m`.
- **A method may be named for one.**  `datatypes` overloads `end` on every
  array class: `function last_index = end (this, end_dim, ndim_obj)`.

Both are aliased back to `identifier`, so the tree shape is unchanged for a
consumer.  `arguments`, `properties`, `methods`, `events` and `enumeration`
are **not** keywords in Octave: outside the construct that uses them they are
ordinary names, and the grammar treats them so.

## Known gaps

None that any of the 2020 files measured here reach.  Every Octave file and
every MATLAB file parses with no `ERROR` node, the dialect cases all hold, and
the constructs MATLAB has that Octave does not parse as their own nodes rather
than as an approximation.

A body may share its header's line, `if (c) x = 1; endif`, only where the
condition is parenthesised, and after a keyword-only header such as `else` or
`otherwise`.  That restriction is what makes it safe: the closing bracket is
what ends the condition, so the parser cannot go on extending it across the
body.  Without it, `if a (b)` stops being a call.

## Licence

GPL-3.0-or-later, as the packages this serves are.
