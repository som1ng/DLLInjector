#include "dllinjector/pe.h"
#include "pe/pe_detail.h"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace dllinjector;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void Put(std::vector<std::uint8_t>& b, std::size_t at, std::uint64_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) b.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
constexpr std::size_t Opt = 0x98;
std::size_t Sections(bool plus) { return Opt + (plus ? 240 : 224); }
std::size_t Directories(bool plus) { return Opt + (plus ? 112 : 96); }
std::vector<std::uint8_t> Fixture(bool plus = false) {
    std::vector<std::uint8_t> b(0x400);
    Put(b, 0, 0x5a4d, 2); Put(b, 0x3c, 0x80); Put(b, 0x80, 0x4550);
    Put(b, 0x84, plus ? 0x8664 : 0x14c, 2); Put(b, 0x86, 1, 2);
    Put(b, 0x94, plus ? 240 : 224, 2); Put(b, 0x96, 0x2002, 2);
    Put(b, Opt, plus ? 0x20b : 0x10b, 2); Put(b, Opt + 16, 0x1000);
    Put(b, Opt + (plus ? 24 : 28), plus ? 0x20000000010000ull : 0x400000, plus ? 8 : 4);
    Put(b, Opt + 32, 0x1000); Put(b, Opt + 36, 0x200);
    Put(b, Opt + 56, 0x2000); Put(b, Opt + 60, 0x200);
    Put(b, Opt + 68, 3, 2); Put(b, Opt + 70, 0x140, 2);
    Put(b, Opt + (plus ? 108 : 92), 16);
    const auto s = Sections(plus);
    b[s] = '.'; b[s + 1] = 't'; b[s + 2] = 'e'; b[s + 3] = 'x'; b[s + 4] = 't';
    Put(b, s + 8, 0x180); Put(b, s + 12, 0x1000);
    Put(b, s + 16, 0x200); Put(b, s + 20, 0x200); Put(b, s + 36, 0x60000020);
    return b;
}
void Bad(const std::vector<std::uint8_t>& b, const char* stage) {
    const auto r = ParsePe(b);
    Check(!r, "malformed input rejected");
    if (r.error().category != ErrorCategory::MalformedPe || r.error().stage != stage)
        std::cerr << "Expected malformed at " << stage << "; got " << CategoryName(r.error().category)
                  << " at " << r.error().stage << '\n';
    Check(r.error().category == ErrorCategory::MalformedPe && r.error().stage == stage, "malformed category and stage");
    Check(!r.error().win32 && !r.error().ntstatus, "no invented OS error");
}
void Headers() {
    for (bool plus : {false, true}) {
        const auto r = ParsePe(Fixture(plus));
        Check(r && r.value().pe32_plus == plus, "both PE formats independent of host");
        const auto& p = r.value();
        Check(p.machine == (plus ? 0x8664 : 0x14c) && p.subsystem == 3 && p.characteristics == 0x2002,
              "header fields");
        Check(p.image_base == (plus ? 0x20000000010000ull : 0x400000), "image base precision");
        Check(p.sections.size() == 1 && p.directories.size() == 16 && p.file_size == 0x400, "counts");
        Check(p.sections[0].name == ".text" && p.sections[0].name_bytes == "2e74657874000000", "raw names");
    }
    auto b = Fixture(); Put(b, 0x84, 0xffff, 2); Put(b, Opt + 16, 0);
    Check(ParsePe(b) && ParsePe(b).value().machine == 0xffff, "unknown machine and no entry point remain metadata");
    Put(b, Opt + 92, 0); Check(ParsePe(b).value().directories.empty(), "optional empty directory list");
    b = Fixture(); Put(b, Opt + 32, 0x200); Put(b, Opt + 56, 0x1200);
    Check(bool(ParsePe(b)), "small aligned image");
    Check(ExitCode(ErrorCategory::MalformedPe) == 1, "malformed exit code");
}
void Truncation() {
    for (bool plus : {false, true}) {
        auto b = Fixture(plus);
        for (std::size_t length = 0; length < b.size(); ++length) {
            const auto r = ParsePe(std::vector<std::uint8_t>(b.begin(), b.begin() + length));
            Check(!r && r.error().category == ErrorCategory::MalformedPe, "every truncated prefix fails safely");
        }
    }
    auto b = Fixture(); b[0] = 0; Bad(b, "pe.dos");
    b = Fixture(); Put(b, 0x3c, 0xfffffff0); Bad(b, "pe.nt");
    b = Fixture(); Put(b, 0x3c, 32); Bad(b, "pe.nt");
    b = Fixture(); Put(b, 0x80, 0); Bad(b, "pe.nt");
    b = Fixture(); Put(b, 0x94, 2, 2); Bad(b, "pe.optional");
    b = Fixture(); Put(b, Opt, 0x999, 2); Bad(b, "pe.optional");
    b = Fixture(); Put(b, Opt + 92, 17); Bad(b, "pe.directories");
    b = Fixture(); Put(b, 0x86, 0, 2); Bad(b, "pe.sections");
    b = Fixture(); Put(b, 0x86, 97, 2);
    Check(!ParsePe(b) && ParsePe(b).error().category == ErrorCategory::Unsupported, "section resource limit");
    b = Fixture(); Put(b, Opt, 0x107, 2);
    Check(!ParsePe(b) && ParsePe(b).error().category == ErrorCategory::Unsupported, "ROM format explicit");
    b = Fixture(); Put(b, 0x94, 232, 2); Put(b, Opt + 92, 17);
    Check(!ParsePe(b) && ParsePe(b).error().category == ErrorCategory::Unsupported, "extended directory count limit");
}
void Ranges() {
    const auto s = Sections(false);
    for (const auto offset : {s + 16, s + 20}) {
        auto b = Fixture(); Put(b, offset, 0xfffffe00); Bad(b, "pe.sections");
    }
    auto b = Fixture(); Put(b, s + 8, 0xfffff000); Bad(b, "pe.sections");
    b = Fixture(); Put(b, s + 12, 0xfffff000); Bad(b, "pe.sections");
    b = Fixture(); Put(b, s + 20, 0); Bad(b, "pe.sections");
    b = Fixture(); Put(b, Opt + 60, 0x400); Bad(b, "pe.sections");
    b = Fixture(); Put(b, Opt + 60, 0); Bad(b, "pe.optional");
    b = Fixture(); Put(b, Opt + 16, 0); Put(b, Opt + 56, 0x1000); Bad(b, "pe.sections");
    b = Fixture(); Put(b, Opt + 16, 0x2000); Bad(b, "pe.optional");
    for (auto offset : {Opt + 32, Opt + 36}) {
        b = Fixture(); Put(b, offset, 3); Bad(b, "pe.optional");
    }
    b = Fixture(); Put(b, s + 16, 0); Put(b, s + 20, 0xffffffff);
    Check(bool(ParsePe(b)), "zero-sized raw section does not dereference raw pointer");
    b = Fixture(); Put(b, 0x86, 2, 2); Put(b, Opt + 56, 0x3000);
    // A second virtual-only section is valid until it overlaps the first.
    Put(b, s + 40 + 8, 0x200); Put(b, s + 40 + 12, 0x2000);
    Check(bool(ParsePe(b)), "BSS section accepted");
    Put(b, s + 40 + 12, 0x1000); Bad(b, "pe.sections");
    Put(b, s + 40 + 12, 0x2000); Put(b, s + 40 + 16, 0x200); Put(b, s + 40 + 20, 0x200);
    Bad(b, "pe.sections");
    b = Fixture(); for (unsigned i = 0; i < 8; ++i) b[s + i] = 'A';
    Check(ParsePe(b).value().sections[0].name == "AAAAAAAA", "eight-byte name need not terminate");
    b[s] = 0xff; b[s + 1] = '\n';
    Check(ParsePe(b).value().sections[0].name == "\\xff\\x0aAAAAAA", "arbitrary bytes escaped");
}
void DirectoriesTest() {
    auto b = Fixture(); const auto d = Directories(false);
    Put(b, d + 16, 0x1010); Put(b, d + 20, 32);
    auto r = ParsePe(b); Check(r && r.value().directories[2].file_offset == 0x210u, "RVA translation");
    Put(b, d + 16, 0x1100); Put(b, d + 20, 0x200); Put(b, Sections(false) + 8, 0x400);
    r = ParsePe(b); Check(r && !r.value().directories[2].file_offset, "virtual-only tail not file backed");
    Put(b, d + 16, 0xfffffff0); Put(b, d + 20, 32); Bad(b, "pe.directories");
    b = Fixture(); Put(b, d + 4, 1); Bad(b, "pe.directories");
    b = Fixture(); b.resize(0x410); Put(b, d + 32, 0x400); Put(b, d + 36, 16);
    r = ParsePe(b); Check(r && r.value().directories[4].is_file_offset &&
        r.value().directories[4].file_offset == 0x400u, "certificate directory is file-relative");
    Put(b, d + 36, 32); Bad(b, "pe.directories");
    b = Fixture(); Put(b, d + 16, 0x1f0); Put(b, d + 20, 8);
    Check(ParsePe(b).value().directories[2].file_offset == 0x1f0u, "directory in headers");
    b = Fixture(); Put(b, d + 8 * 8, 0x1000);
    Check(bool(ParsePe(b)), "global pointer directory can have zero size");
    b = Fixture(); Put(b, d + 16, 0x1f0); Put(b, d + 20, 0x1000);
    r = ParsePe(b); Check(r && !r.value().directories[2].file_offset, "cross-region range has no single file mapping");
    Put(b, d + 16, 0x800); Put(b, d + 20, 16);
    r = ParsePe(b); Check(r && !r.value().directories[2].file_offset, "image gap is not assumed to contain file bytes");
    b = Fixture(); b.resize(0x410); Put(b, d + 32, 0x401); Put(b, d + 36, 8);
    Bad(b, "pe.directories");
}
std::uint64_t ticks = 0, stride = 0;
std::uint64_t Tick() { const auto current = ticks; ticks += stride; return current; }
void Timeout() {
    auto b = Fixture();
    Check(!ParsePe(b, 0) && ParsePe(b, 0).error().category == ErrorCategory::Timeout, "zero deadline");
    stride = 1; ticks = 0;
    const auto r = detail::ParsePe(b, 5, Tick);
    Check(!r && r.error().category == ErrorCategory::Timeout, "deadline during parse discards partial data");
    stride = 0; ticks = 0; Check(bool(detail::ParsePe(b, 1, Tick)), "unexpired deadline");
    Check(!InspectPeFile(L"") && InspectPeFile(L"").error().category == ErrorCategory::InvalidArgument, "empty file path");
    Check(!InspectPeFile(std::wstring(L"a\0b", 3)) &&
        InspectPeFile(std::wstring(L"a\0b", 3)).error().category == ErrorCategory::InvalidArgument, "embedded NUL rejected");
}
void Mutations() {
    std::mt19937 random(0x5045);
    for (unsigned i = 0; i < 5000; ++i) {
        auto b = Fixture((i & 1) != 0);
        for (unsigned j = 0; j < 1 + i % 8; ++j) b[random() % b.size()] = static_cast<std::uint8_t>(random());
        const auto r = ParsePe(b);
        if (r) {
            Check(r.value().sections.size() <= 96 && r.value().directories.size() <= 16, "bounded mutation results");
            for (const auto& s : r.value().sections)
                Check(s.raw_size == 0 || std::uint64_t{s.raw_offset} + s.raw_size <= b.size(), "no raw range beyond input");
        } else Check(r.error().category == ErrorCategory::MalformedPe || r.error().category == ErrorCategory::Unsupported,
                     "mutations return controlled format errors");
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "headers") Headers(); else if (name == "truncation") Truncation();
    else if (name == "ranges") Ranges(); else if (name == "directories") DirectoriesTest();
    else if (name == "timeout") Timeout(); else if (name == "mutations") Mutations(); else return 2;
    std::cout << "PASS PE " << name << '\n';
}
