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
#include <tlhelp32.h>
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
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return list;
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID != 0) {
                    unsigned long long time = 0;
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                    if (hProcess) {
                        FILETIME ftCreate, ftExit, ftKernel, ftUser;
                        if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                            ULARGE_INTEGER ul;
                            ul.LowPart = ftCreate.dwLowDateTime;
                            ul.HighPart = ftCreate.dwHighDateTime;
                            time = ul.QuadPart;
                        }
                        CloseHandle(hProcess);
                    }
                    list.push_back({ pe32.th32ProcessID, pe32.szExeFile, time });
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
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

    ArchitectureResult detail::QueryProcessArchitecture(DWORD pid, const Win32Api& api) {
        HANDLE hProcess = api.openProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) return {ProcessArchitecture::Unknown, GetLastError(), L"OpenProcess"};
        struct ProcessHandle {
            HANDLE handle;
            decltype(&::CloseHandle) close;
            ~ProcessHandle() { close(handle); }
        } owned{hProcess, api.closeHandle};

        if (api.isWow64Process2) {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (!api.isWow64Process2(hProcess, &processMachine, &nativeMachine)) {
                return {ProcessArchitecture::Unknown, GetLastError(), L"IsWow64Process2"};
            }
            const USHORT machine = processMachine == IMAGE_FILE_MACHINE_UNKNOWN ? nativeMachine : processMachine;
            switch (machine) {
            case IMAGE_FILE_MACHINE_I386: return {ProcessArchitecture::X86, ERROR_SUCCESS, L"IsWow64Process2"};
            case IMAGE_FILE_MACHINE_AMD64: return {ProcessArchitecture::X64, ERROR_SUCCESS, L"IsWow64Process2"};
            case IMAGE_FILE_MACHINE_ARM64: return {ProcessArchitecture::Arm64, ERROR_SUCCESS, L"IsWow64Process2"};
            default: return {ProcessArchitecture::Unknown, ERROR_NOT_SUPPORTED, L"IsWow64Process2"};
            }
        }

        // A FALSE WOW64 flag alone cannot distinguish native x86 from native x64.
        SYSTEM_INFO native = {};
        api.getNativeSystemInfo(&native);
        if (native.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL &&
            native.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
            return {ProcessArchitecture::Unknown, ERROR_NOT_SUPPORTED, L"GetNativeSystemInfo"};
        }
        BOOL isWow64 = FALSE;
        if (!api.isWow64Process(hProcess, &isWow64)) {
            return {ProcessArchitecture::Unknown, GetLastError(), L"IsWow64Process"};
        }
        const auto architecture = native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL || isWow64
            ? ProcessArchitecture::X86 : ProcessArchitecture::X64;
        return {architecture, ERROR_SUCCESS, L"IsWow64Process"};
    }

    ArchitectureResult QueryProcessArchitecture(DWORD pid) {
        detail::Win32Api api;
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32) {
            api.isWow64Process2 = reinterpret_cast<detail::Win32Api::IsWow64Process2Fn>(
                GetProcAddress(kernel32, "IsWow64Process2"));
        }
        return detail::QueryProcessArchitecture(pid, api);
    }
}
