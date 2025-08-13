#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <unordered_map>
#include <memory>
#include "MemoryBlock.h"
// Representation for a mapped image
struct PEImage {
    std::string path;
    uint32_t preferredImageBase = 0;
    uint32_t actualImageBase = 0;
    uint32_t sizeOfImage = 0;
    struct BlockInfo { uint32_t vaddr; uint32_t size; MemoryBlock* block; };
    std::vector<BlockInfo> blocks;
    // exports: name -> VA
    std::map<std::string, uint32_t> exportsByName;
    // exports: ordinal -> VA
    std::unordered_map<uint32_t, uint32_t> exportsByOrdinal;
};

ErrorCode LoadPEImage(const std::string& path, PEImage& outImg, const std::string& systemDir);

std::shared_ptr<PEImage> GetPEImageByHandle(uint32_t handle);

inline std::string ToLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

inline bool ReadFileToVector(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;
    std::streamoff size = ifs.tellg();
    if (size <= 0) return false;
    out.resize((size_t)size);
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(out.data()), out.size());
    return ifs.good();
}