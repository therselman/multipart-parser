/**
 *  Copyright (c) 2019 Trevor Herselman. All rights reserved.
 *
 *  MIT License
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "multipart_parser.h"
#include <assert.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include <stdio.h>

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define SET_ERRNO(e)                                                 \
do {                                                                 \
  parser->multipart_errno = (e);                                     \
} while(0)

#ifdef __GNUC__
# define LIKELY(X) __builtin_expect(!!(X), 1)
# define UNLIKELY(X) __builtin_expect(!!(X), 0)
#else
# define LIKELY(X) (X)
# define UNLIKELY(X) (X)
#endif

#ifndef UNREACHABLE
# ifdef _MSC_VER
#  define UNREACHABLE __assume(0)
# else	/* GCC, Clang & Intel C++ */
#  define UNREACHABLE __builtin_unreachable()
# endif
#endif

#ifndef FALLTHROUGH
# if defined(__GNUC__) || defined(__clang__)
#  define FALLTHROUGH __attribute__((fallthrough))
# else
#  define FALLTHROUGH ((void)0)
# endif
#endif


enum state
  { s_start
  , s_start_dash
  , s_boundary
  , s_boundary_cr
  , s_boundary_almost_done
  , s_header_field_start
  , s_header_field
  , s_header_value_discard_ws
  , s_header_value
  , s_header_value_lws
  , s_header_almost_done

  , s_headers_almost_done
  , s_headers_done

  , s_body_part_start
  , s_body_part

  , s_body_part_boundary
  , s_body_part_boundary_dash
  , s_body_part_boundary_dash_dash
  , s_body_part_boundary_compare
  };

/* Macros for character classes */
#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_HEX(c)           (IS_NUM(c) || (LOWER(c) >= 'a' && LOWER(c) <= 'f'))


void multipart_parser_init(multipart_parser *parser)
{
  parser->state = s_start;
}

void multipart_parser_settings_init(multipart_parser_settings *settings)
{
  memset(settings, 0, sizeof(*settings));
}

int multipart_parser_execute(multipart_parser *parser,
                             const multipart_parser_settings *settings,
                             const char *data,
                             size_t len)
{
  const char* buf_end = &data[len];
  const char* p = data;

  const char* body_start;
  const char* body_end;

  char buf[100];

  for (; p < buf_end; ++p)
  {
    const char ch = *p;

    switch (parser->state)
    {
      case s_start:
        if (LIKELY(ch == '-')) {
          parser->state = s_start_dash;
        }
        continue;

      case s_start_dash:
        if (LIKELY(ch == '-')) {
          parser->nread = 0;
          parser->state = s_boundary;
          continue;
        }
        return -1;

      case s_boundary:
        if (LIKELY(parser->nread < parser->boundary_len)) {
          if (LIKELY(ch == parser->boundary[parser->nread++])) {
            continue;
          }
        } else {
          if (LIKELY(ch == '\r')) {
            parser->state = s_boundary_cr;
            continue;
          } else if (ch == '-') {
            parser->state = s_boundary_almost_done;
            continue;
          }
        }
        return -1;

      case s_boundary_cr:
        if (LIKELY(ch == '\n')) {
          if (LIKELY(settings->on_boundary_begin(parser) == 0)) {
            parser->state = s_header_field_start;
            continue;
          }
        }
        return -1;

      case s_boundary_almost_done:
        if (LIKELY(ch == '-')) {
          return settings->on_body_parts_complete(parser);
        }
        return -1;

      case s_headers_almost_done:
        if (ch == '\r') {
          parser->state = s_headers_done;
          continue;
        }
        FALLTHROUGH;

      case s_header_field_start:
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
          parser->nread = 1;
          parser->state = s_header_field;
          continue;
        }
        return -1;

      case s_header_field:
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-') {
          parser->nread++;
          continue;
        } else if (ch == ':') {
          if (settings->on_header_field(
                parser,
                p - parser->nread,
                parser->nread ) == 0) {
            parser->state = s_header_value_discard_ws;
            continue;
          }
        }
        return -1;

      case s_header_value_discard_ws:
        if (ch > ' ') {
          parser->nread = 1;
          parser->state = s_header_value;
          continue;
        } if (ch == ' ') {
          continue;
        }
        return -1;

      case s_header_value:
        if (ch != '\r') {
          parser->nread++;
          continue;
        }
        if (settings->on_header_value(
              parser,
              p - parser->nread,
              parser->nread) == 0) {
          parser->state = s_header_almost_done;
          continue;
        }
        return -1;

      case s_header_almost_done:
        if (ch == '\n') {
          parser->state = s_headers_almost_done;
          continue;
        }

      case s_header_value_lws:
        return -1;

      case s_headers_done:
        if (ch == '\n') {
          if (LIKELY( settings->on_headers_complete(parser) == 0)) {
            parser->state = s_body_part_start;
            continue;
          }
        }
        return -1;

      case s_body_part_start:
        body_start = p;
        parser->state = s_body_part;
        FALLTHROUGH;

      case s_body_part:
        if (LIKELY(ch != '\r')) {
          continue;
        }

        body_end = p;
        parser->state = s_body_part_boundary;
        continue;

      case s_body_part_boundary:
        if (ch == '\n') {
          parser->state = s_body_part_boundary_dash;
          continue;
        }

        if (ch == '\r') {
          body_end = p;
          continue;
        }

        parser->state = s_body_part;
        continue;

      case s_body_part_boundary_dash:
        if (ch == '-') {
          parser->state = s_body_part_boundary_dash_dash;
          continue;
        }

        if (ch == '\r') {
          body_end = p;
          parser->state = s_body_part_boundary;
          continue;
        }

        parser->state = s_body_part;
        continue;

      case s_body_part_boundary_dash_dash:
        if (ch == '-') {
          parser->nread = 0;
          parser->state = s_body_part_boundary_compare;
          continue;
        }

        if (ch == '\r') {
          body_end = p;
          parser->state = s_body_part_boundary;
          continue;
        }

        parser->state = s_body_part;
        continue;

      case s_body_part_boundary_compare:
        if (LIKELY(parser->nread < parser->boundary_len)) {
          if (LIKELY(ch == parser->boundary[parser->nread++])) {
            continue;
          }

          if (ch == '\r') {
            body_end = p;
            parser->state = s_body_part_boundary;
            continue;
          }

          parser->state = s_body_part;
          continue;
        } else {
          if (settings->on_body(parser, body_start,
                                body_end - body_start) == 0) {
            if (LIKELY(ch == '\r')) {
              parser->state = s_boundary_cr;
              continue;
            }

            if (ch == '-') {
              parser->state = s_boundary_almost_done;
              continue;
            }
          }
        }
        return -1;

      default:
        UNREACHABLE;
    }
    UNREACHABLE;
  }

  return -1;
}

