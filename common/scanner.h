/* External scanner for the Octave grammar.
 *
 * Copyright (C) 2026 Andreas Bertsatos <abertsatos@biol.uoa.gr>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One job: decide whether a newline ends a statement.  At the top level it
 * does.  Inside parentheses it is whitespace, which is what lets an
 * expression break after an operator with no continuation marker.  Inside
 * brackets and braces it separates the rows of a matrix or a cell, so it
 * still carries meaning and is emitted.
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

typedef struct {
  char stack[MAX_DEPTH];
  unsigned depth;
} Scanner;

void *TS_FN (_external_scanner_create) (void)
{
  Scanner *s = (Scanner *) malloc (sizeof (Scanner));
  if (s != NULL)
    s->depth = 0;
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
  if (n > TREE_SITTER_SERIALIZATION_BUFFER_SIZE)
    n = TREE_SITTER_SERIALIZATION_BUFFER_SIZE;
  memcpy (buffer, s->stack, n);
  return n;
}

void TS_FN (_external_scanner_deserialize) (void *payload,
                                                      const char *buffer,
                                                      unsigned length)
{
  Scanner *s = (Scanner *) payload;
  s->depth = length;
  if (length > 0)
    memcpy (s->stack, buffer, length);
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

  for (;;)
    {
      bool spaced = false;
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

      /* One lexer for every word.  Nothing else may step over a prefix and
         then decline: a scanner that gives up after advancing does not
         reliably leave the position where it found it, which showed up as
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
          bool in_matrix = (s->depth > 0
                            && (s->stack[s->depth - 1] == '['
                                || s->stack[s->depth - 1] == '{'));
          if (! (spaced && in_matrix))
            return emit (lexer, TRANSPOSE);
          return false;
        }

      switch (lexer->lookahead)
        {
        case '(':
          push (s, '(');
          return emit (lexer, LPAREN);
        case '[':
          push (s, '[');
          return emit (lexer, LBRACKET);
        case '{':
          push (s, '{');
          return emit (lexer, LBRACE);
        case ')':
          pop (s);
          return emit (lexer, RPAREN);
        case ']':
          pop (s);
          return emit (lexer, RBRACKET);
        case '}':
          pop (s);
          return emit (lexer, RBRACE);
        default:
          break;
        }

      if (lexer->lookahead != '\n' && lexer->lookahead != '\r')
        return false;

      if (! (s->depth > 0 && s->stack[s->depth - 1] == '('))
        {
          if (! valid_symbols[NEWLINE])
            return false;
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
