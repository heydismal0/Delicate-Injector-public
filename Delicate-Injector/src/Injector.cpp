#include "Injector.h"
#include <TlHelp32.h>
#include <Windows.h>
#include <sstream>
#include <string>
#include <vector>

namespace Injector
{

static std::wstring Utf8ToWide(const std::string &src)
{
    if (src.empty())
        return std::wstring();
    int req = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, nullptr, 0);
    std::wstring out;
    out.resize(req);
    MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, &out[0], req);
    if (!out.empty() && out.back() == L'\0')
        out.pop_back();
    return out;
}

bool EnableDebugPrivilege(std::string &outMessage)
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        outMessage = "OpenProcessToken failed: " + std::to_string(GetLastError());
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid))
    {
        outMessage = "LookupPrivilegeValue failed: " + std::to_string(GetLastError());
        CloseHandle(hToken);
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr))
    {
        outMessage = "AdjustTokenPrivileges failed: " + std::to_string(GetLastError());
        CloseHandle(hToken);
        return false;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        outMessage = "SeDebugPrivilege is not available for this user.";
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    outMessage = "SeDebugPrivilege enabled.";
    return true;
}

std::vector<DWORD> FindProcessesByName(const std::string &processName)
{
    std::vector<DWORD> results;
    std::string lowerName = processName;
    for (auto &c : lowerName)
        c = (char)tolower(c);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return results;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe))
    {
        do
        {
            std::string exeName;
            int req = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nullptr, 0, nullptr, nullptr);
            if (req > 0)
            {
                exeName.resize(req);
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &exeName[0], req, nullptr, nullptr);
                if (!exeName.empty() && exeName.back() == '\0')
                    exeName.pop_back();
                std::string lowerExe = exeName;
                for (auto &c : lowerExe)
                    c = (char)tolower(c);
                if (lowerExe == lowerName)
                {
                    results.push_back(pe.th32ProcessID);
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return results;
}

bool InjectByPid(DWORD pid, const std::string &dllPath, std::string &outMessage)
{
    if (dllPath.empty())
    {
        outMessage = "DLL path is empty.";
        return false;
    }

    std::wstring dllPathW = Utf8ToWide(dllPath);
    if (dllPathW.empty())
    {
        outMessage = "Failed to convert DLL path to wide string.";
        return false;
    }

    DWORD desiredAccess =
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE hProcess = OpenProcess(desiredAccess, FALSE, pid);
    if (!hProcess)
    {
        std::string dbgMsg;
        EnableDebugPrivilege(dbgMsg);
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess)
        {
            outMessage = "OpenProcess failed: " + std::to_string(GetLastError());
            return false;
        }
    }

    SIZE_T sizeBytes = (dllPathW.size() + 1) * sizeof(wchar_t);
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, sizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem)
    {
        outMessage = "VirtualAllocEx failed: " + std::to_string(GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, remoteMem, dllPathW.c_str(), sizeBytes, &written) || written != sizeBytes)
    {
        outMessage = "WriteProcessMemory failed: " + std::to_string(GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32)
    {
        outMessage = "GetModuleHandleW(kernel32) failed: " + std::to_string(GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    FARPROC loadLibAddr = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!loadLibAddr)
    {
        outMessage = "GetProcAddress(LoadLibraryW) failed: " + std::to_string(GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread =
        CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLibAddr, remoteMem, 0, nullptr);
    if (!hThread)
    {
        outMessage = "CreateRemoteThread failed: " + std::to_string(GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD remoteModuleHandle = 0;
    GetExitCodeThread(hThread, &remoteModuleHandle);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (remoteModuleHandle == 0)
    {
        outMessage = "Remote LoadLibraryW returned NULL (injection failed / DLL not loaded).";
        return false;
    }

    outMessage = "DLL injected successfully.";
    return true;
}

bool InjectByProcessName(const std::string &processName, const std::string &dllPath, std::string &outMessage)
{
    auto pids = FindProcessesByName(processName);
    if (pids.empty())
    {
        outMessage = "No process found with name: " + processName;
        return false;
    }

    std::stringstream ss;
    for (DWORD pid : pids)
    {
        std::string tryMsg;
        if (InjectByPid(pid, dllPath, tryMsg))
        {
            outMessage = "Injected into PID " + std::to_string(pid) + " (" + processName + ").";
            return true;
        }
        else
        {
            ss << "PID " << pid << ": " << tryMsg << "; ";
        }
    }

    outMessage = "All injection attempts failed: " + ss.str();
    return false;
}

} // namespace Injector
