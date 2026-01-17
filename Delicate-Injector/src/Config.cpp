#include "Config.h"
#include "../Libraries/json/json.hpp"
#include <Windows.h>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace
{

std::string WideToUtf8(const std::wstring &s)
{
    if (s.empty())
        return {};
    int req = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (req <= 0)
        return {};
    std::string out;
    out.resize(req);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], req, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}
} // namespace

namespace Config
{

std::string GetConfigPath()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (len == 0)
    {
        return "delicate_config.json";
    }
    std::wstring wpath(buf, buf + len);
    // strip filename
    size_t pos = wpath.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"" : wpath.substr(0, pos + 1);
    std::wstring cfg = dir + L"delicate_config.json";
    return WideToUtf8(cfg);
}

bool Save(const std::string &processName, const std::string &dllPath)
{
    nlohmann::json top_config = nlohmann::json::object();
    top_config["process"] = processName;
    top_config["dll"] = dllPath;
    std::string path = GetConfigPath();
    std::ofstream file(path, std::ios::trunc);
    if (file.is_open())
    {
        file << std::setw(4) << top_config;
        file.close();
        return true;
    }
    return false;
}

bool Load(std::string &processName, std::string &dllPath)
{
    processName.clear();
    dllPath.clear();
    std::string path = GetConfigPath();
    std::ifstream file(path);
    if (!file.good())
    {
        return false;
    }

    nlohmann::json doc;
    try
    {
        doc = nlohmann::json::parse(file);
    }
    catch (...)
    {
        return false;
    }

    if (!doc.is_object())
    {
        return false;
    }

    if (doc.contains("process"))
    {
        auto pVal = doc["process"];
        if (pVal.is_string())
        {
            processName = pVal.get<std::string>();
        }
    }
    if (doc.contains("dll"))
    {
        auto dVal = doc["dll"];
        if (dVal.is_string())
        {
            dllPath = dVal.get<std::string>();
        }
    }
    return true;
}

} // namespace Config
