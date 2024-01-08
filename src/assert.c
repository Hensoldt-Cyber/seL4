/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <assert.h>
#include <machine/io.h>

#ifdef CONFIG_DEBUG_BUILD

void _fail(
    const char  *file,
    unsigned int line,
    const char  *function,
    const char  *s,
    ...)
{
    va_list args;
    printf("seL4 called fail at %s:%u in function %s, saying \"",
           file, line, function);
    va_start(args, s);
    vprintf(s, args);
    va_end(args);
    printf("\"\n");
    halt();
}

void _assert_fail(
    const char  *assertion,
    const char  *file,
    unsigned int line,
    const char  *function)
{
    printf("seL4 failed assertion '%s' at %s:%u in function %s\n",
           assertion,
           file,
           line,
           function
          );
    halt();
}

#endif
