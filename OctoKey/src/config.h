#pragma once

#include "model.h"
#include <filesystem>
#include <string>

namespace macro {

std::filesystem::path ConfigPath();
bool LoadConfig(Config& config, std::wstring& error);
bool SaveConfig(const Config& config, std::wstring& error);

// An explicit path also lets tests and import/export avoid the user's settings.
bool LoadConfig(const std::filesystem::path& path, Config& config, std::wstring& error);
bool SaveConfig(const std::filesystem::path& path, const Config& config, std::wstring& error);

} // namespace macro
