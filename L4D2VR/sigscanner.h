#pragma once
#include <Windows.h>
#include <psapi.h>
#include <vector>
#include <sstream>
#include <cstdint>
#include <limits>
#include <charconv>
#include <algorithm>

class SigScanner
{
public:
	static int FindPattern(const uint8_t *bytes, size_t imageSize, const std::vector<int> &pattern)
	{
		if (!bytes || pattern.empty() || pattern.size() > imageSize)
			return -1;
		int match = -1;
		for (size_t i = 0; i <= imageSize - pattern.size(); ++i)
		{
			bool found = true;
			for (size_t j = 0; j < pattern.size(); ++j)
			{
				if (pattern[j] != -1 && bytes[i + j] != pattern[j]) {
					found = false;
					break;
				}
			}
			if (found) {
				if (match >= 0 || i > static_cast<size_t>((std::numeric_limits<int>::max)()))
					return -1;
				match = static_cast<int>(i);
			}
		}
		return match;
	}

	// A unique match at the current RVA returns 0. A relocated RVA is positive.
	// Malformed images, inaccessible code and duplicate signatures fail closed.
	static int VerifyImageOffset(const uint8_t *bytes, size_t imageSize, int currentOffset,
	                             const std::string &signature, int sigOffset = 0)
	{
		if (!bytes || imageSize < sizeof(IMAGE_NT_HEADERS32) ||
			imageSize > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
			!ReadableMemory(bytes, sizeof(IMAGE_DOS_HEADER), false)) return -1;
		std::vector<int> pattern;
		std::stringstream ss(signature);
		std::string token;
		while (ss >> token) {
			if (token == "?" || token == "??") {
				pattern.push_back(-1);
				continue;
			}
			if (token.empty() || token.size() > 2) return -1;
			unsigned value = 0;
			const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 16);
			if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value > 255)
				return -1;
			pattern.push_back(static_cast<int>(value));
		}
		if (pattern.empty()) return -1;
		const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
			static_cast<size_t>(dos->e_lfanew) > imageSize - sizeof(IMAGE_NT_HEADERS32)) return -1;
		const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(bytes + dos->e_lfanew);
		if (!ReadableMemory(nt, sizeof(*nt), false) || nt->Signature != IMAGE_NT_SIGNATURE ||
			nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
			nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
			nt->OptionalHeader.SizeOfImage != imageSize || !nt->FileHeader.NumberOfSections) return -1;
		const uint64_t tableOffset = static_cast<uint64_t>(dos->e_lfanew) + sizeof(DWORD) +
			sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader;
		const uint64_t tableBytes = static_cast<uint64_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
		if (tableOffset > imageSize || tableBytes > imageSize - tableOffset ||
			tableBytes > nt->OptionalHeader.SizeOfHeaders ||
			tableOffset > nt->OptionalHeader.SizeOfHeaders - tableBytes ||
			!ReadableMemory(bytes + static_cast<size_t>(tableOffset),
				static_cast<size_t>(tableBytes), false)) return -1;
		const auto *sections = reinterpret_cast<const IMAGE_SECTION_HEADER *>(bytes + static_cast<size_t>(tableOffset));
		int64_t match = -1;
		size_t matchSectionStart = 0;
		size_t matchSectionEnd = 0;
		for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
			const auto &section = sections[s];
			if ((section.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ)) !=
				(IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ)) continue;
			const size_t start = section.VirtualAddress;
			const size_t span = section.Misc.VirtualSize ?
				static_cast<size_t>(section.Misc.VirtualSize) :
				static_cast<size_t>(section.SizeOfRawData);
			if (start >= imageSize || span > imageSize - start ||
				(span && !ReadableMemory(bytes + start, span, true))) return -1;
			if (span < pattern.size()) continue;
			for (size_t i = start; i <= start + span - pattern.size(); ++i) {
				bool found = true;
				for (size_t j = 0; j < pattern.size(); ++j) {
					if (pattern[j] != -1 && bytes[i + j] != pattern[j]) { found = false; break; }
				}
				if (found) {
					if (match >= 0) return -1;
					match = static_cast<int64_t>(i);
					matchSectionStart = start;
					matchSectionEnd = start + span;
				}
			}
		}
		if (match < 0) return -1;
		const int64_t resolved = match + sigOffset;
		if (resolved <= 0 || resolved >= static_cast<int64_t>(imageSize) ||
			resolved < static_cast<int64_t>(matchSectionStart) ||
			resolved >= static_cast<int64_t>(matchSectionEnd) ||
			resolved > (std::numeric_limits<int>::max)()) return -1;
		return resolved == currentOffset ? 0 : static_cast<int>(resolved);
	}

	static int VerifyOffset(const std::string &moduleName, int currentOffset,
	                        const std::string &signature, int sigOffset = 0)
	{
		HMODULE module = GetModuleHandleA(moduleName.c_str());
		MODULEINFO info{};
		if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
			return -1;
		return VerifyImageOffset(static_cast<const uint8_t *>(info.lpBaseOfDll),
			info.SizeOfImage, currentOffset, signature, sigOffset);
	}

private:
	static bool ReadableMemory(const void *address, size_t length, bool executable)
	{
		if (!address || !length) return false;
		const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
		if (length > UINTPTR_MAX - begin) return false;
		const uintptr_t end = begin + length;
		for (uintptr_t at = begin; at < end;) {
			MEMORY_BASIC_INFORMATION region{};
			if (!VirtualQuery(reinterpret_cast<const void *>(at), &region, sizeof(region)) ||
				region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) return false;
			const DWORD protection = region.Protect & 0xff;
			const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
				protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
			const bool canExecute = protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
			if (!readable || (executable && !canExecute)) return false;
			const uintptr_t regionBase = reinterpret_cast<uintptr_t>(region.BaseAddress);
			if (region.RegionSize > UINTPTR_MAX - regionBase) return false;
			const uintptr_t regionEnd = regionBase + region.RegionSize;
			if (regionEnd <= at) return false;
			at = (std::min)(end, regionEnd);
		}
		return true;
	}
};
