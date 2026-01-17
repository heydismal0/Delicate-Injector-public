#include "imgui.h"
#include "impl/imgui_impl_dx11.h"
#include "impl/imgui_impl_win32.h"
#include <d3d11.h>
#include <string>
#include <tchar.h>

#pragma comment(linker, "/DELAYLOAD:discord_game_sdk.dll")
#pragma comment(lib, "delayimp.lib")

#include "Ressources/Resource.h"
#include "Utils/Delicate.h"
#include "src/Config.h"
#include "src/DiscordRPC.h"
#include "src/DllHelper.h"
#include "src/FileListener.h"
#include "src/Injector.h"

static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

static bool g_draggingWindow = false;
static POINT g_dragMouseStart = {0, 0};
static POINT g_windowStartPos = {0, 0};

static HMODULE s_sdkModule = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char **)
{
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, (DWORD)std::size(exePath));
    std::wstring exeFolder(exePath);
    size_t pos = exeFolder.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        exeFolder.resize(pos);
    std::wstring outDllPath = exeFolder + L"\\discord_game_sdk.dll";

    {
        std::string extractLog;
        if (!DllHelper::ExtractResourceToFile(IDR_DISCORDSDK, outDllPath, extractLog))
        {
            return false;
        }
    }

    s_sdkModule = LoadLibraryW(outDllPath.c_str());
    if (!s_sdkModule)
    {
        return false;
    }

    bool discordInitialized = DiscordRPC::Init("1461853440073400442");

    FileListener::FileListener fileListener;
    fileListener.Start();

    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc),
                      CS_CLASSDC,
                      WndProc,
                      0L,
                      0L,
                      GetModuleHandle(nullptr),
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr,
                      L"DelicateInjectorClass",
                      nullptr};

    HICON hIconLarge =
        (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_DELICATEINJECTOR), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    HICON hIconSmall =
        (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_DELICATEINJECTOR), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (!hIconLarge)
        hIconLarge = LoadIcon(nullptr, IDI_APPLICATION);
    if (!hIconSmall)
        hIconSmall = hIconLarge;

    wc.hIcon = hIconLarge;
    wc.hIconSm = hIconSmall;

    ::RegisterClassExW(&wc);

    HWND hwnd =
        ::CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Delicate Injector", WS_POPUP, 100, 100,
                          (int)(600 * main_scale), (int)(200 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd)
    {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconLarge);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);

    style.Colors[ImGuiCol_WindowBg] = Delicate::UISettings::Colors::bgColor.Value;
    style.Colors[ImGuiCol_Header] = Delicate::UISettings::Colors::headerColor.Value;
    style.Colors[ImGuiCol_FrameBg] = Delicate::UISettings::Colors::panelColor.Value;
    style.Colors[ImGuiCol_Button] = Delicate::UISettings::Colors::buttonColor.Value;
    style.Colors[ImGuiCol_ButtonHovered] = Delicate::UISettings::Colors::buttonHoverColor.Value;
    style.Colors[ImGuiCol_Text] = Delicate::UISettings::Colors::textColor.Value;
    style.Colors[ImGuiCol_Border] = Delicate::UISettings::Colors::borderColor.Value;
    style.Colors[ImGuiCol_SliderGrab] = Delicate::UISettings::Colors::accentColor.Value;
    style.Colors[ImGuiCol_SliderGrabActive] = Delicate::UISettings::Colors::accentColor.Value;
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    static char dllPath[260] = "";
    static char processName[128] = "minecraft.windows.exe";
    static std::string status = "Idle";
    static bool show_demo_window = false;
    static std::string outMessage;

    {
        std::string savedProc, savedDll;
        if (Config::Load(savedProc, savedDll))
        {
            if (!savedProc.empty())
                strncpy_s(processName, savedProc.c_str(), _TRUNCATE);
            if (!savedDll.empty())
                strncpy_s(dllPath, savedDll.c_str(), _TRUNCATE);
        }
    }

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DiscordRPC::RunCallbacks();

        ImGuiIO &ioRef = ImGui::GetIO();
        ImVec2 winSize = ImVec2((float)(600 * main_scale), (float)(200 * main_scale));
        ImGui::SetNextWindowSize(winSize);
        ImGui::SetNextWindowPos(ImVec2(0, 0));

        ImGuiWindowFlags wndFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("DelicateInjectorRoot", nullptr, wndFlags);

        {
            std::string header = "Delicate Injector";
            ImVec2 headerPos = ImGui::GetCursorScreenPos();
            headerPos.x += 9.0f;
            headerPos.y += 2.0f;
            ImGui::SetCursorPos(headerPos);
            ImGui::TextColored(Delicate::UISettings::Colors::textColor, header.c_str());
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight());

            ImVec2 wPos = ImGui::GetWindowPos();
            ImVec2 wSize = ImGui::GetWindowSize();
            ImVec2 btnSize = ImVec2(18.0f * main_scale, 18.0f * main_scale);
            float padding = 8.0f * main_scale;

            ImVec2 closePos = ImVec2(wPos.x + wSize.x - padding - btnSize.x, wPos.y + padding);
            ImVec2 minPos = ImVec2(closePos.x - padding - btnSize.x, closePos.y);

            ImGui::SetCursorScreenPos(minPos);
            ImGui::PushStyleColor(ImGuiCol_Button, Delicate::UISettings::Colors::buttonColor.Value);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Delicate::UISettings::Colors::buttonHoverColor.Value);
            ImGui::PushStyleColor(ImGuiCol_Text, Delicate::UISettings::Colors::textColor.Value);
            if (ImGui::Button("-##minimize", btnSize))
            {
                ::ShowWindow(hwnd, SW_MINIMIZE);
            }
            ImGui::PopStyleColor(3);

            ImGui::SetCursorScreenPos(closePos);
            ImGui::PushStyleColor(ImGuiCol_Button, Delicate::UISettings::Colors::headerColor.Value);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, Delicate::UISettings::Colors::textColor.Value);
            if (ImGui::Button("X##close", btnSize))
            {
                ::PostQuitMessage(0);
            }
            ImGui::PopStyleColor(3);

            float headerTop = wPos.y;
            float headerHeight = ImGui::GetTextLineHeightWithSpacing() + 8.0f * main_scale;
            ImVec2 headerMin = ImVec2(wPos.x, headerTop);
            ImVec2 headerMax = ImVec2(wPos.x + wSize.x, headerTop + headerHeight);

            if (ImGui::IsMouseHoveringRect(headerMin, headerMax))
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (!g_draggingWindow && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                ImGui::IsMouseHoveringRect(headerMin, headerMax))
            {
                POINT p;
                GetCursorPos(&p);
                g_dragMouseStart = p;
                RECT r;
                if (GetWindowRect(hwnd, &r))
                {
                    g_windowStartPos.x = r.left;
                    g_windowStartPos.y = r.top;
                }
                SetCapture(hwnd);
                g_draggingWindow = true;
            }

            if (g_draggingWindow && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                POINT cur;
                GetCursorPos(&cur);
                int dx = cur.x - g_dragMouseStart.x;
                int dy = cur.y - g_dragMouseStart.y;
                int newX = g_windowStartPos.x + dx;
                int newY = g_windowStartPos.y + dy;
                ::SetWindowPos(hwnd, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            if (g_draggingWindow && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                ReleaseCapture();
                g_draggingWindow = false;
            }
        }

        ImGui::Separator();

        ImGui::PushItemWidth(-FLT_MIN);
        ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "Target Process");
        ImGui::InputText("##process", processName, IM_ARRAYSIZE(processName));
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImVec2 browserBtnSize = ImVec2(28.0f * main_scale, 0);
        float extraSpacing = 8.0f * main_scale;

        ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "DLL Path");

        ImGui::PushItemWidth(-browserBtnSize.x - extraSpacing);
        ImGui::InputText("##dll", dllPath, IM_ARRAYSIZE(dllPath));
        ImGui::SameLine();
        if (ImGui::Button("...", browserBtnSize))
        {
            WCHAR szFile[MAX_PATH] = L"";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"DLL Files\0*.dll\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameW(&ofn))
            {
                int required = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                if (required > 0 && required <= (int)sizeof(dllPath))
                {
                    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, dllPath, (int)sizeof(dllPath), nullptr, nullptr);
                }
            }
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::BeginGroup();
        if (ImGui::Button("Inject", ImVec2(120 * main_scale, 0)))
        {
            if (dllPath[0] == '\0' || processName[0] == '\0')
            {
                status = "Provide valid process name and DLL path";
            }
            else
            {
                Config::Save(std::string(processName), std::string(dllPath));

                status = "Injecting...";
                if (Injector::InjectByProcessName(processName, dllPath, outMessage))
                {
                    DiscordRPC::SetActivity("Delicate", "Delicate Injector");
                }
                status = outMessage;
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "Status:");
        ImGui::SameLine();
        ImGui::TextColored(Delicate::UISettings::Colors::textColor.Value, "%s", status.c_str());

        ImGui::EndGroup();

        ImGui::Spacing();

        {
            std::string lastLog = DiscordRPC::GetLastLog();
            if (DiscordRPC::IsInitialized())
            {
                ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "Discord:");
                ImGui::SameLine();
                ImGui::TextColored(Delicate::UISettings::Colors::textColor.Value, "Connected (SDK initialized)");
            }
            else
            {
                ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "Discord:");
                ImGui::SameLine();
                ImGui::TextColored(Delicate::UISettings::Colors::headerColor.Value, "Not initialized");
            }

            if (!lastLog.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(Delicate::UISettings::Colors::mutedTextColor.Value, "Last SDK log:");
                ImGui::SameLine();
                ImGui::TextColored(Delicate::UISettings::Colors::textColor.Value, "%s", lastLog.c_str());
            }
        }

        ImGui::End();

        ImGui::Render();

        ImVec4 bg = Delicate::UISettings::Colors::bgColor.Value;
        const float clear_color_with_alpha[4] = {bg.x * bg.w, bg.y * bg.w, bg.z * bg.w, bg.w};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        if (DiscordRPC::IsInitialized())
            DiscordRPC::RunCallbacks();
    }

    Config::Save(std::string(processName), std::string(dllPath));

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    fileListener.Stop();

    if (DiscordRPC::IsInitialized())
    {
        DiscordRPC::Shutdown();
    }
    if (s_sdkModule)
    {
        FreeLibrary(s_sdkModule);
        s_sdkModule = nullptr;
    }

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                                &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try WARP
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                                            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                            &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D *pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

INT WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*lpCmdLine*/, INT /*nCmdShow*/)
{
    int exit_result = main(0, NULL);
    return exit_result;
}
