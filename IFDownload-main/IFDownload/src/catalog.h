#pragma once
#include "manifest.h"
#include <string>

bool fetchCatalog(const std::vector<Entry>& manifestEntries, std::vector<Entry>& entries, std::string& note);
std::string catalogNodeUrl(const std::string& itemPath);
