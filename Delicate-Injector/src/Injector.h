#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace Injector
{
bool InjectByProcessName(const std::string &processName, const std::string &dllPath, std::string &outMessage);

bool InjectByPid(DWORD pid, const std::string &dllPath, std::string &outMessage);

std::vector<DWORD> FindProcessesByName(const std::string &processName);

bool EnableDebugPrivilege(std::string &outMessage);

} // namespace Injector
