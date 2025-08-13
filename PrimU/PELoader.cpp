// PE Loader for ARM (PE32) with: 
//  - import by ordinal support
//  - Thumb-aware stubs (unresolved imports get Thumb stubs by default)
//  - fixed redirection/recursion logic for loading dependencies
//  - does not assume MemoryBlock::GetVAddr(); it records vaddrs when allocating
// 
// Drop this file into your project and adapt small bits if your MemoryManager/MemoryBlock
// API names differ (notes are included where adaptation may be required).
#include "common.h"
#include "MemoryManager.h"
#include "PELoader.h"
#include <windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
namespace fs = std::filesystem;

//
//static std::string ToLower(const std::string& s) {
//    std::string r = s;
//    for (char& c : r) c = (char)std::tolower((unsigned char)c);
//    return r;
//}
//
//static bool ReadFileToVector(const std::string& path, std::vector<uint8_t>& out) {
//    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
//    if (!ifs.is_open()) return false;
//    std::streamoff size = ifs.tellg();
//    if (size <= 0) return false;
//    out.resize((size_t)size);
//    ifs.seekg(0, std::ios::beg);
//    ifs.read(reinterpret_cast<char*>(out.data()), out.size());
//    return ifs.good();
//}



static PEImage::BlockInfo* FindBlockForVA(PEImage& img, uint32_t va) {
    for (auto& b : img.blocks) {
        if (va >= b.vaddr && va < b.vaddr + b.size) return &b;
    }
    return nullptr;
}

static void* HostPtrForVA(PEImage& img, uint32_t va) {
    auto* bi = FindBlockForVA(img, va);
    if (!bi) return nullptr;
    uint32_t off = va - bi->vaddr;
    return reinterpret_cast<void*>((uintptr_t)bi->block->GetRAddr() + off);
}

static bool WriteU32AtVA(PEImage& img, uint32_t va, uint32_t val) {
    void* p = HostPtrForVA(img, va);
    if (!p) return false;
    *(uint32_t*)p = val;
    return true;
}
static bool ReadU32AtVA(PEImage& img, uint32_t va, uint32_t& outVal) {
    void* p = HostPtrForVA(img, va);
    if (!p) return false;
    outVal = *(uint32_t*)p;
    return true;
}

// Try to allocate any free region for a block of given size. Returns ERROR_OK and sets outBlock and outVAddr.
static ErrorCode AllocateAny(uint32_t size, MemoryBlock** outBlock, uint32_t& outVAddr) {
    const uint32_t START = 0x10000000;
    const uint32_t END = 0x70000000;
    const uint32_t STEP = 0x00100000; // 1MB steps
    for (uint32_t a = START; a < END; a += STEP) {
        MemoryBlock* mb = nullptr;
        ErrorCode err = sMemoryManager->StaticAlloc(a, size, &mb);
        if (err == ERROR_OK && mb) {
            *outBlock = mb;
            outVAddr = a;
            return ERROR_OK;
        }
    }
    return ERROR_GENERIC;
}

// Create a stub that returns 0 in r0 and BX lr. Supports ARM or Thumb form.
// If thumb==true, the returned VA will have LSB=1 to indicate Thumb mode (typical calling convention for ARM on Windows).
static uint32_t CreateReturnZeroStub(PEImage& img, bool thumb) {
    const uint32_t ARM_STUB_SIZE = 8;   // two 32-bit ARM instr
    const uint32_t THUMB_STUB_SIZE = 4; // two 16-bit Thumb instr

    MemoryBlock* mb = nullptr;
    uint32_t chosenVaddr = 0;
    uint32_t allocSize = thumb ? THUMB_STUB_SIZE : ARM_STUB_SIZE;
    if (AllocateAny(allocSize, &mb, chosenVaddr) != ERROR_OK) return 0;

    // write stub
    if (thumb) {
        // Thumb little-endian: MOVS r0,#0 -> 0x2000 ; BX lr -> 0x4770
        uint8_t bytes[4] = { 0x00, 0x20, 0x70, 0x47 };
        void* dest = mb->GetRAddr();
        memcpy(dest, bytes, sizeof(bytes));
        img.blocks.push_back({ chosenVaddr, allocSize, mb });
        // indicate Thumb by setting LSB
        return chosenVaddr | 1u;
    }
    else {
        uint32_t instr1 = 0xE3A00000u; // MOV r0, #0
        uint32_t instr2 = 0xE12FFF1Eu; // BX lr
        void* dest = mb->GetRAddr();
        memcpy(dest, &instr1, 4);
        memcpy((uint8_t*)dest + 4, &instr2, 4);
        img.blocks.push_back({ chosenVaddr, allocSize, mb });
        return chosenVaddr; // ARM uses even VA
    }
}

