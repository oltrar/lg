/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common/debug.h"
#include <stdarg.h>
void debug_level(enum DebugLevel level, const char * file, unsigned int line,
  const char * function, const char * format, ...)
{
  (void)level; (void)file; (void)line; (void)function;
  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  fputc('\n', stderr);
  va_end(arguments);
}
