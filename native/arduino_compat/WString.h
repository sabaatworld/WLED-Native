#pragma once
#include <string>
#include <vector>

class String : public std::string {
public:
  String() : std::string() {}
  String(const char* cstr) : std::string(cstr ? cstr : "") {}
  String(const std::string& str) : std::string(str) {}
  String(int val, unsigned char base = 10);
  String(unsigned int val, unsigned char base = 10);
  String(long val, unsigned char base = 10);
  String(unsigned long val, unsigned char base = 10);
  String(float val, unsigned char decimalPlaces = 2);
  String(double val, unsigned char decimalPlaces = 2);
  
  bool equals(const String& s) const { return *this == s; }
  void reserve(size_t size) { std::string::reserve(size); }
  void replace(const String& find, const String& replaceWith) {
      size_t pos = 0;
      while ((pos = find_first_of(find, pos)) != std::string::npos) {
          std::string::replace(pos, find.length(), replaceWith);
          pos += replaceWith.length();
      }
  }
  
  // Implicit conversions to std::string types are defined by base class std::string
  operator const char*() const { return c_str(); }
};
