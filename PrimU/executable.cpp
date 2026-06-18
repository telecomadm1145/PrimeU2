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
		// 注意：这里也需要加上 prot 参数，OS ROM 通常需要所有权限 (读/写/执行)
		__check((err = sMemoryManager->StaticAlloc(0x30000000, _kernelImage.size(), &memBlock, UC_PROT_ALL)), ERROR_OK, err);

		RealPtr addr = memBlock->GetRAddr();
		(memcpy(addr, _kernelImage.data(), _kernelImage.size()));
	}

	// 先尝试 ELF
	{
		ELFIO::elfio reader;
		runningApplicationImagePath = _path; // 设置全局变量，供其他函数使用
		if (reader.load(_path)) {
			__check(reader.get_class(), ELFCLASS32, ERROR_LOADER_INCORRECT_ATTRIBUTE);
			__check(reader.get_machine(), EM_ARM, ERROR_LOADER_INCORRECT_ATTRIBUTE);

			ELFIO::Elf_Half segSize = reader.segments.size();

			for (int i = 0; i < segSize; i++)
			{
				const ELFIO::segment* seg = reader.segments[i];

				// 【重要】只加载需要加载到内存的段 (PT_LOAD)
				if (seg->get_type() != PT_LOAD) {
					continue;
				}

				_address = seg->get_virtual_address();
				// 建议使用 seg->get_memory_size() 并在 Manager 里做页对齐，
				// 这里保留您的硬编码 16MB
				_size = 16 * 1024 * 1024;
				auto fileSize = seg->get_file_size();
				auto data = seg->get_data();

				// 【新增】解析 ELF 段权限并映射到 Unicorn 权限
				uint32_t elf_flags = seg->get_flags();
				uint32_t uc_prot = UC_PROT_NONE;
				if (elf_flags & PF_R) uc_prot |= UC_PROT_READ;
				if (elf_flags & PF_W) uc_prot |= UC_PROT_WRITE;
				if (elf_flags & PF_X) uc_prot |= UC_PROT_EXEC;

				ErrorCode err;
				MemoryBlock* memBlock;
				printf("Loading segment %d: VAddr=0x%08X, Size=0x%08X, VEnd=0x%08X, Prot=%d\n",
					i, _address, _size, _address + _size, uc_prot);

				// 传入 uc_prot
				__check((err = sMemoryManager->StaticAlloc(_address, _size, &memBlock, uc_prot)), ERROR_OK, err);

				RealPtr addr = memBlock->GetRAddr();
				memset(addr, 0, _size); // 先清零整个段，确保 bss 部分正确初始化为 0

				if (fileSize > 0 && data != nullptr) {
					if (memcpy_s(addr, _size, data, fileSize)) {
						sMemoryManager->StaticFree(_address);
						return ERROR_GENERIC;
					}
				}
			}

			_entry = reader.get_entry();
			_state = EXEC_LOADED;
			return ERROR_OK;
		}
	}

	// 如果不是 ELF，则尝试用 WinSDK 加载 PE32
	{
		// ... 保持原有代码不变 ...
		// (如果 LoadPEImage 内部也调用了 StaticAlloc，请记得在里面也加上权限参数)
		PEImage img;
		std::string sysdir = std::string(".\\prime_data\\A\\WINDOW\\SYSTEM");
		runningApplicationImagePath = _path;
		ErrorCode err = LoadPEImage(_path, img, sysdir);
		if (err != ERROR_OK) return err;
		_address = img.actualImageBase;
		_size = img.sizeOfImage;
		std::vector<uint8_t> buf;
		if (!ReadFileToVector(_path, buf)) return ERROR_LOADER_READER_FAIL;
		auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(buf.data());
		auto nt32 = reinterpret_cast<const IMAGE_NT_HEADERS32*>(buf.data() + dos->e_lfanew);
		_entry = img.actualImageBase + nt32->OptionalHeader.AddressOfEntryPoint;
		_state = EXEC_LOADED;
		return ERROR_OK;
	}

	return ERROR_LOADER_READER_FAIL;
}