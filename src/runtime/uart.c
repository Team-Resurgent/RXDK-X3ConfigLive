/* COM1 UART output (LPC47M157 on xemu / debug boards). Titles run in ring 0. */
#include "types.h"

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

void uart_putc(char c)
{
    while (!(inb(0x3FD) & 0x20)) {
    }
    outb(0x3F8, (u8)c);
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_hex(u32 v)
{
    static const char hx[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        uart_putc(hx[(v >> i) & 15]);
}
