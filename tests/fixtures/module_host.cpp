#include <windows.h>
#include <iostream>
#include <cstdint>

// Test-only cooperative host. Loads the explicit inert fixture into itself.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const HMODULE fixture = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!fixture) return 3;
    const auto value = reinterpret_cast<int (*)()>(GetProcAddress(fixture, "FixtureValue"));
    if (!value || value() != 42) { FreeLibrary(fixture); return 4; }
    std::cout << "READY " << std::hex << reinterpret_cast<std::uintptr_t>(fixture) << '\n' << std::flush;
    std::cin.get();
    return FreeLibrary(fixture) ? 0 : 5;
}
