/* runtime/selftest: the always-on start-up checks (RX_SELF_TEST, mostly structure layouts that
 * must match the original's on-disk / in-memory formats) and their reporting on the UART.
 * RX_SELF_LATE_TEST runs once from OpenGamepads, after the app's startup (heap usable). */
#include "types.h"

__attribute__((used, section(".rxtest$a"))) static const struct rx_test tests_begin[1];
__attribute__((used, section(".rxtest$z"))) static const struct rx_test tests_end[1];
__attribute__((used, section(".rxltst$a"))) static const struct rx_test ltests_begin[1];
__attribute__((used, section(".rxltst$z"))) static const struct rx_test ltests_end[1];

static const char *current_test;
static int n_pass, n_fail;

int rx_streq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

void rx_check(int ok, const char *what)
{
    if (ok)
        n_pass++;
    else
        n_fail++;
    uart_puts(ok ? "[test] PASS " : "[test] FAIL ");
    uart_puts(current_test);
    uart_puts(": ");
    uart_puts(what);
    uart_puts("\r\n");
}

static void run(const struct rx_test *b, const struct rx_test *e, const char *tag)
{
    n_pass = n_fail = 0;
    for (const struct rx_test *t = b + 1; t < e; t++) {
        if (!t->fn)
            continue;
        current_test = t->name;
        t->fn();
    }
    uart_puts(tag);
    uart_hex((u32)n_pass);
    uart_puts(" passed, ");
    uart_hex((u32)n_fail);
    uart_puts(" failed\r\n");
}

void rx_run_tests(void)
{
    run(tests_begin, tests_end, "[test] ");
}

void rx_run_late_tests(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    run(ltests_begin, ltests_end, "[late] ");
}
