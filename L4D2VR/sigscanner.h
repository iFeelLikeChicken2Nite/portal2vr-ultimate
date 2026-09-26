#pragma once
#include <Windows.h>
#include <psapi.h>
#include <vector>
#include <sstream>
#include <cstdint>
#include <limits>

class SigScanner
{
public:
	static int FindPattern(const uint8_t *bytes, size_t imageSize, const std::vector<int> &pattern)
	{
		if (!bytes || pattern.empty() || pattern.size() > imageSize)
			return -1;
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
			if (found)
				return static_cast<int>(i);
		}
		return -1;
	}

	// Returns 0 if current offset matches, -1 if no matches found.
	// A value > 0 is the new offset.
	static int VerifyOffset(std::string moduleName, int currentOffset, std::string signature, int sigOffset = 0)
	{
		HMODULE hModule = GetModuleHandleA(moduleName.c_str());
		MODULEINFO moduleInfo{};
		if (!hModule || !GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo)))
			return -1;

		uint8_t *bytes = (uint8_t *)moduleInfo.lpBaseOfDll;

		std::vector<int> pattern;

		std::stringstream ss(signature);
		std::string sigByte;
		while (ss >> sigByte)
		{
			if (sigByte == "?" || sigByte == "??")
				pattern.push_back(-1);
			else
				pattern.push_back(strtoul(sigByte.c_str(), NULL, 16));
		}

		const size_t patternLen = pattern.size();
		const size_t imageSize = moduleInfo.SizeOfImage;
		if (patternLen == 0 || patternLen > imageSize)
			return -1;

		// Check if current offset is good
		const int start = currentOffset - sigOffset;
		if (start >= 0 && static_cast<size_t>(start) <= imageSize - patternLen &&
			FindPattern(bytes + start, patternLen, pattern) == 0)
			return 0;

		// Scan the dll for new offset
		const int match = FindPattern(bytes, imageSize, pattern);
		if (match < 0) return -1;
		const int64_t resolved = static_cast<int64_t>(match) + sigOffset;
		if (resolved < 0 || static_cast<uint64_t>(resolved) >= imageSize ||
			resolved > (std::numeric_limits<int>::max)()) return -1;
		return static_cast<int>(resolved);

	}
};
