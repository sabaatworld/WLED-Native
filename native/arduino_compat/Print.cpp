#include "Print.h"
#include <stdio.h>
#include <stdarg.h>

static size_t printNumber(unsigned long n, uint8_t base, Print* p) {
  char buf[8 * sizeof(long) + 1];
  char *str = &buf[sizeof(buf) - 1];

  *str = '\0';

  if (base < 2) base = 10;

  do {
    char c = n % base;
    n /= base;

    *--str = c < 10 ? c + '0' : c + 'A' - 10;
  } while(n);

  return p->write((const uint8_t*)str, strlen(str));
}

size_t Print::write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--) {
        n += write(*buffer++);
    }
    return n;
}

size_t Print::print(const String &s) {
    return write((const uint8_t *)s.c_str(), s.length());
}

size_t Print::print(const char str[]) {
    return write((const uint8_t *)str, strlen(str));
}

size_t Print::print(char c) {
    return write((uint8_t)c);
}

size_t Print::print(unsigned char b, int base) {
    return print((unsigned long)b, base);
}

size_t Print::print(int n, int base) {
    return print((long)n, base);
}

size_t Print::print(unsigned int n, int base) {
    return print((unsigned long)n, base);
}

size_t Print::print(long n, int base) {
    if (base == 0) {
        return write((uint8_t)n);
    } else if (base == 10) {
        if (n < 0) {
            int t = print('-');
            n = -n;
            return printNumber(n, 10, this) + t;
        }
        return printNumber(n, 10, this);
    } else {
        return printNumber(n, base, this);
    }
}

size_t Print::print(unsigned long n, int base) {
    if (base == 0) return write((uint8_t)n);
    else return printNumber(n, base, this);
}

size_t Print::print(double n, int digits) {
    return print(String(n, digits));
}

size_t Print::println(const String &s) {
    size_t n = print(s);
    n += println();
    return n;
}

size_t Print::println(const char c[]) {
    size_t n = print(c);
    n += println();
    return n;
}

size_t Print::println(char c) {
    size_t n = print(c);
    n += println();
    return n;
}

size_t Print::println(unsigned char b, int base) {
    size_t n = print(b, base);
    n += println();
    return n;
}

size_t Print::println(int num, int base) {
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(unsigned int num, int base) {
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(long num, int base) {
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(unsigned long num, int base) {
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(double num, int digits) {
    size_t n = print(num, digits);
    n += println();
    return n;
}

size_t Print::println(void) {
    return print("\r\n");
}

size_t Print::printf(const char * format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len > 0) {
        return write((const uint8_t*)buf, len);
    }
    return 0;
}
