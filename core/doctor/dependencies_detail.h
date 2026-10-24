#pragma once
#include "dllinjector/doctor.h"
#include <windows.h>

namespace dllinjector::detail {
struct DependencyApi {
    decltype(&::GetFullPathNameW) fullPath = ::GetFullPathNameW;
    decltype(&::GetDriveTypeW) driveType = ::GetDriveTypeW;
    decltype(&::GetFileAttributesW) attributes = ::GetFileAttributesW;
    decltype(&::GetTickCount64) tick = ::GetTickCount64;
    decltype(&InspectPeFile) inspectPe = InspectPeFile;
};
Result<DependencyReport> CheckDependencyCandidates(const PeInfo& pe,
    const std::vector<std::wstring>& directories, std::uint32_t timeout_ms, const DependencyApi& api);
Result<DoctorReport> InspectDoctorFile(const std::wstring& path, std::optional<Architecture> target,
    const std::vector<std::wstring>& directories, std::uint32_t timeout_ms, const DependencyApi& api);
}
