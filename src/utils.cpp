/**
 * 文件名: src/utils.cpp
 * 修改: 新增 IsProcess64Bit 实现
 */

#include "utils.h"
#include "utils_detail.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <commdlg.h>

namespace utils {

    void PrintBanner() { std::cout << "--- Injector Engine ---" << std::endl; }
    void Log(const std::string& msg, bool isError) {
        if (isError) std::cerr << "[-] " << msg << std::endl;
        else         std::cout << "[+] " << msg << std::endl;
    }

    bool detail::EnableDebugPrivilege(const Win32Api& api) {
        HANDLE hToken = nullptr;
        LUID luid = {};
        TOKEN_PRIVILEGES tkp = {};
        if (!api.openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
        const auto finish = [&](DWORD error) {
            api.closeHandle(hToken);
            SetLastError(error);
            return error == ERROR_SUCCESS;
        };
        if (!api.lookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &luid)) return finish(GetLastError());
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Luid = luid;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        const BOOL adjusted = api.adjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), nullptr, nullptr);
        const DWORD error = GetLastError();
        // TRUE with ERROR_NOT_ALL_ASSIGNED is not successful assignment.
        const bool success = adjusted != FALSE && error == ERROR_SUCCESS;
        finish(error);
        return success;
    }

    bool EnableDebugPrivilege() {
        return detail::EnableDebugPrivilege(detail::Win32Api{});
    }

    std::vector<unsigned char> ReadFileToBuffer(const std::wstring& filepath) {
        FILE* file = _wfopen(filepath.c_str(), L"rb");
        if (!file) return {};
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (fileSize <= 0) { fclose(file); return {}; }
        std::vector<unsigned char> buffer(fileSize);
        size_t readSize = fread(buffer.data(), 1, fileSize, file);
        fclose(file);
        if (readSize != static_cast<size_t>(fileSize)) return {};
        return buffer;
    }

    std::vector<ProcessInfo> GetAllProcesses() {
        std::vector<ProcessInfo> list;
        const auto result = dllinjector::ListProcesses();
        if (!result) {
            SetLastError(result.error().win32.value_or(
                result.error().category == dllinjector::ErrorCategory::Timeout ? ERROR_TIMEOUT : ERROR_GEN_FAILURE));
            return list;
        }
        for (const auto& process : result.value()) {
            list.push_back({process.pid, process.name, process.creation_time.value_or(0)});
        }
        std::sort(list.begin(), list.end(), [](const ProcessInfo& a, const ProcessInfo& b){
            if (a.creationTime != 0 && b.creationTime != 0) return a.creationTime < b.creationTime;
            if (a.creationTime == 0 && b.creationTime == 0) return a.pid < b.pid;
            if (a.creationTime == 0) return true;
            if (b.creationTime == 0) return false;
            return false;
        });
        return list;
    }

    DWORD GetProcessIdByName(const std::wstring& processName) {
        auto list = GetAllProcesses();
        for (const auto& proc : list) { if (proc.name == processName) return proc.pid; }
        return 0;
    }

    std::wstring detail::SelectFile(const wchar_t* filter, const Win32Api& api) {
        OPENFILENAMEW ofn;
        wchar_t szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(szFile) / sizeof(szFile[0]));
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (api.getOpenFileName(&ofn) == TRUE) return std::wstring(ofn.lpstrFile);
        return L"";
    }

    std::wstring SelectFile(const wchar_t* filter) {
        return detail::SelectFile(filter, detail::Win32Api{});
    }

    namespace {
    ArchitectureResult AdaptArchitecture(const dllinjector::Result<dllinjector::ArchitectureInfo>& result) {
        const auto& stage = result ? result.value().source : result.error().stage;
        const wchar_t* legacyStage = L"Core.QueryProcessArchitecture";
        if (stage == "OpenProcess") legacyStage = L"OpenProcess";
        else if (stage == "IsWow64Process2") legacyStage = L"IsWow64Process2";
        else if (stage == "IsWow64Process") legacyStage = L"IsWow64Process";
        else if (stage == "GetNativeSystemInfo") legacyStage = L"GetNativeSystemInfo";
        if (result) return {result.value().architecture, ERROR_SUCCESS, legacyStage};
        return {ProcessArchitecture::Unknown, result.error().win32.value_or(
            result.error().category == dllinjector::ErrorCategory::Timeout ? ERROR_TIMEOUT : ERROR_GEN_FAILURE), legacyStage};
    }
    }

    ArchitectureResult detail::QueryProcessArchitecture(DWORD pid, const Win32Api& api) {
        return AdaptArchitecture(dllinjector::detail::QueryProcessArchitecture(pid, {}, api));
    }

    ArchitectureResult QueryProcessArchitecture(DWORD pid) {
        return AdaptArchitecture(dllinjector::QueryProcessArchitecture(pid));
    }
}
