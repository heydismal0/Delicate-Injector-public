#pragma once
#include <string>

namespace DllHelper
{
bool ExtractResourceToFile(int resourceId, const std::wstring &outPath, std::string &outLog);
}
