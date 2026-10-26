#include "preflight.h"
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected a self-owned DLL fixture");
        const auto result = gui::PreflightDoctor::AnalyzeDll(argv[1], std::nullopt);
        if (!result.valid || result.importCount == 0 || result.dependencies.empty()) {
            throw std::runtime_error("GUI preflight must populate imported dependency candidates");
        }
        if (gui::PreflightDoctor::AnalyzeDll(L"", std::nullopt).valid) {
            throw std::runtime_error("empty selection must remain invalid");
        }
        std::cout << "PASS GUI preflight dependency integration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
