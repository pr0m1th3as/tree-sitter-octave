/* External scanner for the Octave grammar.
 *
 * Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One job: decide whether a newline ends a statement.  At the top level it
 * does.  Inside parentheses and the braces of a cell index it is
 * whitespace, which is what lets an expression break after an operator with
 * no continuation marker.  Inside brackets and the braces of a cell it
 * separates the rows of a matrix or a cell, so it still carries meaning and
 * is emitted.
 *
 * The parse state cannot answer this: tree-sitter reports an external token
 * as valid in every state that could recover with one, so a newline reads as
 * acceptable even in the middle of a parenthesised expression.  The scanner
 * therefore keeps the bracket stack itself, which is why the delimiters are
 * its tokens rather than the grammar's.
 */

#include "tree_sitter/parser.h"
#include <string.h>

/* One scanner, two parsers.  tree-sitter derives the exported names from the
   grammar's name, so each variant defines TS_LANG before including this. */
#ifndef TS_LANG
#define TS_LANG octave
#endif
#define TS_CAT_(a, b) a ## b
#define TS_CAT(a, b) TS_CAT_(a, b)
#define TS_FN(suffix) TS_CAT (TS_CAT (tree_sitter_, TS_LANG), suffix)

enum TokenType {
  NEWLINE,
  TRANSPOSE,
  COMMAND_ARGUMENT,
  END_INDEX,
#ifndef TS_STRICT
  ARGUMENTS_KEYWORD,
#endif
  LPAREN,
  RPAREN,
  LBRACKET,
  RBRACKET,
  LBRACE,
  RBRACE,
  IDENTIFIER,
  ELEMENT_GAP,
  LINE_CONTINUATION,
  ANONYMOUS_LPAREN,
  COMMA,
  SEMICOLON,
  SPACED_LPAREN,
  INDEX_LBRACE,
};

/* Octave's own reserved words, from `iskeyword ()` on 11.3.0.  `__FILE__`
   and `__LINE__` are keywords there too but behave as values, so they are
   left to lex as names.  `arguments`, `properties`, `methods`, `events` and
   `enumeration` are not keywords: outside the construct that uses them they
   are ordinary names. */
static const char *const RESERVED[] = {
  "break", "case", "catch", "classdef", "continue", "do", "else", "elseif",
  "end", "end_try_catch", "end_unwind_protect", "endarguments", "endclassdef",
  "endenumeration", "endevents", "endfor", "endfunction", "endif",
  "endmethods", "endparfor", "endproperties", "endspmd", "endswitch",
  "endwhile", "for", "function", "global", "if", "otherwise", "parfor",
  "persistent", "return", "spmd", "switch", "try", "until", "unwind_protect",
  "unwind_protect_cleanup", "while",
};

static bool is_reserved (const char *word, unsigned length)
{
  for (unsigned i = 0; i < sizeof (RESERVED) / sizeof (RESERVED[0]); i++)
    if (strlen (RESERVED[i]) == length
        && strncmp (RESERVED[i], word, length) == 0)
      return true;
  return false;
}

static bool word_char (int32_t c)
{
  return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '_');
}

#define MAX_DEPTH 256

/* `continued` is set by a line continuation and read by the next scan, so
   that the continuation counts as the space it stands for. */
typedef struct {
  char stack[MAX_DEPTH];
  unsigned depth;
  bool continued;
} Scanner;

void *TS_FN (_external_scanner_create) (void)
{
  Scanner *s = (Scanner *) malloc (sizeof (Scanner));
  if (s != NULL)
    {
      s->depth = 0;
      s->continued = false;
    }
  return s;
}

void TS_FN (_external_scanner_destroy) (void *payload)
{
  free (payload);
}

unsigned TS_FN (_external_scanner_serialize) (void *payload,
                                                        char *buffer)
{
  Scanner *s = (Scanner *) payload;
  unsigned n = s->depth;
  if (n > TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 1)
    n = TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 1;
  buffer[0] = (char) s->continued;
  memcpy (buffer + 1, s->stack, n);
  return n + 1;
}

void TS_FN (_external_scanner_deserialize) (void *payload,
                                                      const char *buffer,
                                                      unsigned length)
{
  Scanner *s = (Scanner *) payload;
  s->depth = 0;
  s->continued = false;
  if (length > 0)
    {
      s->continued = (bool) buffer[0];
      s->depth = length - 1;
      memcpy (s->stack, buffer + 1, s->depth);
    }
}

