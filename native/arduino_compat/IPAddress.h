#pragma once
#include <stdint.h>
#include <string>
#include "Print.h"
#include "WString.h"

class IPAddress : public Printable {
private:
    uint8_t _address[4];
public:
    IPAddress();
    IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet);
    IPAddress(uint32_t address);
    IPAddress(const uint8_t *address);
    
    bool fromString(const char *address);
    bool fromString(const String &address) { return fromString(address.c_str()); }
    
    operator uint32_t() const;
    bool operator==(const IPAddress& addr) const;
    bool operator==(const uint8_t* addr) const;
    uint8_t operator[](int index) const;
    uint8_t& operator[](int index);

    size_t printTo(Print& p) const override;
    String toString() const;
};
