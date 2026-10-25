#include "dllinjector/injector.h"
#include "dllinjector/text.h"
#include "methods/injector_methods.h"
#include <windows.h>
#include <fstream>

namespace dllinjector {

const char* InjectionMethodName(InjectionMethod method) {
    switch (method) {
    case InjectionMethod::Crt: return "crt";
    case InjectionMethod::Apc: return "apc";
    case InjectionMethod::Hook: return "hook";
    case InjectionMethod::Reflective: return "reflective";
    case InjectionMethod::Hijack: return "hijack";
    case InjectionMethod::Syscall: return "syscall";
    default: return "unknown";
    }
}

std::optional<InjectionMethod> ParseInjectionMethod(const std::wstring& text) {
    if (text == L"crt") return InjectionMethod::Crt;
    if (text == L"apc") return InjectionMethod::Apc;
    if (text == L"hook") return InjectionMethod::Hook;
    if (text == L"reflective") return InjectionMethod::Reflective;
    if (text == L"hijack") return InjectionMethod::Hijack;
    if (text == L"syscall") return InjectionMethod::Syscall;
    return {};
}

namespace {

std::vector<unsigned char> ReadBinaryFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart == 0 || size.QuadPart > 256 * 1024 * 1024) {
        CloseHandle(file);
        return {};
    }
    std::vector<unsigned char> buffer(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read != buffer.size()) {
        CloseHandle(file);
        return {};
    }
    CloseHandle(file);
    return buffer;
}

} // namespace

Result<InjectionOutcome> ExecuteInjection(const InjectionRequest& request) {
    InjectionOutcome outcome;
    outcome.method = request.method;
    outcome.method_name = InjectionMethodName(request.method);
    outcome.dll_path = request.dll_path;
    outcome.pid = request.pid;
    outcome.target_dir = request.target_dir;

    // 1. Preflight check: DLL file existence & PE analysis
    DWORD attr = GetFileAttributesW(request.dll_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        outcome.preflight.push_back({"file_existence", "fail", "DLL file not found or is a directory"});
        return Error{ErrorCategory::NotFound, "preflight.dll_file", "DLL file does not exist or cannot be accessed.", GetLastError(), {}};
    }
    outcome.preflight.push_back({"file_existence", "pass", "DLL file exists and is accessible"});

    // Inspect PE header with Core
    auto peResult = InspectPeFile(request.dll_path, 5000);
    Architecture dllArch = Architecture::Unknown;
    if (!peResult) {
        outcome.preflight.push_back({"pe_format", "fail", "Failed to parse PE headers: " + peResult.error().message});
        if (!request.force) return peResult.error();
    } else {
        const auto& pe = peResult.value();
        dllArch = pe.pe32_plus ? Architecture::X64 : Architecture::X86;
        outcome.preflight.push_back({"pe_format", "pass", std::string("Valid PE file (") + (pe.pe32_plus ? "PE32+ / x64" : "PE32 / x86") + ")"});
    }

    // 2. Preflight check for PID-based injection methods
    if (request.method != InjectionMethod::Hijack) {
        if (!request.pid) {
            return Error{ErrorCategory::InvalidArgument, "preflight.pid", "Target PID is required for this injection method.", {}, {}};
        }

        // Validate target process with Core
        QueryOptions query;
        query.timeout_ms = 5000;
        auto procResult = InspectProcess(*request.pid, query);
        if (!procResult) {
            outcome.preflight.push_back({"target_process", "fail", "Process inspection failed: " + procResult.error().message});
            if (!request.force) return procResult.error();
        } else {
            const auto& proc = procResult.value();
            std::string procInfo = "Process found: " + proc.architecture.source + " (" + ArchitectureName(proc.architecture.architecture) + ")";
            outcome.preflight.push_back({"target_process", "pass", procInfo});

            // Architecture compatibility check
            if (dllArch != Architecture::Unknown && proc.architecture.architecture != Architecture::Unknown) {
                if (dllArch != proc.architecture.architecture) {
                    std::string mismatchMsg = std::string("Architecture mismatch: DLL is ") +
                        ArchitectureName(dllArch) + " but target process is " + ArchitectureName(proc.architecture.architecture);
                    outcome.preflight.push_back({"architecture_match", "fail", mismatchMsg});
                    if (!request.force) {
                        return Error{ErrorCategory::InvalidArgument, "preflight.arch_mismatch", mismatchMsg, {}, {}};
                    }
                } else {
                    outcome.preflight.push_back({"architecture_match", "pass", "DLL and target process architectures match"});
                }
            }
        }
    } else {
        // Hijack preflight check: target directory
        if (!request.target_dir || request.target_dir->empty()) {
            return Error{ErrorCategory::InvalidArgument, "preflight.target_dir", "Target directory is required for DLL hijacking.", {}, {}};
        }
        DWORD dirAttr = GetFileAttributesW(request.target_dir->c_str());
        if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            outcome.preflight.push_back({"target_dir", "fail", "Target directory does not exist or is not a directory"});
            return Error{ErrorCategory::NotFound, "preflight.target_dir", "Target directory does not exist.", GetLastError(), {}};
        }
        outcome.preflight.push_back({"target_dir", "pass", "Target directory exists"});
    }

    outcome.preflight_passed = true;

    // 3. Dispatch to existing injection methods
    bool executed = false;
    bool success = false;
    DWORD targetPid = request.pid.value_or(0);

    switch (request.method) {
    case InjectionMethod::Crt:
        executed = true;
        success = methods::Inject_CRT_DLL(targetPid, request.dll_path);
        break;
    case InjectionMethod::Apc:
        executed = true;
        success = methods::Inject_APC_DLL(targetPid, request.dll_path);
        break;
    case InjectionMethod::Hook:
        executed = true;
        success = methods::Inject_Hook_DLL(targetPid, request.dll_path);
        break;
    case InjectionMethod::Reflective: {
        executed = true;
        auto bytes = ReadBinaryFile(request.dll_path);
        if (bytes.empty()) {
            return Error{ErrorCategory::Io, "inject.reflective", "Failed to read DLL file into memory.", GetLastError(), {}};
        }
        success = methods::Inject_Reflective(targetPid, bytes);
        break;
    }
    case InjectionMethod::Hijack: {
        executed = true;
        // Deploy DLL into target directory
        success = methods::Deploy_Hijack(*request.target_dir, request.dll_path);
        break;
    }
    case InjectionMethod::Syscall:
#if DLLINJECTOR_HAS_SYSCALL
        executed = true;
        success = methods::Inject_Syscall_DLL(targetPid, request.dll_path);
#else
        return Error{ErrorCategory::Unsupported, "inject.syscall", "Direct syscall injection is not available in MSVC builds (requires GNU x64 toolchain).", {}, {}};
#endif
        break;
    }

    outcome.executed = executed;
    outcome.success = success;
    outcome.message = success ? "Injection routine executed successfully." : "Injection routine failed or was blocked by the OS/security software.";

    if (!success) {
        return Error{ErrorCategory::System, "inject.execute", outcome.message, GetLastError(), {}};
    }

    return outcome;
}

} // namespace dllinjector

