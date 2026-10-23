#include "pe_detail.h"
#include "common/win32.h"
#include <algorithm>
#include <new>

namespace dllinjector {
namespace {
[[noreturn]] void Fail(ErrorCategory category, const char* stage, const char* message) {
    throw Error{category, stage, message, {}, {}};
}
void Require(bool condition, const char* stage, const char* message) {
    if (!condition) Fail(ErrorCategory::MalformedPe, stage, message);
}
std::uint64_t NativeTick() { return GetTickCount64(); }
class Deadline {
public:
    Deadline(std::uint32_t ms, detail::PeClock tick) : tick_(tick), start_(tick()), ms_(ms) {}
    void Check() const {
        if (tick_() - start_ >= ms_) Fail(ErrorCategory::Timeout, "pe.timeout", "PE inspection deadline expired.");
    }
private:
    detail::PeClock tick_;
    std::uint64_t start_;
    std::uint32_t ms_;
};
bool Contains(std::uint64_t total, std::uint64_t offset, std::uint64_t size) {
    return offset <= total && size <= total - offset;
}
bool Overlaps(std::uint64_t a, std::uint64_t as, std::uint64_t b, std::uint64_t bs) {
    return as && bs && a < b + bs && b < a + as;
}
bool PowerOfTwo(std::uint32_t n) { return n && (n & (n - 1)) == 0; }
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    void Range(std::size_t offset, std::size_t count, const char* stage) const {
        Require(Contains(bytes_.size(), offset, count), stage, "Declared structure extends beyond the file bytes.");
    }
    std::uint64_t Get(std::size_t offset, unsigned width, const char* stage) const {
        Range(offset, width, stage);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) value |= std::uint64_t{bytes_[offset + i]} << (i * 8);
        return value;
    }
    std::uint16_t U16(std::size_t offset, const char* stage) const { return static_cast<std::uint16_t>(Get(offset, 2, stage)); }
    std::uint32_t U32(std::size_t offset, const char* stage) const { return static_cast<std::uint32_t>(Get(offset, 4, stage)); }
private:
    const std::vector<std::uint8_t>& bytes_;
};
std::uint32_t Span(const PeSection& section) { return (std::max)(section.virtual_size, section.raw_size); }
PeInfo Parse(const std::vector<std::uint8_t>& bytes, const Deadline& deadline) {
    deadline.Check();
    if (bytes.size() > MaxPeFileBytes)
        Fail(ErrorCategory::Unsupported, "pe.size", "PE inspection supports files up to 256 MiB.");
    const Reader r(bytes);
    r.Range(0, 64, "pe.dos");
    Require(r.U16(0, "pe.dos") == 0x5a4d, "pe.dos", "Missing DOS MZ signature.");
    const auto nt = r.U32(0x3c, "pe.nt");
    Require(nt >= 64 && nt <= 0x7fffffff, "pe.nt", "Invalid signed e_lfanew offset.");
    r.Range(nt, 24, "pe.nt");
    Require(r.U32(nt, "pe.nt") == 0x4550, "pe.nt", "Missing PE signature.");
    const auto sectionCount = r.U16(nt + 6, "pe.sections");
    Require(sectionCount != 0, "pe.sections", "The image has no sections.");
    if (sectionCount > 96) Fail(ErrorCategory::Unsupported, "pe.sections", "This inspector supports at most 96 sections.");
    const std::size_t opt = static_cast<std::size_t>(nt) + 24;
    const auto optionalSize = r.U16(nt + 20, "pe.optional");
    r.Range(opt, optionalSize, "pe.optional");
    Require(optionalSize >= 2, "pe.optional", "Missing optional-header magic.");
    const auto magic = r.U16(opt, "pe.optional");
    if (magic == 0x107) Fail(ErrorCategory::Unsupported, "pe.optional", "ROM optional headers are not supported.");
    Require(magic == 0x10b || magic == 0x20b, "pe.optional", "Expected a PE32 or PE32+ optional header.");
    const bool plus = magic == 0x20b;
    const std::size_t fixedSize = plus ? 112 : 96;
    Require(optionalSize >= fixedSize, "pe.optional", "Optional header is shorter than its fixed fields.");
    const auto directoryCount = r.U32(opt + fixedSize - 4, "pe.directories");
    Require(directoryCount <= (optionalSize - fixedSize) / 8, "pe.directories", "Directory count exceeds the optional header.");
    if (directoryCount > 16) Fail(ErrorCategory::Unsupported, "pe.directories", "This inspector supports at most 16 data directories.");
    const auto table = opt + optionalSize;
    r.Range(table, std::size_t{sectionCount} * 40, "pe.sections");
    PeInfo p{};
    p.file_size = bytes.size(); p.pe32_plus = plus;
    p.machine = r.U16(nt + 4, "pe.nt"); p.characteristics = r.U16(nt + 22, "pe.nt");
    p.entry_point_rva = r.U32(opt + 16, "pe.optional");
    p.image_base = r.Get(opt + (plus ? 24 : 28), plus ? 8 : 4, "pe.optional");
    p.section_alignment = r.U32(opt + 32, "pe.optional"); p.file_alignment = r.U32(opt + 36, "pe.optional");
    p.size_of_image = r.U32(opt + 56, "pe.optional"); p.size_of_headers = r.U32(opt + 60, "pe.optional");
    p.subsystem = r.U16(opt + 68, "pe.optional"); p.dll_characteristics = r.U16(opt + 70, "pe.optional");
    Require(PowerOfTwo(p.section_alignment) && PowerOfTwo(p.file_alignment) &&
        p.section_alignment >= p.file_alignment && p.file_alignment <= 65536 &&
        (p.section_alignment < 4096 ? p.section_alignment == p.file_alignment : p.file_alignment >= 512),
        "pe.optional", "Invalid image/file alignments for the supported PE layout.");
    Require(p.size_of_headers >= table + std::size_t{sectionCount} * 40 && p.size_of_headers <= bytes.size() &&
        p.size_of_image >= p.size_of_headers && p.size_of_headers % p.file_alignment == 0 &&
        p.size_of_image % p.section_alignment == 0 && p.entry_point_rva < p.size_of_image,
        "pe.optional", "Invalid header/image size or entry-point range.");
    deadline.Check();
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < sectionCount; ++i) {
        deadline.Check();
        const auto at = table + std::size_t{i} * 40;
        PeSection s{};
        bool ended = false;
        for (unsigned j = 0; j < 8; ++j) {
            const auto ch = bytes[at + j];
            s.name_bytes += hex[ch >> 4]; s.name_bytes += hex[ch & 15];
            if (!ch) ended = true;
            if (!ended) {
                if (ch >= 0x20 && ch <= 0x7e && ch != '\\') s.name += static_cast<char>(ch);
                else { s.name += "\\x"; s.name += hex[ch >> 4]; s.name += hex[ch & 15]; }
            }
        }
        s.virtual_size = r.U32(at + 8, "pe.sections"); s.virtual_address = r.U32(at + 12, "pe.sections");
        s.raw_size = r.U32(at + 16, "pe.sections"); s.raw_offset = r.U32(at + 20, "pe.sections");
        s.characteristics = r.U32(at + 36, "pe.sections");
        Require(!s.raw_size || (s.raw_offset >= p.size_of_headers &&
            Contains(bytes.size(), s.raw_offset, s.raw_size) && s.raw_offset % p.file_alignment == 0 &&
            s.raw_size % p.file_alignment == 0), "pe.sections", "Invalid section raw range or alignment.");
        Require(!Span(s) || (s.virtual_address >= p.size_of_headers &&
            Contains(p.size_of_image, s.virtual_address, Span(s)) && s.virtual_address % p.section_alignment == 0),
            "pe.sections", "Invalid section virtual range or alignment.");
        for (const auto& previous : p.sections)
            Require(!Overlaps(s.raw_offset, s.raw_size, previous.raw_offset, previous.raw_size) &&
                !Overlaps(s.virtual_address, Span(s), previous.virtual_address, Span(previous)),
                "pe.sections", "Overlapping section ranges are not accepted.");
        p.sections.push_back(std::move(s));
    }
    for (std::uint32_t i = 0; i < directoryCount; ++i) {
        deadline.Check();
        const auto at = opt + fixedSize + std::size_t{i} * 8;
        PeDirectory d{i, r.U32(at, "pe.directories"), r.U32(at + 4, "pe.directories"), i == 4, {}};
        if (!d.address) Require(!d.size_bytes, "pe.directories", "A nonempty directory has no address.");
        else {
            // GlobalPtr can have a nonzero RVA with a zero Size field.
            const auto length = (std::max)(d.size_bytes, std::uint32_t{1});
            if (d.is_file_offset) {
                Require(Contains(bytes.size(), d.address, length) && d.address % 8 == 0,
                    "pe.directories", "Certificate table file range or alignment is invalid.");
                d.file_offset = d.address;
            } else {
                Require(Contains(p.size_of_image, d.address, length), "pe.directories", "Directory range exceeds the image.");
                if (Contains(p.size_of_headers, d.address, length)) d.file_offset = d.address;
                for (const auto& s : p.sections) {
                    if (d.address >= s.virtual_address && Contains(Span(s), d.address - s.virtual_address, length)) {
                        if (Contains(s.raw_size, d.address - s.virtual_address, length))
                            d.file_offset = s.raw_offset + (d.address - s.virtual_address);
                    }
                }
            }
        }
        p.directories.push_back(d);
    }
    deadline.Check();
    return p;
}
Error FileError(DWORD code, const char* stage) {
    auto category = ErrorCategory::Io;
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) category = ErrorCategory::NotFound;
    else if (code == ERROR_ACCESS_DENIED) category = ErrorCategory::AccessDenied;
    return {category, stage, "Could not read the input file; inspect the Win32 code and check the path, permissions or sharing mode.", code, {}};
}
Error MemoryError() { return {ErrorCategory::System, "pe.memory", "Insufficient memory for PE inspection.", {}, {}}; }
}
Result<PeInfo> detail::ParsePe(const std::vector<std::uint8_t>& bytes, std::uint32_t timeout_ms, PeClock clock) {
    try { return Parse(bytes, Deadline(timeout_ms, clock)); }
    catch (const Error& e) { return e; }
    catch (const std::bad_alloc&) { return MemoryError(); }
}
Result<PeInfo> ParsePe(const std::vector<std::uint8_t>& bytes, std::uint32_t timeout_ms) {
    return detail::ParsePe(bytes, timeout_ms, NativeTick);
}
Result<PeInfo> InspectPeFile(const std::wstring& path, std::uint32_t timeout_ms) {
    try {
        const Deadline deadline(timeout_ms, NativeTick);
        deadline.Check();
        if (path.empty() || path.find(L'\0') != std::wstring::npos)
            Fail(ErrorCategory::InvalidArgument, "pe.path", "A nonempty file path without embedded NUL is required.");
        const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (!needed) return FileError(GetLastError(), "pe.path");
        std::wstring full(needed, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
        if (!length) return FileError(GetLastError(), "pe.path");
        if (length >= needed) Fail(ErrorCategory::Io, "pe.path", "The path changed while being resolved; retry with an absolute path.");
        full.resize(length);
        if (full.size() < 3 || full[1] != L':' || full[2] != L'\\' || GetDriveTypeW(full.substr(0, 3).c_str()) == DRIVE_REMOTE)
            Fail(ErrorCategory::Unsupported, "pe.path", "Use a local drive path; UNC, device and remote drive paths are not supported.");
        deadline.Check();
        detail::UniqueHandle file(CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file) return FileError(GetLastError(), "pe.open");
        deadline.Check();
        if (GetFileType(file.get()) != FILE_TYPE_DISK)
            Fail(ErrorCategory::Unsupported, "pe.file_type", "PE inspection requires a regular disk file.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.get(), &info)) return FileError(GetLastError(), "pe.stat");
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Fail(ErrorCategory::InvalidArgument, "pe.file_type", "PE inspection requires a file, not a directory.");
        const std::uint64_t size = (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
        if (size > MaxPeFileBytes) Fail(ErrorCategory::Unsupported, "pe.size", "PE inspection supports files up to 256 MiB.");
        deadline.Check();
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            deadline.Check();
            const auto wanted = static_cast<DWORD>((std::min)(bytes.size() - offset, std::size_t{1024 * 1024}));
            DWORD count = 0;
            if (!ReadFile(file.get(), bytes.data() + offset, wanted, &count, nullptr)) return FileError(GetLastError(), "pe.read");
            if (!count) Fail(ErrorCategory::Io, "pe.read", "The input file ended before its reported size.");
            offset += count;
        }
        return Parse(bytes, deadline);
    } catch (const Error& e) { return e; }
    catch (const std::bad_alloc&) { return MemoryError(); }
}
}
