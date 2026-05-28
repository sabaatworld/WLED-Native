#include "Arduino.h"
#include <chrono>
#include <thread>
#include <cstdlib>

using namespace std::chrono;

static auto start_time = steady_clock::now();

void delay(uint32_t ms) {
    std::this_thread::sleep_for(milliseconds(ms));
}

void yield() {
    std::this_thread::yield();
}

uint32_t millis() {
    auto now = steady_clock::now();
    return duration_cast<milliseconds>(now - start_time).count();
}

uint32_t micros() {
    auto now = steady_clock::now();
    return duration_cast<microseconds>(now - start_time).count();
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    return howsmall + (std::rand() % (howbig - howsmall));
}

long random(long howbig) {
    return random(0, howbig);
}

void randomSeed(unsigned long seed) {
    std::srand(seed);
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_min == in_max) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Memory helpers
void* d_malloc(size_t size) {
    return malloc(size);
}

void* p_malloc(size_t size) {
    return malloc(size);
}

void p_free(void* ptr) {
    free(ptr);
}

void* p_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

uint32_t getFreeHeap() {
    return 1024 * 1024 * 100; // 100MB dummy for native
}

uint32_t getFreePsram() {
    return 1024 * 1024 * 100; // 100MB dummy
}

bool psramFound() {
    return true;
}

