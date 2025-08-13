// 在文件顶部添加这些 include
#include "common.h"
#include "MemoryBlock.h"
#include "MemoryManager.h"
#include "executable.h"
#include <windows.h>    // IMAGE_DOS_HEADER, IMAGE_NT_HEADERS, IMAGE_SECTION_HEADER, IMAGE_FILE_MACHINE_ARM
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "PELoader.h"

// Load() 的实现
ErrorCode Executable::Load()
{
    {
        std::string kernel(".\\PRIME_OS.ROM");
        std::vector<uint8_t> _kernelImage;
        if (!ReadFileToVector(kernel, _kernelImage)) return ERROR_LOADER_READER_FAIL;

        ErrorCode err;
        MemoryBlock* memBlock;
        __check((err = sMemoryManager->StaticAlloc(0x30000000, _kernelImage.size(), &memBlock)), ERROR_OK, err);

        RealPtr addr = memBlock->GetRAddr();
        (memcpy(addr, _kernelImage.data(), _kernelImage.size()));
    }
    // 先尝试 ELF（保持原逻辑）
    {
        ELFIO::elfio reader;
        if (reader.load(_path)) {
            __check(reader.get_class(), ELFCLASS32, ERROR_LOADER_INCORRECT_ATTRIBUTE);
            __check(reader.get_machine(), EM_ARM, ERROR_LOADER_INCORRECT_ATTRIBUTE);

            ELFIO::Elf_Half segSize = reader.segments.size();

            for (int i = 0; i < segSize; i++)
            {
                const ELFIO::segment* seg = reader.segments[i];

                _address = seg->get_virtual_address();
                _size = seg->get_memory_size();
                auto fileSize = seg->get_file_size();
                auto data = seg->get_data();

                ErrorCode err;
                MemoryBlock* memBlock;
                printf("Loading segment %d: VAddr=0x%08X, Size=0x%08X, VEnd=0x%08X\n", i, _address, _size, _address + _size);
                __check((err = sMemoryManager->StaticAlloc(_address, _size, &memBlock)), ERROR_OK, err);

                RealPtr addr = memBlock->GetRAddr();
                if (memcpy_s(addr, _size, data, fileSize)) {
                    sMemoryManager->StaticFree(_address);
                    return ERROR_GENERIC;
                }
            }

            _entry = reader.get_entry();
            _state = EXEC_LOADED;
            return ERROR_OK;
        }
    }

    // 如果不是 ELF，则尝试用 WinSDK 加载 PE32
    {
        PEImage img;
        std::string sysdir = std::string(".\\prime_data\\A\\WINDOW\\SYSTEM");
        ErrorCode err = LoadPEImage(_path, img, sysdir);
        if (err != ERROR_OK) return err;
        // set Executable state (assumes _entry/_address/_size are accessible in this scope)
        _address = img.actualImageBase;
        _size = img.sizeOfImage;
        // To compute entry: we need AddressOfEntryPoint from the PE headers; easiest is to re-open the file and read it:
        std::vector<uint8_t> buf;
        if (!ReadFileToVector(_path, buf)) return ERROR_LOADER_READER_FAIL;
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(buf.data());
        auto nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(buf.data() + dos->e_lfanew);
        _entry = img.actualImageBase + nt32->OptionalHeader.AddressOfEntryPoint;
        _state = EXEC_LOADED;
        return ERROR_OK;
    }

    // 理论上不应到达这里
    return ERROR_LOADER_READER_FAIL;
}
