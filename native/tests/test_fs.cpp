#include "../fs/LittleFS.h"
#include <iostream>
#include <cassert>
#include <filesystem>

void test_fs() {
    std::string test_dir = "test_config_dir";
    std::filesystem::remove_all(test_dir);
    
    WLED_FS.begin(test_dir.c_str());
    
    // Test write and read
    File f = WLED_FS.open("/test.txt", "w");
    assert(f);
    f.print("Hello WLED Native");
    f.close();
    
    assert(WLED_FS.exists("/test.txt"));
    
    File f2 = WLED_FS.open("/test.txt", "r");
    assert(f2);
    assert(f2.size() == 17);
    String content = f2.readString();
    assert(content == "Hello WLED Native");
    f2.close();
    
    // Test directory
    WLED_FS.open("/dir/file.txt", "w").close();
    Dir d = WLED_FS.openDir("/dir");
    bool found = false;
    while(d.next()) {
        if (d.fileName() == "file.txt") {
            found = true;
            assert(d.isFile());
        }
    }
    assert(found);
    
    // Clean up
    std::filesystem::remove_all(test_dir);
    std::cout << "FS tests passed." << std::endl;
}

int main() {
    std::cout << "Running FS tests..." << std::endl;
    test_fs();
    std::cout << "All FS tests passed!" << std::endl;
    return 0;
}
