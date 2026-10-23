#pragma once

#include "result.h"
#include <vector>

namespace dllinjector {
inline constexpr std::uint64_t MaxPeFileBytes = 256ull * 1024 * 1024;
struct PeSection {
    // Display escapes non-ASCII/control bytes; name_bytes preserves all eight bytes.
    std::string name;
    std::string name_bytes;
    std::uint32_t virtual_address, virtual_size, raw_offset, raw_size, characteristics;
};
struct PeDirectory {
    std::uint32_t index, address, size_bytes;
    bool is_file_offset;
    // Set only when the whole declared range has contiguous backing bytes.
    std::optional<std::uint32_t> file_offset;
};
struct PeInfo {
    std::uint64_t file_size;
    bool pe32_plus;
    std::uint16_t machine, characteristics, subsystem, dll_characteristics;
    std::uint32_t entry_point_rva, size_of_image, size_of_headers, section_alignment, file_alignment;
    std::uint64_t image_base;
    std::vector<PeSection> sections;
    std::vector<PeDirectory> directories;
};

// Pure byte inspection: no loading, imports resolution or execution.
Result<PeInfo> ParsePe(const std::vector<std::uint8_t>& bytes, std::uint32_t timeout_ms = 5000);
Result<PeInfo> InspectPeFile(const std::wstring& path, std::uint32_t timeout_ms = 5000);
}
