#include <string>
#include <wtypes.h>

namespace DllHelper
{
bool ExtractResourceToFile(int resourceId, const std::wstring &outPath, std::string &outLog)
{
    HMODULE hm = GetModuleHandleW(NULL);
    if (!hm)
    {
        outLog = "GetModuleHandleW failed";
        return false;
    }

    HRSRC hrsrc = FindResourceW(hm, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hrsrc)
    {
        outLog = "FindResource failed";
        return false;
    }

    HGLOBAL hres = LoadResource(hm, hrsrc);
    if (!hres)
    {
        outLog = "LoadResource failed";
        return false;
    }

    DWORD size = SizeofResource(hm, hrsrc);
    if (size == 0)
    {
        outLog = "SizeofResource returned 0";
        return false;
    }

    LPVOID data = LockResource(hres);
    if (!data)
    {
        outLog = "LockResource failed";
        return false;
    }

    HANDLE hf = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE)
    {
        outLog = "CreateFileW failed";
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hf, data, size, &written, nullptr);
    CloseHandle(hf);

    if (!ok || written != size)
    {
        outLog = "WriteFile failed or incomplete";
        return false;
    }

    outLog = "Extracted resource to file";
    return true;
}
} // namespace DllHelper
