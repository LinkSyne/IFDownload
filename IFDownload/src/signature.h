#pragma once
#include <string>

#ifdef _WIN32
bool verifyPe(const std::wstring& filePath);
#endif
bool verifyPe(const std::string& filePath);
