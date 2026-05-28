#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size);
    
    size_t print(const String &s);
    size_t print(const char str[]);
    size_t print(char c);
    size_t print(unsigned char b, int base = DEC);
    size_t print(int n, int base = DEC);
    size_t print(unsigned int n, int base = DEC);
    size_t print(long n, int base = DEC);
    size_t print(unsigned long n, int base = DEC);
    size_t print(double n, int digits = 2);

    size_t println(const String &s);
    size_t println(const char c[]);
    size_t println(char c);
    size_t println(unsigned char b, int base = DEC);
    size_t println(int num, int base = DEC);
    size_t println(unsigned int num, int base = DEC);
    size_t println(long num, int base = DEC);
    size_t println(unsigned long num, int base = DEC);
    size_t println(double num, int digits = 2);
    size_t println(void);

    size_t printf(const char * format, ...);
};

class Printable {
public:
    virtual size_t printTo(Print& p) const = 0;
};
