#pragma once
#include "manifest.h"
#include <string>

namespace cache {

std::string dir();
std::string path(const std::string& file);
std::string partialPath(const std::string& file);
bool init();
bool lookup(const Entry& e);
void storeHash(const std::string& file, const std::string& algo, const std::string& hex);

}
