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
void Text(std::vector<std::uint8_t>& b, std::size_t at, const std::string& text) {
    for (std::size_t i = 0; i <= text.size(); ++i) b.at(at + i) = i == text.size() ? 0 : static_cast<std::uint8_t>(text[i]);
}
std::size_t Dir(bool plus) { return 152 + (plus ? 112 : 96); }
std::vector<std::uint8_t> Fixture(bool plus = false) {
    std::vector<std::uint8_t> b(0x4200);
    Put(b, 0, 0x5a4d, 2); Put(b, 60, 128); Put(b, 128, 0x4550);
    Put(b, 132, plus ? 0x8664 : 0x14c, 2); Put(b, 134, 1, 2);
    Put(b, 148, plus ? 240 : 224, 2); Put(b, 150, 0x2002, 2);
    Put(b, 152, plus ? 0x20b : 0x10b, 2);
    Put(b, 152 + (plus ? 24 : 28), plus ? 0x140000000ull : 0x400000, plus ? 8 : 4);
    Put(b, 184, 0x1000); Put(b, 188, 0x200); Put(b, 208, 0x5000); Put(b, 212, 0x200);
    Put(b, 220, 3, 2); Put(b, Dir(plus) - 4, 16);
    const auto s = 152 + (plus ? 240 : 224);
    Text(b, s, ".rdata"); Put(b, s + 8, 0x4000); Put(b, s + 12, 0x1000);
    Put(b, s + 16, 0x4000); Put(b, s + 20, 0x200); Put(b, s + 36, 0x40000040);
    return b;
}
void Imports(std::vector<std::uint8_t>& b, bool plus = false) {
    Put(b, Dir(plus) + 8, 0x1000); Put(b, Dir(plus) + 12, 40);
    Put(b, 0x200, 0x1200); Put(b, 0x20c, 0x1100); Put(b, 0x210, 0x1280);
    Text(b, 0x300, "sample.dll");
    const auto width = plus ? 8u : 4u;
    Put(b, 0x400, 0x1300, width); Put(b, 0x400 + width, (plus ? 0x8000000000000000ull : 0x80000000ull) | 7, width);
    Put(b, 0x480, 0x1300, width); Put(b, 0x480 + width, (plus ? 0x8000000000000000ull : 0x80000000ull) | 7, width);
    Put(b, 0x500, 9, 2); Text(b, 0x502, "Function");
}
void Delay(std::vector<std::uint8_t>& b, bool plus = false) {
    Put(b, Dir(plus) + 13 * 8, 0x1400); Put(b, Dir(plus) + 13 * 8 + 4, 64);
    Put(b, 0x600, 1); Put(b, 0x604, 0x1500); Put(b, 0x608, 0x17c0);
    Put(b, 0x60c, 0x1680); Put(b, 0x610, 0x1600);
    Text(b, 0x700, "delayed.dll"); Put(b, 0x800, 0x1700, plus ? 8 : 4);
    Put(b, 0x900, 3, 2); Text(b, 0x902, "LazyFunction");
}
void Exports(std::vector<std::uint8_t>& b, bool plus = false) {
    Put(b, Dir(plus), 0x1800); Put(b, Dir(plus) + 4, 0x300);
    Put(b, 0xa0c, 0x1880); Put(b, 0xa10, 7); Put(b, 0xa14, 4); Put(b, 0xa18, 3);
    Put(b, 0xa1c, 0x1900); Put(b, 0xa20, 0x1920); Put(b, 0xa24, 0x1930);
    Text(b, 0xa80, "exports.dll");
    Put(b, 0xb00, 0x2000); Put(b, 0xb04, 0x1a40); Put(b, 0xb0c, 0x2010);
    Put(b, 0xb20, 0x1a00); Put(b, 0xb24, 0x1a10); Put(b, 0xb28, 0x1a20);
    Put(b, 0xb30, 0, 2); Put(b, 0xb32, 0, 2); Put(b, 0xb34, 1, 2);
    Text(b, 0xc00, "Alias"); Text(b, 0xc10, "Alpha"); Text(b, 0xc20, "Forward");
    Text(b, 0xc40, "KERNEL32.Sleep");
}
void ErrorAt(const std::vector<std::uint8_t>& b, const char* stage, ErrorCategory category = ErrorCategory::MalformedPe) {
    const auto r = ParsePe(b);
    if (r) { std::cerr << "Unexpected success for " << stage << '\n'; std::exit(1); }
    if (r.error().stage != stage || r.error().category != category)
        std::cerr << "Expected " << stage << "; got " << r.error().stage << " " << CategoryName(r.error().category) << '\n';
    Check(r.error().stage == stage && r.error().category == category, "controlled table error");
    Check(!r.error().win32 && !r.error().ntstatus, "no invented OS codes");
}
void ImportTest() {
    for (bool plus : {false, true}) {
        auto b = Fixture(plus); Imports(b, plus);
        auto r = ParsePe(b); Check(bool(r), "import fixture accepted");
        const auto& lib = r.value().imports.at(0);
        Check(lib.name == "sample.dll" && lib.lookup_source == "import_lookup_table" && lib.symbols.size() == 2, "library and lookup");
        Check(lib.symbols[0].name == "Function" && lib.symbols[0].hint == 9u && !lib.symbols[0].ordinal, "named import");
        Check(lib.symbols[1].ordinal == 7u && !lib.symbols[1].name && !lib.symbols[1].hint, "ordinal import");
        Put(b, 0x200, 0); r = ParsePe(b);
        Check(r && r.value().imports[0].lookup_source == "unbound_iat", "unbound IAT fallback");
        Put(b, 0x204, 1); ErrorAt(b, "pe.imports", ErrorCategory::Unsupported);
        Put(b, 0x200, 0x1200); Check(bool(ParsePe(b)), "bound IAT with original lookup remains readable");
    }
}
void ExportTest() {
    for (bool plus : {false, true}) {
        auto b = Fixture(plus); Exports(b, plus);
        auto r = ParsePe(b); Check(r && r.value().exports.has_value(), "export fixture accepted");
        const auto& table = *r.value().exports;
        Check(table.dll_name == "exports.dll" && table.ordinal_base == 7 && table.address_table_entries == 4, "export metadata");
        Check(table.symbols.size() == 3 && table.symbols[0].names == std::vector<std::string>{"Alias", "Alpha"}, "aliases and hole");
        Check(table.symbols[1].forwarder == "KERNEL32.Sleep" && table.symbols[1].ordinal == 8, "forwarder is literal");
        Check(table.symbols[2].names.empty() && table.symbols[2].ordinal == 10, "ordinal-only export");
        Put(b, 0xa18, 0); Check(ParsePe(b).value().exports->symbols[0].names.empty(), "no name tables required for ordinal exports");
        Put(b, 0xa14, 0); Check(ParsePe(b).value().exports->symbols.empty(), "present empty export table");
    }
}
void DelayTest() {
    for (bool plus : {false, true}) {
        auto b = Fixture(plus); Delay(b, plus);
        auto r = ParsePe(b); Check(r && r.value().delay_imports.size() == 1, "delay fixture accepted");
        const auto& lib = r.value().delay_imports[0];
        Check(lib.name == "delayed.dll" && lib.symbols[0].name == "LazyFunction" && lib.lookup_source == "delay_import_name_table", "delay names");
        Put(b, 0x600, 0); ErrorAt(b, "pe.delay_imports", ErrorCategory::Unsupported);
        Put(b, 0x600, 3); ErrorAt(b, "pe.delay_imports", ErrorCategory::Unsupported);
        Put(b, 0x600, 1); Put(b, 0x610, 0); ErrorAt(b, "pe.delay_imports");
        Put(b, 0x610, 0x1600); Put(b, 0x614, 0x4fff); ErrorAt(b, "pe.delay_imports");
    }
}
void Malformed() {
    auto b = Fixture(); Imports(b); Put(b, Dir(false) + 12, 20); ErrorAt(b, "pe.imports");
    b = Fixture(); Imports(b); Put(b, 0x20c, 0x5000); ErrorAt(b, "pe.imports");
    b = Fixture(true); Imports(b, true); Put(b, 0x400, 0x100001300ull, 8); ErrorAt(b, "pe.imports");
    b = Fixture(); Imports(b); Put(b, 0x404, 0x80010007); ErrorAt(b, "pe.imports");
    b = Fixture(); Imports(b); Put(b, 0x200, 0x4ffc); Put(b, 0x41fc, 0x80000001); ErrorAt(b, "pe.imports");
    b = Fixture(); Imports(b); Put(b, 0x20c, 0x4fff); b.back() = 'A'; ErrorAt(b, "pe.imports");
    b = Fixture(); Imports(b); Put(b, 0x210, 0); ErrorAt(b, "pe.imports");
    b = Fixture(); Exports(b); Put(b, 0xb30, 4, 2); ErrorAt(b, "pe.exports");
    b = Fixture(); Exports(b); Put(b, 0xb30, 2, 2); ErrorAt(b, "pe.exports");
    b = Fixture(); Exports(b); Put(b, 0xa10, 0xffffffff); ErrorAt(b, "pe.exports");
    b = Fixture(); Exports(b); Put(b, 0xb04, 0x1aff); b[0xcff] = 'X'; ErrorAt(b, "pe.exports");
    b = Fixture(); Exports(b); Put(b, 0xa1c, 0x4ffc); ErrorAt(b, "pe.exports");
    b = Fixture(); Delay(b); Put(b, Dir(false) + 13 * 8 + 4, 32); ErrorAt(b, "pe.delay_imports");
    b = Fixture(); Imports(b); Put(b, 0x20c, 0x800); ErrorAt(b, "pe.imports"); // image gap
    b = Fixture(); Imports(b); Put(b, 152 + 224 + 8, 0x5000); Put(b, 208, 0x6000);
    Put(b, 0x20c, 0x5100); ErrorAt(b, "pe.imports"); // virtual-only tail
}
void Limits() {
    auto b = Fixture(); Exports(b); Put(b, 0xa14, 65537); ErrorAt(b, "pe.exports", ErrorCategory::Unsupported);
    b = Fixture(); Imports(b); Text(b, 0x300, "bad\nname.dll"); ErrorAt(b, "pe.imports", ErrorCategory::Unsupported);
    b = Fixture(); Imports(b); b[0x300] = 0xff; ErrorAt(b, "pe.imports", ErrorCategory::Unsupported);
    b = Fixture(); Imports(b); Put(b, 0x20c, 0x3000);
    for (unsigned i = 0; i < 4097; ++i) b[0x2200 + i] = 'A';
    ErrorAt(b, "pe.imports", ErrorCategory::Unsupported);
}
std::uint64_t ticks = 0;
std::uint64_t Tick() { return ticks++; }
void Timeout() {
    auto b = Fixture(); Imports(b); Delay(b); Exports(b);
    ticks = 0; auto r = detail::ParsePe(b, 30, Tick);
    Check(!r && r.error().category == ErrorCategory::Timeout, "table parsing uses the shared deadline");
    Check(bool(ParsePe(b)), "same bytes parse without forced timeout");
}
void Mutations() {
    std::mt19937 rng(0x5441424c);
    for (unsigned i = 0; i < 6000; ++i) {
        const bool plus = (i & 1) != 0;
        auto b = Fixture(plus); Imports(b, plus); Delay(b, plus); Exports(b, plus);
        for (unsigned j = 0; j < 1 + i % 8; ++j) b[0x200 + rng() % 0xb00] = static_cast<std::uint8_t>(rng());
        const auto r = ParsePe(b);
        if (!r) Check(r.error().category == ErrorCategory::MalformedPe || r.error().category == ErrorCategory::Unsupported, "mutation fails safely");
        else Check(r.value().imports.size() <= 1024 && r.value().delay_imports.size() <= 1024, "bounded mutation output");
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "imports") ImportTest(); else if (name == "exports") ExportTest();
    else if (name == "delay") DelayTest(); else if (name == "malformed") Malformed();
    else if (name == "limits") Limits(); else if (name == "timeout") Timeout();
    else if (name == "mutations") Mutations(); else return 2;
    std::cout << "PASS PE tables " << name << '\n';
}