static ErrorCode ApplyRelocations(PEImage& img, const std::vector<uint8_t>& fileBuf, const IMAGE_NT_HEADERS32* nt, int64_t delta) {
    if (delta == 0) return ERROR_OK;
    const IMAGE_DATA_DIRECTORY& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relocDir.Size == 0) return ERROR_OK;

    uint32_t pos = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    uint32_t end = pos + relocDir.Size;
    uint32_t cur = pos;
    while (cur < end) {
        uint32_t blockVA = img.actualImageBase + cur;
        IMAGE_BASE_RELOCATION* brel = reinterpret_cast<IMAGE_BASE_RELOCATION*>(HostPtrForVA(img, blockVA));
        if (!brel) return ERROR_LOADER_READER_FAIL;
        uint32_t blockSize = brel->SizeOfBlock;
        if (blockSize < sizeof(IMAGE_BASE_RELOCATION)) return ERROR_LOADER_READER_FAIL;
        uint32_t entryCount = (blockSize - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        WORD* entries = (WORD*)((uint8_t*)brel + sizeof(IMAGE_BASE_RELOCATION));
        uint32_t pageRVA = brel->VirtualAddress;
        for (uint32_t i = 0; i < entryCount; ++i) {
            WORD e = entries[i];
            WORD type = e >> 12;
            WORD offset = e & 0x0FFF;
            if (type == IMAGE_REL_BASED_HIGHLOW) {
                uint32_t relocAddrVA = (uint32_t)(img.actualImageBase + pageRVA + offset);
                uint32_t orig = 0;
                if (!ReadU32AtVA(img, relocAddrVA, orig)) return ERROR_LOADER_READER_FAIL;
                uint32_t patched = (uint32_t)((int64_t)orig + delta);
                if (!WriteU32AtVA(img, relocAddrVA, patched)) return ERROR_LOADER_READER_FAIL;
            }
        }
        cur += blockSize;
    }
    return ERROR_OK;
}

static void ParseAndFillExports(PEImage& img, const std::vector<uint8_t>& fileBuf, const IMAGE_NT_HEADERS32* nt) {
    const IMAGE_DATA_DIRECTORY& expDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (expDir.Size == 0) return;
    uint32_t expRVA = expDir.VirtualAddress;
    IMAGE_EXPORT_DIRECTORY* expPtr = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(HostPtrForVA(img, img.actualImageBase + expRVA));
    if (!expPtr) return;
    uint32_t base = expPtr->Base; // ordinal base
    uint32_t* addrOfFunctions = reinterpret_cast<uint32_t*>(HostPtrForVA(img, img.actualImageBase + expPtr->AddressOfFunctions));
    uint32_t* addrOfNameRVAs = reinterpret_cast<uint32_t*>(HostPtrForVA(img, img.actualImageBase + expPtr->AddressOfNames));
    uint16_t* addrOfNameOrdinals = reinterpret_cast<uint16_t*>(HostPtrForVA(img, img.actualImageBase + expPtr->AddressOfNameOrdinals));
    if (addrOfFunctions) {
        // fill ordinal map for all exported functions
        for (uint32_t i = 0; i < expPtr->NumberOfFunctions; ++i) {
            uint32_t funcRVA = addrOfFunctions[i];
            uint32_t funcVA = img.actualImageBase + funcRVA;
            uint32_t ord = base + i;
            img.exportsByOrdinal[ord] = funcVA;
        }
    }
    if (addrOfNameRVAs && addrOfNameOrdinals) {
        for (uint32_t i = 0; i < expPtr->NumberOfNames; ++i) {
            uint32_t nameRVA = addrOfNameRVAs[i];
            char* namePtr = reinterpret_cast<char*>(HostPtrForVA(img, img.actualImageBase + nameRVA));
            if (!namePtr) continue;
            uint16_t ordIndex = addrOfNameOrdinals[i];
            uint32_t funcRVA = addrOfFunctions[ordIndex];
            uint32_t funcVA = img.actualImageBase + funcRVA;
            img.exportsByName[std::string(namePtr)] = funcVA;
        }
    }
}

