/**
* @file kprintf.h
 * @brief Kernel printf function
 */

#ifndef KPRINTF_H
#define KPRINTF_H

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* KPRINTF_H */