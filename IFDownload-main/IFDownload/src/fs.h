#pragma once
#include <cstdio>
#include <string>

namespace fs {

FILE* openRead(const std::string& path);
FILE* openWrite(const std::string& path);
FILE* openAppend(const std::string& path);

bool fileExists(const std::string& path);
unsigned long long fileSize(const std::string& path);
double modifiedTime(const std::string& path);
bool removeFile(const std::string& path);
bool renameFile(const std::string& from, const std::string& to);
bool copyFile(const std::string& from, const std::string& to);
bool makeDirs(const std::string& path);

std::string currentDir();
std::string exeDir();
std::string joinPath(const std::string& dir, const std::string& name);
std::string readText(const std::string& path);
bool writeText(const std::string& path, const std::string& content);

}