// Forward decl
static ErrorCode MapPEIntoMemory(const std::vector<uint8_t>& fileBuf, const std::string& path,
    PEImage& outImg, std::map<std::string, std::shared_ptr<PEImage>>& loaded,
    const std::string& systemDir);

static ErrorCode ResolveImports(PEImage& img, const std::vector<uint8_t>& fileBuf,
    std::map<std::string, std::shared_ptr<PEImage>>& loaded,
    const std::string& systemDir)
{
    if (fileBuf.size() < sizeof(IMAGE_DOS_HEADER)) return ERROR_LOADER_READER_FAIL;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fileBuf.data());
    auto nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(fileBuf.data() + dos->e_lfanew);

    const IMAGE_DATA_DIRECTORY& importDir = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.Size == 0) return ERROR_OK;

    uint32_t importRVA = importDir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR* impDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(HostPtrForVA(img, img.actualImageBase + importRVA));
    if (!impDesc) return ERROR_LOADER_READER_FAIL;

    for (; impDesc->Name != 0; ++impDesc) {
        char* dllNamePtr = reinterpret_cast<char*>(HostPtrForVA(img, img.actualImageBase + impDesc->Name));
        if (!dllNamePtr) return ERROR_LOADER_READER_FAIL;
        std::string dllName = ToLower(std::string(dllNamePtr));
        size_t pos = dllName.find_last_of("\\/");
        if (pos != std::string::npos) dllName = dllName.substr(pos + 1);

        std::shared_ptr<PEImage> dep;
        if (loaded.count(dllName) && loaded[dllName]) {
            dep = loaded[dllName];
        }
        else {
            // insert placeholder shared_ptr immediately to handle circular deps
            auto depImg = std::make_shared<PEImage>();
            loaded[dllName] = depImg; // placeholder

            // attempt to find DLL file in systemDir
            std::string depPath = systemDir + "\\" + dllName;
            std::vector<uint8_t> depBuf;
            if (!ReadFileToVector(depPath, depBuf)) {
                // not found -> leave dep as nullptr; we'll generate stubs for its functions
                dep = nullptr;
                // keep the placeholder as null to indicate unresolved
                loaded[dllName] = nullptr;
            }
            else {
                // map dep into memory (this will fill *depImg)
                ErrorCode merr = MapPEIntoMemory(depBuf, depPath, *depImg, loaded, systemDir);
                if (merr != ERROR_OK) return merr;
                dep = depImg;
                loaded[dllName] = dep;
            }
        }

        uint32_t firstThunk = impDesc->FirstThunk;
        uint32_t origThunk = impDesc->OriginalFirstThunk ? impDesc->OriginalFirstThunk : impDesc->FirstThunk;
        uint32_t ftVA = img.actualImageBase + firstThunk;
        uint32_t oftVA = img.actualImageBase + origThunk;

        while (true) {
            uint32_t thunkVal = 0;
            if (!ReadU32AtVA(img, oftVA, thunkVal)) return ERROR_LOADER_READER_FAIL;
            if (thunkVal == 0) break;
            uint32_t resolvedAddr = 0;
            bool resolved = false;

            if (thunkVal & IMAGE_ORDINAL_FLAG32) {
                uint32_t ordinal = thunkVal & 0xFFFF;
                if (dep) {
                    auto it = dep->exportsByOrdinal.find(ordinal);
                    if (it != dep->exportsByOrdinal.end()) {
                        resolvedAddr = it->second;
                        resolved = true;
                    }
                }
            }
            else {
                // import by name. thunkVal is RVA to IMAGE_IMPORT_BY_NAME
                IMAGE_IMPORT_BY_NAME* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(HostPtrForVA(img, img.actualImageBase + thunkVal));
                if (ibn && ibn->Name) {
                    char* funcName = reinterpret_cast<char*>(&ibn->Name);
                    std::string fname(funcName);
                    if (dep) {
                        auto itn = dep->exportsByName.find(fname);
                        if (itn != dep->exportsByName.end()) {
                            resolvedAddr = itn->second;
                            resolved = true;
                        }
                    }
                }
            }

            if (!resolved) {
                // create a Thumb stub by default. Thumb stubs earn the LSB=1 marker.
                uint32_t stub = CreateReturnZeroStub(img, true); // thumb
                if (stub == 0) return ERROR_GENERIC;
                resolvedAddr = stub;
                printf("PE loader: created stub for %s import (thunk=0x%08X) -> 0x%08X\n", dllName.c_str(), thunkVal, resolvedAddr);
            }

            // write resolved address into IAT (FirstThunk)
            if (!WriteU32AtVA(img, ftVA, resolvedAddr)) return ERROR_LOADER_READER_FAIL;

            ftVA += 4;
            oftVA += 4;
        }
    }
    return ERROR_OK;
}
std::map<fs::path, std::shared_ptr<PEImage>> g_loadedPEImages;

