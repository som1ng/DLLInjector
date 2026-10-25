#pragma once

#include "dllinjector/result.h"
#include "dllinjector/pe.h"
#include "dllinjector/process.h"
#include <string>
#include <optional>
#include <vector>

namespace dllinjector {

enum class InjectionMethod {
    Crt,
    Apc,
    Hook,
    Reflective,
    Hijack,
    Syscall
};

const char* InjectionMethodName(InjectionMethod method);
std::optional<InjectionMethod> ParseInjectionMethod(const std::wstring& text);

struct PreflightCheckItem {
    std::string check_name;
    std::string status; // "pass", "warn", "fail"
    std::string message;
};

struct InjectionRequest {
    InjectionMethod method = InjectionMethod::Crt;
    std::wstring dll_path;
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> target_dir;
    bool force = false;
};

struct InjectionOutcome {
    InjectionMethod method = InjectionMethod::Crt;
    std::string method_name;
    std::wstring dll_path;
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> target_dir;
    bool preflight_passed = false;
    bool executed = false;
    bool success = false;
    std::vector<PreflightCheckItem> preflight;
    std::string message;
};

// Execute injection with Core preflight checks (PE architecture, target process validation)
Result<InjectionOutcome> ExecuteInjection(const InjectionRequest& request);

} // namespace dllinjector

