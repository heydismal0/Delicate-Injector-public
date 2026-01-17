#pragma once

#include <string>

namespace Config
{
bool Load(std::string &processName, std::string &dllPath);

bool Save(const std::string &processName, const std::string &dllPath);

std::string GetConfigPath();
} // namespace Config
