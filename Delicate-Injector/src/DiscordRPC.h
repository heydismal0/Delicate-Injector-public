#pragma once

#include <functional>
#include <string>

namespace DiscordRPC
{

bool Init(std::string appId);
void Shutdown();
bool SetActivity(const std::string &details, const std::string &state = "");
void ClearActivity();
void RunCallbacks();
bool IsInitialized();

void SetLogCallback(std::function<void(int, const char *)> callback);

std::string GetLastLog();

} // namespace DiscordRPC
