#include "WString.h"
#include <sstream>
#include <iomanip>

String::String(int val, unsigned char base) : std::string() {
    std::stringstream ss;
    if (base == 16) ss << std::hex;
    else if (base == 8) ss << std::oct;
    ss << val;
    assign(ss.str());
}

String::String(unsigned int val, unsigned char base) : std::string() {
    std::stringstream ss;
    if (base == 16) ss << std::hex;
    else if (base == 8) ss << std::oct;
    ss << val;
    assign(ss.str());
}

String::String(long val, unsigned char base) : std::string() {
    std::stringstream ss;
    if (base == 16) ss << std::hex;
    else if (base == 8) ss << std::oct;
    ss << val;
    assign(ss.str());
}

String::String(unsigned long val, unsigned char base) : std::string() {
    std::stringstream ss;
    if (base == 16) ss << std::hex;
    else if (base == 8) ss << std::oct;
    ss << val;
    assign(ss.str());
}

String::String(float val, unsigned char decimalPlaces) : std::string() {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimalPlaces) << val;
    assign(ss.str());
}

String::String(double val, unsigned char decimalPlaces) : std::string() {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimalPlaces) << val;
    assign(ss.str());
}
