#pragma once

#ifndef WLED_NATIVE
#define WLED_NATIVE 1
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <algorithm>

typedef uint8_t byte;
typedef bool boolean;

#define IRAM_ATTR
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define FPSTR(s) (s)

#ifndef min
#define min(a,b) std::min(a,b)
#endif
#ifndef max
#define max(a,b) std::max(a,b)
#endif

#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "IPAddress.h"

void delay(uint32_t ms);
void yield();
uint32_t millis();
uint32_t micros();
long random(long howsmall, long howbig);
long random(long howbig);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

template <typename T>
const T& constrain(const T& x, const T& a, const T& b) {
  if (x < a) return a;
  if (b < x) return b;
  return x;
}

void* d_malloc(size_t size);
void* p_malloc(size_t size);
void p_free(void* ptr);
void* p_realloc(void* ptr, size_t size);
uint32_t getFreeHeap();
uint32_t getFreePsram();
bool psramFound();

