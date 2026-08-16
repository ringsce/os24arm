/**
 * @file string.c
 * @brief String and Memory Functions Implementation
 */

#include "types.h"

/* String length */
size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

size_t kstrlen(const char *s)
{
    return strlen(s);
}

/* String compare */
int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int kstrcmp(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* String compare (n bytes) */
int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    return n ? (*(unsigned char*)a - *(unsigned char*)b) : 0;
}

/* Case-insensitive string compare (n bytes) */
int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n)
{
    return strncmp(a, b, n);
}

/* String copy */
char* strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

char* kstrcpy(char *dst, const char *src)
{
    return strcpy(dst, src);
}

/* String copy (n bytes) */
char* strncpy(char *dst, const char *src, size_t n)
{
    char *ret = dst;
    while (n && (*dst = *src)) {
        dst++;
        src++;
        n--;
    }
    while (n--) *dst++ = '\0';
    return ret;
}

char* kstrncpy(char *dst, const char *src, size_t n)
{
    return strncpy(dst, src, n);
}

/* Memory copy */
void* memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* kmemcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

/* Memory set */
void* memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void* kmemset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

/* Memory compare */
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;

    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

/* Additional functions */
char* strcat(char *dst, const char *src)
{
    char *ret = dst;
    while (*dst) dst++;
    while ((*dst++ = *src++));
    return ret;
}

char* strchr(const char *str, int c)
{
    while (*str) {
        if (*str == (char)c) return (char *)str;
        str++;
    }
    return (c == '\0') ? (char *)str : NULL;
}

void* memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void* memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;

    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}