static void push (Scanner *s, char c)
{
  if (s->depth < MAX_DEPTH)
    s->stack[s->depth++] = c;
}

static void pop (Scanner *s)
{
  if (s->depth > 0)
    s->depth--;
}

/* The stack holds the open delimiters, plus 'a' for the parentheses around
   an anonymous function's parameters, 'b' for the body of an anonymous
   function written directly inside brackets, and 'i' for the braces of a
   cell index, inside which a space separates nothing. */
static char top (Scanner *s)
{
  return (s->depth > 0 ? s->stack[s->depth - 1] : 0);
}

/* Octave ends an anonymous function's body at a comma, a semicolon, a line
   break or a closing delimiter.  Until then a space inside brackets
   separates nothing: `{@(t) abs (t), 2}` holds two elements. */
static void end_bodies (Scanner *s)
{
  while (top (s) == 'b')
    pop (s);
}

static bool command_argument_starts (int32_t c)
{
  switch (c)
    {
    case 0: case '\n': case '\r': case ';': case ',':
    case '%': case '#': case '=':
    case '(': case ')': case '[': case ']': case '{': case '}':
    case '+': case '-': case '*': case '/': case '\\': case '^':
    case '<': case '>': case '&': case '|': case '!': case '~':
    case ':': case '@': case '.':
      return false;
    default:
      return true;
    }
}

static bool emit (TSLexer *lexer, enum TokenType type)
{
  lexer->advance (lexer, false);
  lexer->result_symbol = type;
  return true;
}

