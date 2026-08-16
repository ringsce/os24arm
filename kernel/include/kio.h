#ifndef KIO_H
#define KIO_H

#include "types.h"

/* ANSI colour codes */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_WHITE   "\x1b[37m"

void    kprintf(const char *fmt, ...);
void    kgets(char *buf, size_t len);   /* size_t matches kio.c */

int     kstrcmp(const char *a, const char *b);
int     kstrncmp(const char *a, const char *b, size_t n);
char   *kstrcpy(char *dst, const char *src);
char   *kstrncpy(char *dst, const char *src, size_t n);
char   *kstrchr(const char *s, int c);
size_t  kstrlen(const char *s);
int32_t katoi(const char *s);

void   *kmemset(void *dst, int c, size_t n);
void   *kmemcpy(void *dst, const void *src, size_t n);

#endif /* KIO_H */
