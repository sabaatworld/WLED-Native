#include "IPAddress.h"
#include <stdio.h>

IPAddress::IPAddress() {
    _address[0] = 0;
    _address[1] = 0;
    _address[2] = 0;
    _address[3] = 0;
}

IPAddress::IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet) {
    _address[0] = first_octet;
    _address[1] = second_octet;
    _address[2] = third_octet;
    _address[3] = fourth_octet;
}

IPAddress::IPAddress(uint32_t address) {
    _address[0] = (uint8_t)(address & 0xFF);
    _address[1] = (uint8_t)((address >> 8) & 0xFF);
    _address[2] = (uint8_t)((address >> 16) & 0xFF);
    _address[3] = (uint8_t)((address >> 24) & 0xFF);
}

IPAddress::IPAddress(const uint8_t *address) {
    _address[0] = address[0];
    _address[1] = address[1];
    _address[2] = address[2];
    _address[3] = address[3];
}

bool IPAddress::fromString(const char *address) {
    uint16_t a, b, c, d;
    if (sscanf(address, "%hu.%hu.%hu.%hu", &a, &b, &c, &d) == 4) {
        if (a < 256 && b < 256 && c < 256 && d < 256) {
            _address[0] = a;
            _address[1] = b;
            _address[2] = c;
            _address[3] = d;
            return true;
        }
    }
    return false;
}

IPAddress::operator uint32_t() const {
    return *((uint32_t*)_address);
}

bool IPAddress::operator==(const IPAddress& addr) const {
    return *((uint32_t*)_address) == *((uint32_t*)addr._address);
}

bool IPAddress::operator==(const uint8_t* addr) const {
    return _address[0] == addr[0] && _address[1] == addr[1] && _address[2] == addr[2] && _address[3] == addr[3];
}

uint8_t IPAddress::operator[](int index) const {
    return _address[index];
}

uint8_t& IPAddress::operator[](int index) {
    return _address[index];
}

size_t IPAddress::printTo(Print& p) const {
    size_t n = 0;
    for (int i = 0; i < 3; i++) {
        n += p.print(_address[i], DEC);
        n += p.print('.');
    }
    n += p.print(_address[3], DEC);
    return n;
}

String IPAddress::toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", _address[0], _address[1], _address[2], _address[3]);
    return String(buf);
}
