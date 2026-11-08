#pragma once
#include <string>

#ifdef _WIN32
std::wstring cacheDirW();
std::wstring cachePathW(const std::string& file);
bool cacheHitW(const std::string& file, unsigned int expectedMb);
#endif

std::string cacheDir();
std::string cachePath(const std::string& file);
bool cacheHit(const std::string& file, unsigned int expectedMb);
bool cacheInit();