bool TS_FN (_external_scanner_scan) (void *payload, TSLexer *lexer,
                                               const bool *valid_symbols)
{
  Scanner *s = (Scanner *) payload;
  bool continued = s->continued;
  s->continued = false;

  for (;;)
    {
      bool spaced = continued;
      continued = false;
      while (lexer->lookahead == ' ' || lexer->lookahead == '\t')
        {
          spaced = true;
          lexer->advance (lexer, true);
        }

      if (valid_symbols[COMMAND_ARGUMENT] && spaced && s->depth == 0
          && (command_argument_starts (lexer->lookahead)
              || lexer->lookahead == '-' || lexer->lookahead == '+'))
        {
          /* `hold on` calls hold ('on'), and `title 'a b'` passes the quoted
             text whole.  Octave reads a word after a name as an argument
             when a space separates them and what follows cannot continue an
             expression: not a delimiter, not an assignment, and not an
             operator with a space after it, which is what tells `x + 1` from
             `x +1`.  Only at the top level, since inside brackets a space
             separates matrix elements.  Anything else falls through, so a
             delimiter is still the switch's to emit. */
          /* `which f -all` passes the flag; `x - 1` subtracts.  Octave tells
             them apart by the space after the sign, not before it. */
          if (lexer->lookahead == '-' || lexer->lookahead == '+')
            {
              int32_t sign = lexer->lookahead;
              lexer->advance (lexer, false);
              /* `a += 1` assigns and `a ++` increments, whatever the spacing
                 before the sign; only a flag follows it directly. */
              if (lexer->lookahead == ' ' || lexer->lookahead == '\t'
                  || lexer->lookahead == 0 || lexer->lookahead == '\n'
                  || lexer->lookahead == '\r' || lexer->lookahead == '='
                  || lexer->lookahead == sign)
                return false;
              while (lexer->lookahead != 0 && lexer->lookahead != ' '
                     && lexer->lookahead != '\t' && lexer->lookahead != '\n'
                     && lexer->lookahead != '\r' && lexer->lookahead != ';'
                     && lexer->lookahead != ',')
                lexer->advance (lexer, false);
              lexer->result_symbol = COMMAND_ARGUMENT;
              return true;
            }

          int32_t quote = lexer->lookahead;
          if (quote == '\'' || quote == '"')
            {
              lexer->advance (lexer, false);
              while (lexer->lookahead != 0 && lexer->lookahead != '\n'
                     && lexer->lookahead != '\r')
                {
                  bool closing = (lexer->lookahead == quote);
                  lexer->advance (lexer, false);
                  if (closing && lexer->lookahead != quote)
                    break;
                  if (closing)
                    lexer->advance (lexer, false);
                }
            }
          else
            {
              while (lexer->lookahead != 0 && lexer->lookahead != ' '
                     && lexer->lookahead != '\t' && lexer->lookahead != '\n'
                     && lexer->lookahead != '\r' && lexer->lookahead != ';'
                     && lexer->lookahead != ',')
                lexer->advance (lexer, false);
            }
          lexer->result_symbol = COMMAND_ARGUMENT;
          return true;
        }

      bool in_matrix = (top (s) == '[' || top (s) == '{');

      /* Only an anonymous function's parameters can follow `@`, so no parse
         state takes both of these; error recovery offers every token. */
      bool recovering = (valid_symbols[ANONYMOUS_LPAREN]
                         && valid_symbols[LPAREN]);

      /* A line continuation is the scanner's, so that it counts as the
         space it stands for: `[v...` with `(w)]` opening the next line is
         two elements, as in Octave.  `...` runs to the end of the line.  A
         dot and a digit after a space inside brackets open a new element,
         as in `[1 .5]`.  Every other dot is declined; a scan that declines
         is reset to where it began, so looking ahead here is safe as long
         as nothing falls through to another block. */
      if (lexer->lookahead == '.')
        {
          lexer->mark_end (lexer);
          lexer->advance (lexer, false);
          if (lexer->lookahead >= '0' && lexer->lookahead <= '9')
            {
              if (in_matrix && spaced && ! recovering
                  && valid_symbols[ELEMENT_GAP])
                {
                  lexer->result_symbol = ELEMENT_GAP;
                  return true;
                }
              return false;
            }
          if (lexer->lookahead != '.')
            return false;
          lexer->advance (lexer, false);
          if (lexer->lookahead != '.')
            return false;
          while (lexer->lookahead != 0 && lexer->lookahead != '\n'
                 && lexer->lookahead != '\r')
            lexer->advance (lexer, false);
          if (lexer->lookahead == '\r')
            lexer->advance (lexer, false);
          if (lexer->lookahead == '\n')
            lexer->advance (lexer, false);
          lexer->mark_end (lexer);
          s->continued = true;
          lexer->result_symbol = LINE_CONTINUATION;
          return true;
        }

      /* Inside brackets Octave turns a space between two elements into a
         comma, and the scanner does the same with a token of no width.  A
         space opens a new element before a name, a number, a delimiter, a
         string or `@`; before `!` or `~` unless `=` follows; and before a
         sign unless a blank or `=` follows, so `[1 -2]` is two elements and
         `[1 - 2]` one.  Not directly inside an anonymous function's body in
         Octave, where a space separates nothing. */
      if (in_matrix && spaced && ! recovering && valid_symbols[ELEMENT_GAP])
        {
          int32_t c = lexer->lookahead;
          if (word_char (c) || c == '(' || c == '[' || c == '{'
              || c == '\'' || c == '"' || c == '@')
            {
              lexer->mark_end (lexer);
              lexer->result_symbol = ELEMENT_GAP;
              return true;
            }
          if (c == '!' || c == '~' || c == '+' || c == '-')
            {
              lexer->mark_end (lexer);
              lexer->advance (lexer, false);
              int32_t n = lexer->lookahead;
              bool opens = (c == '!' || c == '~')
                           ? (n != '=')
                           : ! (n == ' ' || n == '\t' || n == '\n'
                                || n == '\r' || n == 0 || n == '=');
              if (! opens)
                return false;
              lexer->result_symbol = ELEMENT_GAP;
              return true;
            }
        }

      /* One lexer for every word.  Nothing else may step over a prefix and
         then fall through: a block that gives up after advancing and lets
         the next block run leaves the position wrong, which showed up as
         `abc` parsing as an error followed by `bc`.
         A word is the last subscript, the keyword opening an argument
         validation block, a name, or a word Octave reserves, which is
         declined here and left to the grammar's own token.  Reserving them
         is the point: `end = 5` is a syntax error in Octave, and without
         this the lexer has a legal alternative wherever the keyword is not
         valid and takes it. */
      if ((lexer->lookahead >= 'a' && lexer->lookahead <= 'z')
          || (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z')
          || lexer->lookahead == '_')
        {
          char buffer[64];
          unsigned length = 0;
          while (word_char (lexer->lookahead))
            {
              if (length < sizeof (buffer))
                buffer[length] = (char) lexer->lookahead;
              length++;
              lexer->advance (lexer, false);
            }

          if (length >= sizeof (buffer))
            {
              if (! valid_symbols[IDENTIFIER])
                return false;
              lexer->result_symbol = IDENTIFIER;
              return true;
            }

          if (valid_symbols[END_INDEX] && s->depth > 0
              && length == 3 && strncmp (buffer, "end", 3) == 0)
            {
              lexer->result_symbol = END_INDEX;
              return true;
            }

#ifndef TS_STRICT
          if (valid_symbols[ARGUMENTS_KEYWORD]
              && length == 9 && strncmp (buffer, "arguments", 9) == 0)
            {
              lexer->mark_end (lexer);
              while (lexer->lookahead == ' ' || lexer->lookahead == '\t')
                lexer->advance (lexer, false);
              int32_t next = lexer->lookahead;
              if (next == '\n' || next == '\r' || next == '(' || next == '%'
                  || next == '#' || next == ';' || next == ',')
                {
                  lexer->result_symbol = ARGUMENTS_KEYWORD;
                  return true;
                }
              if (! valid_symbols[IDENTIFIER])
                return false;
              lexer->result_symbol = IDENTIFIER;
              return true;
            }
#endif

          if (is_reserved (buffer, length))
            return false;
          if (! valid_symbols[IDENTIFIER])
            return false;
          lexer->result_symbol = IDENTIFIER;
          return true;
        }

      if (lexer->lookahead == '\'' && valid_symbols[TRANSPOSE])
        {
          /* Octave tells a transpose from a string by the space before the
             quote, and only inside brackets, where `[a 'txt']` concatenates
             and `[a' b']` transposes.  Outside them nothing can follow an
             expression, so the quote is a transpose however it is spaced. */
          if (! (spaced && in_matrix))
            return emit (lexer, TRANSPOSE);
          return false;
        }

      switch (lexer->lookahead)
        {
        case '(':
          if (valid_symbols[ANONYMOUS_LPAREN] && ! valid_symbols[LPAREN])
            {
              push (s, 'a');
              return emit (lexer, ANONYMOUS_LPAREN);
            }
          push (s, '(');
          /* Octave's style writes a call with a space before its
             parenthesis and an index without one, `max (2, 5)` against
             `x(2)`.  The token carries the space into the tree and changes
             nothing about the parse. */
          if (spaced && ! in_matrix && ! recovering
              && valid_symbols[SPACED_LPAREN])
            return emit (lexer, SPACED_LPAREN);
          return emit (lexer, LPAREN);
        case '[':
          push (s, '[');
          return emit (lexer, LBRACKET);
        case '{':
          /* A brace after an expression indexes it, as in Octave, however
             it is spaced outside brackets; inside them a space has already
             opened a new element. */
          if (! recovering && valid_symbols[INDEX_LBRACE])
            {
              push (s, 'i');
              return emit (lexer, INDEX_LBRACE);
            }
          push (s, '{');
          return emit (lexer, LBRACE);
        case ')':
          {
            end_bodies (s);
            bool parameters = (top (s) == 'a');
            pop (s);
            /* Octave reads an anonymous function's body inside brackets with
               no separators in it.  MATLAB does not: a space separates
               elements there too, so `{@(t) t (1) 2}` holds three. */
#ifndef TS_MATLAB
            if (parameters && (top (s) == '[' || top (s) == '{'))
              push (s, 'b');
#else
            (void) parameters;
#endif
            return emit (lexer, RPAREN);
          }
        case ']':
          end_bodies (s);
          pop (s);
          return emit (lexer, RBRACKET);
        case '}':
          end_bodies (s);
          pop (s);
          return emit (lexer, RBRACE);
        case ',':
          end_bodies (s);
          return emit (lexer, COMMA);
        case ';':
          end_bodies (s);
          return emit (lexer, SEMICOLON);
        default:
          break;
        }

      if (lexer->lookahead != '\n' && lexer->lookahead != '\r')
        return false;

      end_bodies (s);
      if (! (top (s) == '(' || top (s) == 'a' || top (s) == 'i'))
        {
          lexer->advance (lexer, false);
          lexer->result_symbol = NEWLINE;
          return true;
        }

      /* A break inside parentheses continues the expression.  The loop goes
         round because a scanner that returns false hands the next token to
         the internal lexer as well, which has no rule for a delimiter: the
         break is stepped over here so that a line opening with one is still
         this scanner's to emit.  Where the next token is not a delimiter the
         return discards these steps and the newline in `extras` takes it. */
      while (lexer->lookahead == '\n' || lexer->lookahead == '\r'
             || lexer->lookahead == ' ' || lexer->lookahead == '\t')
        lexer->advance (lexer, true);
    }
}