const char* multipart_get_name(const char* str, size_t len,
                               size_t* value_len)
{
  const char* str_end = &str[len];
  const char* p = str;

  const char* value_start;

  typedef enum
  { s_seek
  , s_N
  , s_NA
  , s_NAM
  , s_NAME
  , s_NAME_EQ
  , s_NAME_EQ_QUOT
  , s_value_start
  , s_value
  , s_value_end
  } e_state;

  for (e_state state = s_seek; p < str_end; ++p)
  {
    const char ch = *p;

    switch (state)
    {
      case s_seek:
      _reset:
        if (UNLIKELY(LOWER(ch) == 'n')) {
          state = s_N;
        }
        continue;

      case s_N:
        if (LIKELY(ch == 'a' || ch == 'A')) {
          state = s_NA;
        } else {
          state = s_seek;
          goto _reset;
        }
        continue;

      case s_NA:
        if (LIKELY(ch == 'm' || ch == 'M')) {
          state = s_NAM;
        } else {
          state = s_seek;
          goto _reset;
        }
        continue;

      case s_NAM:
        if (LIKELY(ch == 'e' || ch == 'E')) {
          state = s_NAME;
        } else {
          state = s_seek;
          goto _reset;
        }
        continue;

      case s_NAME:
        if (LIKELY(ch == '=')) {
          state = s_NAME_EQ;
        } else {
          if (ch == ' ') {                                /*  Skip whitespace */
            continue;
          }

          state = s_seek;
          goto _reset;
        }
        continue;

      case s_NAME_EQ:
        if (LIKELY(ch == '"')) {
          state = s_value_start;
        } else {
          if (ch == ' ') {                                /*  Skip whitespace */
            continue;
          }

          state = s_seek;
          goto _reset;
        }
        continue;

      case s_value_start:
        value_start = p;

        if (LIKELY(ch != '"')) {
          state = s_value;
        } else {
          *value_len = 0;                          /* detected an empty value */
          return value_start;
        }
        continue;

      case s_value:
        if (LIKELY(ch != '"')) {
          continue;
        } else {
          *value_len = p - value_start;
          return value_start;
        }

      default:
        UNREACHABLE;
    }
  }

  return NULL;
}

const char* multipart_get_filename(const char* str, size_t len,
                                   size_t* value_len)
{
  const char* str_end = &str[len];
  const char* p = str;

  const char* value_start;

  typedef enum
    { s_F
    , s_FI
    , s_FIL
    , s_FILE
    , s_FILEN
    , s_FILENA
    , s_FILENAM
    , s_FILENAME
    , s_FILENAME_EQ
    , s_FILENAME_EQ_QUOT
    , s_value_start
    , s_value
  } e_state;

  for (e_state state = s_F; p < str_end; ++p)
  {
    const char ch = *p;

    switch (state)
    {
      case s_F:
      _reset:
        if (UNLIKELY(LOWER(ch) == 'f')) {
          state = s_FI;
        }
        continue;

      case s_FI:
        if (LIKELY(ch == 'i') || ch == 'I') {
          state = s_FIL;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FIL:
        if (LIKELY(ch == 'l') || ch == 'L') {
          state = s_FILE;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILE:
        if (LIKELY(ch == 'e') || ch == 'E') {
          state = s_FILEN;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILEN:
        if (LIKELY(ch == 'n') || ch == 'N') {
          state = s_FILENA;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILENA:
        if (LIKELY(ch == 'a') || ch == 'A') {
          state = s_FILENAM;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILENAM:
        if (LIKELY(ch == 'm') || ch == 'M') {
          state = s_FILENAME;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILENAME:
        if (LIKELY(ch == 'e') || ch == 'E') {
          state = s_FILENAME_EQ;
        } else {
          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILENAME_EQ:
        if (LIKELY(ch == '=')) {
          state = s_FILENAME_EQ_QUOT;
        } else {
          if (ch == ' ') {                                /*  Skip whitespace */
            continue;
          }

          state = s_F;
          goto _reset;
        }
        continue;

      case s_FILENAME_EQ_QUOT:
        if (LIKELY(ch == '"')) {
          state = s_value_start;
        } else {
          if (ch == ' ') {                                /*  Skip whitespace */
            continue;
          }

          state = s_F;
          goto _reset;
        }
        continue;

      case s_value_start:
        value_start = p;
        state = s_value;
        FALLTHROUGH;

      case s_value:
        if (LIKELY(ch != '"')) {
          continue;
        } else {
          *value_len = p - value_start;
          return value_start;
        }

      default:
        UNREACHABLE;
    }
  }

  return NULL;
}
