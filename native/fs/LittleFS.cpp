#include "LittleFS.h"
#include <iostream>

FS WLED_FS;

// File implementation
File::File(const char* path, const char* mode) : _path(path), _mode(mode) {
    // Determine the stdio mode based on the Arduino mode
    // "r", "w", "a", "r+", "w+", "a+"
    _f = fopen(path, mode);
}

File::~File() {
    close();
}

File::File(File&& other) noexcept : _path(std::move(other._path)), _f(other._f), _mode(std::move(other._mode)) {
    other._f = nullptr;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        _path = std::move(other._path);
        _f = other._f;
        _mode = std::move(other._mode);
        other._f = nullptr;
    }
    return *this;
}

bool File::isDirectory() const {
    std::error_code ec;
    return std::filesystem::is_directory(_path, ec);
}

String File::name() const {
    std::filesystem::path p(_path);
    std::string filename = p.filename().string();
    return String(filename.c_str());
}

size_t File::size() const {
    if (!_f) return 0;
    long current = ftell(_f);
    fseek(_f, 0, SEEK_END);
    long size = ftell(_f);
    fseek(_f, current, SEEK_SET);
    return size;
}

void File::close() {
    if (_f) {
        fclose(_f);
        _f = nullptr;
    }
}

int File::available() {
    if (!_f) return 0;
    long current = ftell(_f);
    fseek(_f, 0, SEEK_END);
    long size = ftell(_f);
    fseek(_f, current, SEEK_SET);
    return size - current;
}

int File::read() {
    if (!_f) return -1;
    uint8_t c;
    if (fread(&c, 1, 1, _f) == 1) return c;
    return -1;
}

int File::peek() {
    if (!_f) return -1;
    long current = ftell(_f);
    uint8_t c;
    if (fread(&c, 1, 1, _f) == 1) {
        fseek(_f, current, SEEK_SET);
        return c;
    }
    return -1;
}

void File::flush() {
    if (_f) fflush(_f);
}

size_t File::write(uint8_t c) {
    if (!_f) return 0;
    return fwrite(&c, 1, 1, _f);
}

size_t File::write(const uint8_t *buf, size_t size) {
    if (!_f) return 0;
    return fwrite(buf, 1, size, _f);
}

bool File::seek(uint32_t pos) {
    if (!_f) return false;
    return fseek(_f, pos, SEEK_SET) == 0;
}

uint32_t File::position() const {
    if (!_f) return 0;
    return ftell(_f);
}

int File::read(uint8_t* buf, size_t size) {
    if (!_f) return 0;
    return fread(buf, 1, size, _f);
}

String File::readString() {
    String ret;
    int avail;
    while ((avail = available()) > 0) {
        int to_read = avail < 1024 ? avail : 1024;
        std::vector<char> buf(to_read);
        int read_bytes = read((uint8_t*)buf.data(), buf.size());
        if (read_bytes > 0) {
            ret.append(buf.data(), read_bytes);
        }
    }
    return ret;
}

// Dir implementation
Dir::Dir(const std::string& path) : _path(path), _valid(false) {
    std::error_code ec;
    if (std::filesystem::exists(_path, ec) && std::filesystem::is_directory(_path, ec)) {
        _it = std::filesystem::directory_iterator(_path, ec);
        _valid = (_it != std::filesystem::directory_iterator());
    }
}

bool Dir::next() {
    if (!std::filesystem::exists(_path)) return false;
    if (!_valid) return false; // Already reached the end previously
    
    // The iterator points to the current item. To advance to the next, we increment it.
    // Notice that if this is the first call to next(), we don't want to advance if we have just initialized it.
    // Arduino's Dir::next() moves to the next file and returns true if there is one.
    // Here we need to skip the first increment.
    
    static bool first = true;
    if (first) {
        first = false;
        return _valid;
    }
    
    std::error_code ec;
    _it.increment(ec);
    
    _valid = (_it != std::filesystem::directory_iterator());
    return _valid;
}

String Dir::fileName() {
    if (!_valid) return String("");
    return String(_it->path().filename().string().c_str());
}

size_t Dir::fileSize() {
    if (!_valid) return 0;
    std::error_code ec;
    if (_it->is_regular_file(ec)) {
        return std::filesystem::file_size(_it->path(), ec);
    }
    return 0;
}

bool Dir::isFile() {
    if (!_valid) return false;
    std::error_code ec;
    return _it->is_regular_file(ec);
}

bool Dir::isDirectory() {
    if (!_valid) return false;
    std::error_code ec;
    return _it->is_directory(ec);
}

// FS implementation
std::string FS::resolvePath(const char* path) const {
    if (path == nullptr) return _basePath;
    
    std::string p(path);
    // Remove leading slash to not treat it as an absolute path from root
    if (!p.empty() && p[0] == '/') p = p.substr(1);
    
    std::filesystem::path fp(_basePath);
    fp /= p;
    return fp.string();
}

void FS::begin(const char* basePath) {
    _basePath = basePath;
    std::error_code ec;
    std::filesystem::create_directories(_basePath, ec);
}

File FS::open(const char* path, const char* mode) {
    std::string full_path = resolvePath(path);
    
    // Create parent directories if opening for write
    if (strchr(mode, 'w') || strchr(mode, 'a')) {
        std::error_code ec;
        std::filesystem::path fp(full_path);
        if (fp.has_parent_path()) {
            std::filesystem::create_directories(fp.parent_path(), ec);
        }
    }
    
    return File(full_path.c_str(), mode);
}

bool FS::exists(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(resolvePath(path), ec);
}

bool FS::remove(const char* path) {
    std::error_code ec;
    return std::filesystem::remove(resolvePath(path), ec);
}

bool FS::rename(const char* pathFrom, const char* pathTo) {
    std::error_code ec;
    std::filesystem::rename(resolvePath(pathFrom), resolvePath(pathTo), ec);
    return !ec;
}

Dir FS::openDir(const char* path) {
    return Dir(resolvePath(path));
}