static ErrorCode MapPEIntoMemory(const std::vector<uint8_t>& fileBuf, const std::string& path,
    PEImage& outImg, std::map<std::string, std::shared_ptr<PEImage>>& loaded,
    const std::string& systemDir)
{
    auto filename = ToLower(fs::path(path).filename().string());
	printf("PE loader: Loading '%s' into memory...\n", path.c_str());
    if(g_loadedPEImages.count(filename)) {
        auto existing = g_loadedPEImages[filename];
        if(existing) {
            outImg = *existing;
            return ERROR_OK;
        }
	}
    if (fileBuf.size() < sizeof(IMAGE_DOS_HEADER)) return ERROR_LOADER_READER_FAIL;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fileBuf.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return ERROR_LOADER_READER_FAIL;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > fileBuf.size()) return ERROR_LOADER_READER_FAIL;
    auto nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(fileBuf.data() + dos->e_lfanew);
    if (nt32->Signature != IMAGE_NT_SIGNATURE) return ERROR_LOADER_READER_FAIL;

    if (nt32->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return ERROR_LOADER_INCORRECT_ATTRIBUTE;
    // if (nt32->FileHeader.Machine != IMAGE_FILE_MACHINE_ARM) return ERROR_LOADER_INCORRECT_ATTRIBUTE;

    uint32_t preferredBase = (uint32_t)nt32->OptionalHeader.ImageBase;
    uint32_t sizeOfImage = nt32->OptionalHeader.SizeOfImage;
    if (sizeOfImage == 0) return ERROR_LOADER_INCORRECT_ATTRIBUTE;

    outImg.path = path;
    outImg.preferredImageBase = preferredBase;
    outImg.sizeOfImage = sizeOfImage;

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt32);
    WORD numSections = nt32->FileHeader.NumberOfSections;
    bool allocationFailed = false;

    // try allocate at preferred base
    for (WORD i = 0; i < numSections; ++i) {
        const IMAGE_SECTION_HEADER& s = sections[i];
        uint32_t secVA = preferredBase + s.VirtualAddress;
        uint32_t secSize = std::max<uint32_t>(s.Misc.VirtualSize, s.SizeOfRawData);
        if (secSize == 0) continue;
        MemoryBlock* mb = nullptr;
        ErrorCode err = sMemoryManager->StaticAlloc(secVA, secSize, &mb);
        if (err != ERROR_OK || !mb) { allocationFailed = true; break; }
        // copy raw data
        if (s.SizeOfRawData > 0) {
            if ((size_t)s.PointerToRawData + s.SizeOfRawData > fileBuf.size()) { sMemoryManager->StaticFree(secVA); return ERROR_LOADER_READER_FAIL; }
            void* dest = (void*)mb->GetRAddr();
            const void* src = fileBuf.data() + s.PointerToRawData;
            memcpy(dest, src, s.SizeOfRawData);
            if (secSize > s.SizeOfRawData) memset((uint8_t*)dest + s.SizeOfRawData, 0, secSize - s.SizeOfRawData);
        }
        else {
            void* dest = (void*)mb->GetRAddr(); memset(dest, 0, secSize);
        }
        outImg.blocks.push_back({ secVA, secSize, mb });
    }

    if (!allocationFailed) {
        outImg.actualImageBase = preferredBase;
    }
    else {
        // free partial allocations
        for (auto& b : outImg.blocks) sMemoryManager->StaticFree(b.vaddr);
        outImg.blocks.clear();

        // find a candidate base that can host all sections
        const uint32_t BASE_START = 0x20000000;
        const uint32_t BASE_END = 0x60000000;
        const uint32_t BASE_STEP = 0x00100000;
        bool baseFound = false;
        uint32_t chosenBase = 0;
        for (uint32_t candidate = BASE_START; candidate < BASE_END; candidate += BASE_STEP) {
            bool ok = true;
            std::vector<uint32_t> tmpAddrs;
            for (WORD i = 0; i < numSections; ++i) {
                const IMAGE_SECTION_HEADER& s = sections[i];
                uint32_t secVA = candidate + s.VirtualAddress;
                uint32_t secSize = std::max<uint32_t>(s.Misc.VirtualSize, s.SizeOfRawData);
                if (secSize == 0) continue;
                MemoryBlock* mb = nullptr;
                ErrorCode err = sMemoryManager->StaticAlloc(secVA, secSize, &mb);
                if (err != ERROR_OK || !mb) { ok = false; break; }
                // free immediately to test
                sMemoryManager->StaticFree(secVA);
            }
            if (ok) { chosenBase = candidate; baseFound = true; break; }
        }
        if (!baseFound) return ERROR_GENERIC;

        // do actual allocations at chosenBase
        for (WORD i = 0; i < numSections; ++i) {
            const IMAGE_SECTION_HEADER& s = sections[i];
            uint32_t secVA = chosenBase + s.VirtualAddress;
            uint32_t secSize = std::max<uint32_t>(s.Misc.VirtualSize, s.SizeOfRawData);
            if (secSize == 0) continue;
            MemoryBlock* mb = nullptr;
            ErrorCode err = sMemoryManager->StaticAlloc(secVA, secSize, &mb);
            if (err != ERROR_OK || !mb) { for (auto& b : outImg.blocks) sMemoryManager->StaticFree(b.vaddr); outImg.blocks.clear(); return ERROR_GENERIC; }
            if (s.SizeOfRawData > 0) {
                if ((size_t)s.PointerToRawData + s.SizeOfRawData > fileBuf.size()) { sMemoryManager->StaticFree(secVA); return ERROR_LOADER_READER_FAIL; }
                void* dest = (void*)mb->GetRAddr();
                const void* src = fileBuf.data() + s.PointerToRawData;
                memcpy(dest, src, s.SizeOfRawData);
                if (secSize > s.SizeOfRawData) memset((uint8_t*)dest + s.SizeOfRawData, 0, secSize - s.SizeOfRawData);
            }
            else {
                void* dest = (void*)mb->GetRAddr(); memset(dest, 0, secSize);
            }
            outImg.blocks.push_back({ secVA, secSize, mb });
        }
        outImg.actualImageBase = chosenBase;
        int64_t delta = (int64_t)outImg.actualImageBase - (int64_t)preferredBase;
        ErrorCode relErr = ApplyRelocations(outImg, fileBuf, nt32, delta);
        if (relErr != ERROR_OK) { for (auto& b : outImg.blocks) sMemoryManager->StaticFree(b.vaddr); outImg.blocks.clear(); return relErr; }
    }

    // parse exports
    ParseAndFillExports(outImg, fileBuf, nt32);
    // resolve imports
    ErrorCode impErr = ResolveImports(outImg, fileBuf, loaded, systemDir);
    if (impErr != ERROR_OK) { for (auto& b : outImg.blocks) sMemoryManager->StaticFree(b.vaddr); outImg.blocks.clear(); return impErr; }
    printf("PE loader: Loaded '%s' into memory...\n", path.c_str());
    g_loadedPEImages[filename] = std::make_shared<PEImage>(outImg);
    return ERROR_OK;
}

