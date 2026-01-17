#include "FileListener.h"
#include "DiscordRPC.h"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace FileListener
{

static const char *kMinecraftPackagePathTail =
    R"(\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\Delicate\discord-rpc.txt)";

FileListener::FileListener() : m_running(false), m_lastWriteTime()
{
}

FileListener::~FileListener()
{
    Stop();
}

void FileListener::Start()
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
        return;

    try
    {
        fs::path p = GetTargetPath();
        if (fs::exists(p))
        {
            m_lastWriteTime = fs::last_write_time(p);
            ProcessFile();
        }
    }
    catch (...)
    {
    }

    m_thread = std::thread(&FileListener::Worker, this);
}

void FileListener::Stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
        return;

    if (m_thread.joinable())
        m_thread.join();
}

bool FileListener::IsRunning() const
{
    return m_running.load();
}

fs::path FileListener::GetTargetPath() const
{
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", buf, ARRAYSIZE(buf));
    if (len == 0 || len >= ARRAYSIZE(buf))
    {
        len = GetEnvironmentVariableA("USERPROFILE", buf, ARRAYSIZE(buf));
    }

    std::string base;
    if (len > 0 && len < ARRAYSIZE(buf))
        base.assign(buf, buf + len);
    else
        base = "C:\\Users\\Default";

    fs::path p = fs::path(base + kMinecraftPackagePathTail);
    return p;
}

void FileListener::Worker()
{
    while (m_running.load())
    {
        try
        {
            fs::path p = GetTargetPath();
            if (fs::exists(p))
            {
                auto writeTime = fs::last_write_time(p);
                if (m_lastWriteTime != writeTime)
                {
                    m_lastWriteTime = writeTime;
                }
                ProcessFile();
            }
            else
            {
                if (DiscordRPC::IsInitialized())
                    DiscordRPC::ClearActivity();
            }
        }
        catch (const std::exception &)
        {
        }
        std::this_thread::sleep_for(1s);
    }
}

static inline std::string Trim(const std::string &s)
{
    const char *whitespace = " \t\r\n";
    size_t start = s.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

static bool TryParseTimestamp(const std::string &s, std::chrono::system_clock::time_point &out)
{
    std::string t = Trim(s);
    if (t.empty())
        return false;

    bool allDigits = true;
    for (char c : t)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            allDigits = false;
            break;
        }
    }

    if (allDigits)
    {
        try
        {
            long long v = std::stoll(t);
            if (t.size() >= 13)
            {
                out = std::chrono::system_clock::time_point(std::chrono::milliseconds(v));
                return true;
            }
            else
            {
                out = std::chrono::system_clock::time_point(std::chrono::seconds(v));
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
    }

    std::tm tm = {};
    std::istringstream ss(t);
    bool hasZ = false;
    if (!t.empty() && (t.back() == 'Z' || t.back() == 'z'))
        hasZ = true;

    std::string parseStr = t;
    if (hasZ)
        parseStr = t.substr(0, t.size() - 1);

    ss.clear();
    ss.str(parseStr);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail())
    {
        ss.clear();
        ss.str(parseStr);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    }
    if (ss.fail())
    {
        return false;
    }

    tm.tm_isdst = -1;

    time_t tt;
    if (hasZ)
        tt = _mkgmtime(&tm);
    else
        tt = mktime(&tm);

    if (tt == -1)
        return false;

    out = std::chrono::system_clock::from_time_t(tt);
    return true;
}

void FileListener::ProcessFile()
{
    fs::path p = GetTargetPath();

    std::ifstream ifs(p, std::ios::in);
    if (!ifs.is_open())
        return;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    std::istringstream ss(content);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ss, line))
    {
        lines.push_back(Trim(line));
    }

    while (!lines.empty() && lines.back().empty())
        lines.pop_back();

    if (lines.empty())
    {
        if (DiscordRPC::IsInitialized())
            DiscordRPC::SetActivity("Chilling in da Injector", "Delicate Injector");
        return;
    }

    if (!DiscordRPC::IsInitialized())
    {
        return;
    }

    bool hasTimestamp = false;
    std::chrono::system_clock::time_point timestamp{};
    if (!lines.empty())
    {
        std::string candidate = lines.back();
        std::chrono::system_clock::time_point tp;
        if (TryParseTimestamp(candidate, tp))
        {
            hasTimestamp = true;
            timestamp = tp;
            lines.pop_back();
            while (!lines.empty() && lines.back().empty())
                lines.pop_back();
        }
    }

    std::string details = "";
    std::string state = "";

    if (!lines.empty())
        details = lines[0];
    if (lines.size() >= 2)
        state = lines[1];

    if (hasTimestamp)
    {
        auto now = std::chrono::system_clock::now();
        auto age = now - timestamp;
        auto ageSecs = std::chrono::duration_cast<std::chrono::seconds>(age).count();

        if (ageSecs > 10)
        {
            DiscordRPC::SetActivity("Chilling in da Injector", "Delicate Injector");
            return;
        }
        DiscordRPC::SetActivity(details, state);
        return;
    }

    DiscordRPC::SetActivity(details, state);
}

} // namespace FileListener
