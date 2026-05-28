#include "../arduino_compat/Arduino.h"
#include <iostream>
#include <cassert>

class StringPrint : public Print {
public:
    std::string str;
    size_t write(uint8_t c) override {
        str += (char)c;
        return 1;
    }
};

void test_string() {
    String s1 = "Hello";
    assert(s1 == "Hello");
    String s2(1234);
    assert(s2 == "1234");
    String s3(255, HEX);
    assert(s3 == "ff" || s3 == "FF"); // Either case is fine for now
    std::cout << "String tests passed." << std::endl;
}

void test_ipaddress() {
    IPAddress ip(192, 168, 1, 100);
    assert(ip[0] == 192);
    assert(ip[3] == 100);
    assert(ip.toString() == "192.168.1.100");
    
    IPAddress ip2;
    ip2.fromString("10.0.0.1");
    assert(ip2[0] == 10);
    assert(ip2[3] == 1);
    
    StringPrint sp;
    ip.printTo(sp);
    assert(sp.str == "192.168.1.100");
    
    std::cout << "IPAddress tests passed." << std::endl;
}

void test_print() {
    StringPrint sp;
    sp.print("Hello");
    assert(sp.str == "Hello");
    sp.str = "";
    sp.print(123);
    assert(sp.str == "123");
    sp.str = "";
    sp.print(255, HEX);
    assert(sp.str == "FF" || sp.str == "ff"); // Either case
    std::cout << "Print tests passed." << std::endl;
}

void test_math() {
    assert(map(50, 0, 100, 0, 200) == 100);
    assert(constrain(150, 0, 100) == 100);
    assert(constrain(50, 0, 100) == 50);
    assert(constrain(-10, 0, 100) == 0);
    std::cout << "Math tests passed." << std::endl;
}

int main() {
    std::cout << "Running arduino_compat tests..." << std::endl;
    test_string();
    test_ipaddress();
    test_print();
    test_math();
    std::cout << "All arduino_compat tests passed!" << std::endl;
    return 0;
}
