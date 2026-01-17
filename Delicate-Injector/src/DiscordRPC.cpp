#include "DiscordRPC.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "../Libraries/discord/cpp/core.h"
#include "../Libraries/discord/cpp/types.h"
#include <iostream>

using discord::Activity;
using discord::ActivityAssets;
using discord::ActivityParty;
using discord::ActivitySecrets;
using discord::ActivityTimestamps;
using discord::LogLevel;
using discord::Result;

namespace DiscordRPC
{

static std::unique_ptr<discord::Core> s_core;
static std::atomic<bool> s_initialized{false};
static discord::ClientId s_clientId = 0;

static std::function<void(int, const char *)> s_externalLogCallback = nullptr;
static std::mutex s_logMutex;
static std::string s_lastLog;

static const char *ResultToString(Result r)
{
    switch (r)
    {
    case Result::Ok:
        return "Ok";
    case Result::ServiceUnavailable:
        return "ServiceUnavailable";
    case Result::InvalidVersion:
        return "InvalidVersion";
    case Result::LockFailed:
        return "LockFailed";
    case Result::InternalError:
        return "InternalError";
    case Result::InvalidPayload:
        return "InvalidPayload";
    case Result::InvalidCommand:
        return "InvalidCommand";
    case Result::InvalidPermissions:
        return "InvalidPermissions";
    case Result::NotFetched:
        return "NotFetched";
    case Result::NotFound:
        return "NotFound";
    case Result::Conflict:
        return "Conflict";
    case Result::InvalidSecret:
        return "InvalidSecret";
    case Result::InvalidJoinSecret:
        return "InvalidJoinSecret";
    case Result::NoEligibleActivity:
        return "NoEligibleActivity";
    case Result::InvalidInvite:
        return "InvalidInvite";
    case Result::NotAuthenticated:
        return "NotAuthenticated";
    case Result::InvalidAccessToken:
        return "InvalidAccessToken";
    case Result::ApplicationMismatch:
        return "ApplicationMismatch";
    case Result::InvalidDataUrl:
        return "InvalidDataUrl";
    case Result::InvalidBase64:
        return "InvalidBase64";
    case Result::NotFiltered:
        return "NotFiltered";
    case Result::LobbyFull:
        return "LobbyFull";
    case Result::InvalidLobbySecret:
        return "InvalidLobbySecret";
    case Result::InvalidFilename:
        return "InvalidFilename";
    case Result::InvalidFileSize:
        return "InvalidFileSize";
    case Result::InvalidEntitlement:
        return "InvalidEntitlement";
    case Result::NotInstalled:
        return "NotInstalled";
    case Result::NotRunning:
        return "NotRunning";
    case Result::InsufficientBuffer:
        return "InsufficientBuffer";
    case Result::PurchaseCanceled:
        return "PurchaseCanceled";
    case Result::InvalidGuild:
        return "InvalidGuild";
    case Result::InvalidEvent:
        return "InvalidEvent";
    case Result::InvalidChannel:
        return "InvalidChannel";
    case Result::InvalidOrigin:
        return "InvalidOrigin";
    case Result::RateLimited:
        return "RateLimited";
    case Result::OAuth2Error:
        return "OAuth2Error";
    case Result::SelectChannelTimeout:
        return "SelectChannelTimeout";
    case Result::GetGuildTimeout:
        return "GetGuildTimeout";
    case Result::SelectVoiceForceRequired:
        return "SelectVoiceForceRequired";
    case Result::CaptureShortcutAlreadyListening:
        return "CaptureShortcutAlreadyListening";
    case Result::UnauthorizedForAchievement:
        return "UnauthorizedForAchievement";
    case Result::InvalidGiftCode:
        return "InvalidGiftCode";
    case Result::PurchaseError:
        return "PurchaseError";
    case Result::TransactionAborted:
        return "TransactionAborted";
    case Result::DrawingInitFailed:
        return "DrawingInitFailed";
    default:
        return "UnknownResult";
    }
}

bool Init(std::string appId)
{
    if (s_initialized.load())
        return true;

    if (appId.empty())
        return false;

    discord::ClientId cid;
    try
    {
        long long v = std::stoll(appId, nullptr, 0);
        cid = static_cast<discord::ClientId>(v);
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lk(s_logMutex);
            s_lastLog = "Invalid Client ID string";
        }
        return false;
    }

    if (cid == 0)
    {
        {
            std::lock_guard<std::mutex> lk(s_logMutex);
            s_lastLog = "Client ID evaluates to 0";
        }
        return false;
    }

    discord::Core *corePtr = nullptr;
    auto res = discord::Core::Create(cid, static_cast<std::uint64_t>(discord::CreateFlags::NoRequireDiscord), &corePtr);
    if (res != Result::Ok || !corePtr)
    {
        std::ostringstream os;
        os << "Core::Create failed: " << ResultToString(res);
        {
            std::lock_guard<std::mutex> lk(s_logMutex);
            s_lastLog = os.str();
        }
        return false;
    }

    s_core.reset(corePtr);
    s_clientId = cid;

    s_initialized.store(true);
    {
        std::lock_guard<std::mutex> lk(s_logMutex);
        s_lastLog = "SDK initialized";
    }
    return true;
}

void Shutdown()
{
    if (!s_initialized.load())
        return;
    s_core.reset();
    s_clientId = 0;
    s_initialized.store(false);
}

bool SetActivity(const std::string &details, const std::string &state)
{
    if (!s_initialized.load() || !s_core)
        return false;

    discord::Activity activity{};
    if (s_clientId != 0)
    {
        activity.SetApplicationId(static_cast<std::int64_t>(s_clientId));
    }

    if (!details.empty())
        activity.SetDetails(details.c_str());
    else
        activity.SetDetails("");

    if (!state.empty())
        activity.SetState(state.c_str());
    else
        activity.SetState("");

    activity.SetName("Delicate");

    activity.GetAssets().SetLargeImage("logo");

    activity.SetType(discord::ActivityType::Playing);

    s_core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
        std::ostringstream os;
        os << "UpdateActivity result: " << ResultToString(result);
        {
            std::lock_guard<std::mutex> lk(s_logMutex);
            s_lastLog = os.str();
        }
    });

    return true;
}

void ClearActivity()
{
    if (!s_initialized.load() || !s_core)
        return;
    s_core->ActivityManager().ClearActivity([](Result result) {
        std::ostringstream os;
        os << "ClearActivity callback result: " << ResultToString(result);
        {
            std::lock_guard<std::mutex> lk(s_logMutex);
            s_lastLog = os.str();
        }
    });
}

void RunCallbacks()
{
    if (!s_initialized.load() || !s_core)
        return;
    s_core->RunCallbacks();
}

bool IsInitialized()
{
    return s_initialized.load();
}

std::string GetLastLog()
{
    std::lock_guard<std::mutex> lk(s_logMutex);
    return s_lastLog;
}

} // namespace DiscordRPC
