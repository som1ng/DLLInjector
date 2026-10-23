#pragma once
#include "dllinjector/doctor.h"
#include <windows.h>

namespace dllinjector::detail {
struct DependencyApi {
    decltype(&::GetFullPathNameW) fullPath = ::GetFullPathNameW;
    decltype(&::GetDriveTypeW) driveType = ::GetDriveTypeW;
    decltype(&::GetFileAttributesW) attributes = ::GetFileAttributesW;
    decltype(&::GetTickCount64) tick = ::GetTickCount64;
};
Result<DependencyReport> CheckDependencyCandidates(const PeInfo& pe,
    const std::vector<std::wstring>& directories, std::uint32_t timeout_ms, const DependencyApi& api);
}
