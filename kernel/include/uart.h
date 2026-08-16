#ifndef UART_H
#define UART_H

#include "types.h"

void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int  uart_ready(void);

/* Called from boot.S's exception vectors; uses only uart_putc/uart_puts. */
void report_sync_exception(unsigned long esr, unsigned long elr, unsigned long far);

#endif /* UART_H */
