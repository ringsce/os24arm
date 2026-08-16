#include "string.h"

size_t strlen(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b)
{
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    while (n && *a && (*a == *b)) {
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = dst;
    const unsigned char* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, size_t n)
{
    unsigned char* d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}


char* strcpy(char* dst, const char* src)
{
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];

    for (; i < n; i++)
        dst[i] = 0;

    return dst;
}
