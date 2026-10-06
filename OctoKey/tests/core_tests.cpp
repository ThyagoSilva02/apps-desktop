#include "actions.h"
#include "config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned assertions = 0;

void Check(bool passed, const char* description) {
    ++assertions;
    if (!passed) throw std::runtime_error(description);
}

void WriteBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(file.good(), "write test fixture");
}

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

struct TestDirectory {
    std::filesystem::path path;
    TestDirectory() {
        path = std::filesystem::temp_directory_path() /
            (L"MacroPill-core-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-\u00e7");
        Check(std::filesystem::create_directory(path), "create isolated test directory");
    }
    ~TestDirectory() {
        // Delete only the explicit files owned by this test, never a user settings directory.
        std::error_code ignored;
        for (const wchar_t* name : { L"config.ini", L"malformed.ini", L"target.txt" })
            std::filesystem::remove(path / name, ignored);
        std::filesystem::remove(path / L"blocked", ignored);
        std::filesystem::remove(path, ignored);
    }
};

void TestShortcuts() {
    std::wstring error;
    std::vector<WORD> keys;
    Check(macro::ParseShortcut(L" shift + m + cTrL ", keys, error), "shortcut case / modifier order");
    Check(keys == std::vector<WORD>{ VK_CONTROL, VK_SHIFT, L'M' }, "canonical key-down order");
    Check(error.empty(), "shortcut success clears error");
    Check(macro::ParseShortcut(L"WIN+ARROWLEFT", keys, error), "arrow alias");
    const auto inputs = macro::BuildShortcutInputs(keys);
    Check(inputs.size() == 4, "press and release for every key");
    Check(inputs[0].ki.wVk == VK_LWIN && inputs[1].ki.wVk == VK_LEFT &&
          inputs[2].ki.wVk == VK_LEFT && inputs[3].ki.wVk == VK_LWIN, "reverse release order");
    Check((inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) == 0 &&
          (inputs[2].ki.dwFlags & KEYEVENTF_KEYUP) != 0, "key-up flags");
    Check((inputs[1].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0, "extended navigation key flag");
    for (const wchar_t* shortcut : { L"F1", L"F24", L"CTRL+0", L"ALT+ENTER", L"CTRL+SPACE", L"TAB",
          L"ESC", L"BACKSPACE", L"DELETE", L"HOME", L"END", L"PGUP", L"PGDN", L"DOWN", L"UP", L"RIGHT" })
        Check(macro::ParseShortcut(shortcut, keys, error), "supported shortcut");
    for (const wchar_t* shortcut : { L"", L"CTRL", L"CTRL+SHIFT+ALT+WIN", L"CTRL+CONTROL+A", L"A+A",
          L"CTRL+C+V", L"CTRL+", L"+A", L"CTRL++A", L"F0", L"F25", L"UNKNOWN+A" }) {
        Check(!macro::ParseShortcut(shortcut, keys, error), "invalid shortcut rejected");
        Check(keys.empty() && !error.empty(), "shortcut failure returns empty keys and message");
    }
}

void TestActions(const std::filesystem::path& directory) {
    std::wstring error;
    macro::ButtonConfig button;
    Check(macro::ValidateAction(button, error), "empty action valid");
    button.type = macro::ActionType::Url;
    for (const wchar_t* value : { L"https://example.com/", L"HTTP://example.com/?a=1&b=2", L"https://example.com/a%20b" }) {
        button.value = value;
        Check(macro::ValidateAction(button, error), "http or https URL valid");
    }
    for (const wchar_t* value : { L"javascript:alert(1)", L"file:///C:/Windows/notepad.exe", L"https://",
          L"http:///path", L"https://example.com/a b", L"https://example.com/\r\ncmd", L"https://example.com\\path" }) {
        button.value = value;
        Check(!macro::ValidateAction(button, error), "unsafe or malformed URL rejected");
    }
    button.type = macro::ActionType::Program;
    button.value = (directory / L"target.txt").wstring();
    Check(!macro::ValidateAction(button, error), "missing file rejected");
    WriteBytes(directory / L"target.txt", "fixture");
    Check(macro::ValidateAction(button, error), "existing file valid");
    button.value = directory.wstring();
    Check(!macro::ValidateAction(button, error), "directory not a program/file action");
    button.type = static_cast<macro::ActionType>(99);
    Check(!macro::ValidateAction(button, error), "unknown action type rejected");
}

void TestConfig(const std::filesystem::path& directory) {
    const auto path = directory / L"config.ini";
    const auto malformedPath = directory / L"malformed.ini";
    macro::Config config;
    std::wstring error;
    Check(macro::LoadConfig(path, config, error), "missing settings tolerated");
    Check(config.buttons[0].label == L"Bot\u00e3o 1" && config.buttons[7].label == L"Bot\u00e3o 8", "default labels");
    Check(config.baud == 115200 && config.minimizeToTray, "default connection/settings");
    config.port = L"COM123";
    config.minimizeToTray = false;
    config.buttons[0].label = L"A\u00e7\u00e3o \U0001F3B5 = [OBS]";
    config.buttons[0].type = macro::ActionType::Url;
    config.buttons[0].value = L"https://example.com/?a=1&b=2";
    config.buttons[1].value = L"  C:\\a=b\\[nome];#\nlinha\r\ntab\tfinal  ";
    config.buttons[1].label = L"\u65e5\u672c\u8a9e";
    config.buttons[2].type = macro::ActionType::Program;
    config.buttons[2].value = L"C:\\programa-ausente.exe";
    Check(macro::SaveConfig(path, config, error), "save Unicode and escaped special characters");
    const std::string initialBytes = ReadBytes(path);
    Check(initialBytes.size() > 3 && initialBytes.substr(0, 3) == "\xEF\xBB\xBF", "UTF-8 BOM saved");
    macro::Config loaded;
    Check(macro::LoadConfig(path, loaded, error), "load saved configuration");
    Check(loaded.port == config.port && loaded.baud == config.baud && loaded.minimizeToTray == config.minimizeToTray,
          "connection/settings round trip");
    for (std::size_t index = 0; index < config.buttons.size(); ++index)
        Check(loaded.buttons[index].label == config.buttons[index].label && loaded.buttons[index].type == config.buttons[index].type &&
              loaded.buttons[index].value == config.buttons[index].value, "all eight buttons round trip");

    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(lock != INVALID_HANDLE_VALUE, "lock target against replacement");
    macro::Config changed = config;
    changed.buttons[0].label = L"New label";
    const bool savedWhileLocked = macro::SaveConfig(path, changed, error);
    CloseHandle(lock);
    Check(!savedWhileLocked && !error.empty(), "failed atomic replacement reported");
    Check(ReadBytes(path) == initialBytes, "atomic failure leaves previous configuration intact");
    bool leftoverTemporary = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().filename().wstring().find(L"config.ini.tmp-") == 0) leftoverTemporary = true;
    Check(!leftoverTemporary, "failed save cleans its own temporary file");
    Check(macro::SaveConfig(path, changed, error), "replacement after unlocking succeeds");
    Check(macro::LoadConfig(path, loaded, error) && loaded.buttons[0].label == L"New label", "replacement loaded");

    const std::vector<std::string> malformed = {
        "", "version=2\n", "version=1\nbaud=0\n", "version=1\nbaud=42949672960\n",
        "version=1\nport=LPT1\n", "version=1\nminimize_to_tray=2\n", "version=1\nversion=1\n",
        "version=1\n[button1]\ntype=4\n", "version=1\n[button1]\nlabel=bad\\q\n",
        "version=1\n[button1]\nlabel=bad\\\n", "version=1\n[button1]\nlabel=\\uD800\n",
        "version=1\n[button1]\nlabel=\\u0000\n", "version=1\n[button1\n",
        std::string("version=1\n[button1]\nlabel=") + '\xC3' + '\x28' + '\n'
    };
    for (const auto& bytes : malformed) {
        WriteBytes(malformedPath, bytes);
        loaded = changed;
        Check(!macro::LoadConfig(malformedPath, loaded, error), "malformed config rejected");
        Check(!error.empty() && loaded.buttons[0].label == changed.buttons[0].label, "invalid load leaves caller config intact");
    }
    WriteBytes(malformedPath, "version=1\nport=com7\nunknown=ignored\n[future]\nsetting=1\n[button8]\nlabel=Last\ntype=2\nvalue=CTRL+F24\n");
    Check(macro::LoadConfig(malformedPath, loaded, error) && loaded.port == L"COM7" && loaded.buttons[7].label == L"Last",
          "optional fields, unknown sections and case normalized port");
    macro::Config invalid = changed;
    invalid.buttons[0].label.assign(129, L'x');
    Check(!macro::SaveConfig(path, invalid, error), "oversize label rejected before replacement");
    Check(ReadBytes(path).find("New label") != std::string::npos, "validation failure preserves saved file");
    invalid = changed;
    invalid.buttons[0].label.assign(1, static_cast<wchar_t>(0xD800));
    Check(!macro::SaveConfig(path, invalid, error), "unpaired surrogate rejected");
    Check(macro::ConfigPath().is_absolute() && macro::ConfigPath().filename() == L"config.ini", "production path is absolute");
}

} // namespace

int main() {
    try {
        TestDirectory directory;
        TestShortcuts();
        TestActions(directory.path);
        TestConfig(directory.path);
        std::cout << "Passed " << assertions << " core assertions.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failed: " << exception.what() << "\n";
        return 1;
    }
}
