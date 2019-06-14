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

#ifndef multipart_parser_h
#define multipart_parser_h
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
  (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

typedef struct multipart_parser multipart_parser;
typedef struct multipart_parser_settings multipart_parser_settings;

/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 */
typedef int (*multipart_data_cb) (multipart_parser*, const char *at, size_t length);
typedef int (*multipart_cb) (multipart_parser*);

struct multipart_parser_settings {
  multipart_cb      on_boundary_begin;
  multipart_data_cb on_header_field;
  multipart_data_cb on_header_value;
  multipart_cb      on_headers_complete;
  multipart_data_cb on_body;
  multipart_cb      on_body_parts_complete;
  multipart_data_cb on_debug;
};

struct multipart_parser {
  /** PRIVATE **/
  unsigned char state;                       /* enum state from http_parser.c */

  uint64_t nread;                        /* # bytes read in various scenarios */

  /** READ-ONLY **/
  unsigned char multipart_errno;

  /** PUBLIC **/
  void *data; /* A pointer to get hook to the "connection" or "socket" object */

  const char* boundary;   /* set this to a boundary string taken from headers */
  size_t boundary_len;
};

void multipart_parser_init(multipart_parser *parser);

/* Initialize multipart_parser_settings members to 0
 */
void multipart_parser_settings_init(multipart_parser_settings *settings);

/* `return -1` on error
 * Sets `parser->multipart_errno` on error.
 */
int multipart_parser_execute(multipart_parser *parser,
                             const multipart_parser_settings *settings,
                             const char *data,
                             size_t len);

/* Helper method to get the boundary string from Content-Type header */
const char* get_boundary(const char* str, size_t str_len, size_t* boundary_len);

/**
 */
const char* multipart_get_name(const char* str, size_t len, size_t* value_len);

/**
 * Helper function to get the `filename` value from a header string value
 * eg. `form-data; name="file1"; filename="我a私aaaa\"xxx\"abc"`
 * returns: `我a私aaaa"xxx"abc`
 * Extracts and decodes the `filename` string into buffer,
 *   performs URL decoding, and slash string decoding!
 * Firefox will encode a double quote `"` character into \"
 * Chrome will use %22
 */
const char* multipart_get_filename(const char* str, size_t len,
                                   size_t* value_len);

#ifdef __cplusplus
}
#endif
#endif