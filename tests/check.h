#ifndef T80_TEST_CHECK_H
#define T80_TEST_CHECK_H
/* assert() replacement for tests that also run as 6502 code. It prints the
 * failing line, and its argument is a plain 0/1 value: Oscar64 1.32.273
 * -O2 can miscompile an expression inlined straight into assert(bool),
 * e.g. assert(!f()) where f() returns NULL compared the result with a
 * stale register instead of 0. */
#include <assert.h>
#include <stdio.h>
#ifdef __OSCAR64C__
__noinline
#endif
static void check_line(int ok, int line)
{
    if (!ok) printf("FAIL line %d\n", line);
    assert(ok);
}
#undef assert
#define assert(c) check_line((c) ? 1 : 0, __LINE__)
#endif
