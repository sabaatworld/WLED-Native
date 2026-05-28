#pragma once
#include <string>
#include <vector>
#include "Arduino.h"
#include <filesystem>

class File : public Stream {
private:
    std::string _path;
    FILE* _f;
    std::string _mode;
public:
    File(const char* path, const char* mode);
    ~File();
    
    // Disable copy
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    // Enable move
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    
    bool isDirectory() const;
    String name() const;
    size_t size() const;
    void close();
    operator bool() const { return _f != nullptr; }
    
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    
    bool seek(uint32_t pos);
    uint32_t position() const;
    
    int read(uint8_t* buf, size_t size);
    String readString();
};

class Dir {
private:
    std::filesystem::path _path;
    std::filesystem::directory_iterator _it;
    bool _valid;
public:
    Dir(const std::string& path);
    bool next();
    String fileName();
    size_t fileSize();
    bool isFile();
    bool isDirectory();
};

class FS {
private:
    std::string _basePath;
    std::string resolvePath(const char* path) const;
public:
    void begin(const char* basePath);
    File open(const char* path, const char* mode = "r");
    File open(const String& path, const char* mode = "r") { return open(path.c_str(), mode); }
    bool exists(const char* path);
    bool exists(const String& path) { return exists(path.c_str()); }
    bool remove(const char* path);
    bool remove(const String& path) { return remove(path.c_str()); }
    bool rename(const char* pathFrom, const char* pathTo);
    bool rename(const String& pathFrom, const String& pathTo) { return rename(pathFrom.c_str(), pathTo.c_str()); }
    Dir openDir(const char* path);
    Dir openDir(const String& path) { return openDir(path.c_str()); }
    
    String getBasePath() const { return _basePath.c_str(); }
};

extern FS WLED_FS;

// Define LittleFS as a reference to WLED_FS instead of namespace alias
#define LittleFS WLED_FS

