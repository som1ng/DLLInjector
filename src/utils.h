/**
 * 文件名: src/utils.h
 * 修改: 新增 IsProcess64Bit 声明
 */

#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include "dllinjector/process.h"

namespace utils {
    struct ProcessInfo {
        DWORD pid;
        std::wstring name;
        unsigned long long creationTime;
    };

    void PrintBanner();
    void Log(const std::string& msg, bool isError = false);
    bool EnableDebugPrivilege();
    
    // 读取文件到内存
    std::vector<unsigned char> ReadFileToBuffer(const std::wstring& filepath);

    std::vector<ProcessInfo> GetAllProcesses();
    DWORD GetProcessIdByName(const std::wstring& processName);
    std::wstring SelectFile(const wchar_t* filter);

    using ProcessArchitecture = dllinjector::Architecture;

    struct ArchitectureResult {
        ProcessArchitecture architecture = ProcessArchitecture::Unknown;
        DWORD error = ERROR_SUCCESS;
        const wchar_t* stage = L"";
    };

    // Query failures stay Unknown; they must not be treated as x86.
    ArchitectureResult QueryProcessArchitecture(DWORD pid);
}
