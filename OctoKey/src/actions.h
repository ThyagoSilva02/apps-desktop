#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "model.h"
#include <string>
#include <vector>

namespace macro {

bool ValidateAction(const ButtonConfig& button, std::wstring& error);
bool ExecuteAction(const ButtonConfig& button, HWND owner, std::wstring& error);
bool ParseShortcut(const std::wstring& text, std::vector<WORD>& keys, std::wstring& error);
std::vector<INPUT> BuildShortcutInputs(const std::vector<WORD>& keys);

} // namespace macro
