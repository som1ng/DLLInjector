#include "cli.h"
#include "common/win32.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>

namespace {
using namespace dllinjector;
std::optional<Error> WriteBytes(HANDLE handle, const std::string& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto count = static_cast<DWORD>((std::min)(bytes.size() - offset, std::size_t{65536}));
        const BOOL ok = WriteFile(handle, bytes.data() + offset, count, &written, nullptr);
        if (!ok || written == 0) {
            const DWORD code = ok ? ERROR_WRITE_FAULT : GetLastError();
            return Error{ErrorCategory::Io, "output.write", "Could not write the complete result.", code, {}};
        }
        offset += written;
    }
    return {};
}
std::optional<Error> WriteStream(DWORD stream, const std::string& text) {
    const HANDLE handle = GetStdHandle(stream);
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return WriteBytes(handle, text);
    // Unicode console output without changing the user's console code page.
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (!count) return detail::Win32Error(GetLastError(), "MultiByteToWideChar");
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count))
        return detail::Win32Error(GetLastError(), "MultiByteToWideChar");
    std::size_t offset = 0;
    while (offset < wide.size()) {
        DWORD written = 0;
        auto length = static_cast<DWORD>((std::min)(wide.size() - offset, std::size_t{8192}));
        // Keep a surrogate pair together across console-write chunks.
        if (offset + length < wide.size() && wide[offset + length - 1] >= 0xd800 && wide[offset + length - 1] <= 0xdbff)
            --length;
        if (!WriteConsoleW(handle, wide.data() + offset, length, &written, nullptr) || !written)
            return Error{ErrorCategory::Io, "output.console", "Could not write to the console.", GetLastError(), {}};
        offset += written;
    }
    return {};
}
std::optional<Error> Save(const std::wstring& path, const std::string& text) {
    detail::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        const DWORD code = GetLastError();
        return Error{code == ERROR_ACCESS_DENIED ? ErrorCategory::AccessDenied : ErrorCategory::Io,
                     "output.open", "Could not create output file; use a new path in an existing directory.", code, {}};
    }
    return WriteBytes(file.get(), text);
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace dllinjector;
    const auto start = std::chrono::steady_clock::now();
    bool json = false, verbose = false;
    cli::Report report;
    std::optional<std::wstring> output;
    try {
        std::vector<std::wstring> args(argv + 1, argv + argc);
        json = std::find(args.begin(), args.end(), L"--json") != args.end();
        const auto options = cli::Parse(args);
        if (!options) report.errors.push_back(options.error());
        else {
            verbose = options.value().verbose;
            output = options.value().output;
            report = cli::Execute(options.value());
        }
        report.duration_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        auto rendered = cli::Render(report, json);
        if (!rendered) {
            report.errors.push_back(rendered.error());
            report.data = std::monostate{};
            rendered = cli::Render(report, json);
        }
        if (!rendered) throw std::runtime_error("Could not encode result.");
        if (output) {
            if (auto error = Save(*output, rendered.value())) {
                report.errors.insert(report.errors.begin(), *error);
                report.data = std::monostate{};
                rendered = cli::Render(report, json);
            }
        }
        for (const auto& error : report.errors)
            WriteStream(STD_ERROR_HANDLE, std::string("[error] ") + CategoryName(error.category) +
                        " at " + error.stage + ": " + error.message + "\n");
        if (verbose) WriteStream(STD_ERROR_HANDLE, "[debug] " + report.command + " duration_ms=" +
                                  std::to_string(report.duration_ms) + "\n");
        if (auto error = WriteStream(STD_OUTPUT_HANDLE, rendered.value())) {
            WriteStream(STD_ERROR_HANDLE, "[error] " + error->message + "\n");
            return ExitCode(error->category);
        }
        return report.errors.empty() ? 0 : ExitCode(report.errors.front().category);
    } catch (const std::exception&) {
        report.data = std::monostate{};
        report.errors = {{ErrorCategory::System, "cli.internal", "Unexpected internal failure.", {}, {}}};
        const auto rendered = cli::Render(report, json);
        if (rendered) WriteStream(STD_OUTPUT_HANDLE, rendered.value());
        WriteStream(STD_ERROR_HANDLE, "[error] Unexpected internal failure.\n");
        return 4;
    }
}
