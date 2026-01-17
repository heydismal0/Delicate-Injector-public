#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

namespace FileListener
{

class FileListener
{
  public:
    FileListener();
    ~FileListener();

    void Start();

    void Stop();

    bool IsRunning() const;

  private:
    void Worker();
    std::filesystem::path GetTargetPath() const;
    void ProcessFile();

    std::atomic<bool> m_running;
    std::thread m_thread;

    std::filesystem::file_time_type m_lastWriteTime;
};

} // namespace FileListener