// top-level loader
ErrorCode LoadPEImage(const std::string& path, PEImage& outImg, const std::string& systemDir) {
    std::map<std::string, std::shared_ptr<PEImage>> loadedModules; // key: lowercase base filename
    std::vector<uint8_t> fileBuf;
    if (!ReadFileToVector(path, fileBuf)) return ERROR_LOADER_READER_FAIL;
    std::shared_ptr<PEImage> root = std::make_shared<PEImage>();
    ErrorCode err = MapPEIntoMemory(fileBuf, path, *root, loadedModules, systemDir);
    if (err != ERROR_OK) return err;
    outImg = *root;
    return ERROR_OK;
}

std::shared_ptr<PEImage> GetPEImageByHandle(uint32_t handle) {
    for (const auto& pair : g_loadedPEImages) {
        auto img = pair.second;
        if (!img) continue;
        if ((uint32_t)(img->actualImageBase) == handle) {
            return img;
        }
    }
    return nullptr;
}

// End of loader

// -------------------------- Integration note --------------------------
// To use this loader from Executable::Load():
//  - call LoadPEImage(_path, img, ".\\prime_data\\A\\WINDOW\\SYSTEM");
//  - set _address = img.actualImageBase; _size = img.sizeOfImage; _entry = img.actualImageBase + nt->OptionalHeader.AddressOfEntryPoint;
//  - ensure your emulator respects Thumb bit (LSB=1) in function pointers or explicitly set CPSR/T-bit before executing.
// ---------------------------------------------------------------------
