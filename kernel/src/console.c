#define UART0 ((volatile unsigned int*)0x09000000)

void kputc(char c)
{
    *UART0 = (unsigned int)c;
}

void kputs(const char* s)
{
    while (*s) {
        kputc(*s++);
    }
}
