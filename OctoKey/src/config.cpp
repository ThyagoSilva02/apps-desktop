#include "config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace macro {
namespace {

constexpr std::size_t MaxConfigBytes = 8 * 1024 * 1024;

class FileHandle {
public:
    explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~FileHandle() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool close() {
        if (!valid()) return true;
        const HANDLE handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return CloseHandle(handle) != FALSE;
    }
private:
    HANDLE handle_;
};

std::wstring Trim(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

std::wstring LowerAscii(std::wstring text) {
    for (auto& character : text)
        if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
    return text;
}

bool ParseUnsigned(const std::wstring& text, unsigned maximum, unsigned& value) {
    if (text.empty()) return false;
    unsigned result = 0;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        const unsigned digit = static_cast<unsigned>(character - L'0');
        if (result > maximum / 10 || (result == maximum / 10 && digit > maximum % 10)) return false;
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

bool ValidPort(const std::wstring& port) {
    if (port.empty()) return true;
    if (port.size() < 4 || port.size() > 8 || LowerAscii(port.substr(0, 3)) != L"com") return false;
    unsigned number = 0;
    return ParseUnsigned(port.substr(3), 65535, number) && number != 0;
}

bool ValidUnicode(const std::wstring& text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned character = static_cast<unsigned short>(text[index]);
        if (character >= 0xD800 && character <= 0xDBFF) {
            if (++index == text.size()) return false;
            const unsigned next = static_cast<unsigned short>(text[index]);
            if (next < 0xDC00 || next > 0xDFFF) return false;
        } else if (character >= 0xDC00 && character <= 0xDFFF) return false;
    }
    return true;
}

bool StructurallyValid(const Config& config, std::wstring& error) {
    if (!ValidPort(config.port)) {
        error = L"A porta deve ser COM seguida de um n\u00famero, por exemplo COM3.";
        return false;
    }
    if (!config.baud || config.baud > 4000000) {
        error = L"A velocidade da porta serial \u00e9 inv\u00e1lida.";
        return false;
    }
    for (std::size_t index = 0; index < config.buttons.size(); ++index) {
        const auto& button = config.buttons[index];
        const int type = static_cast<int>(button.type);
        if (type < 0 || type > 3 || button.label.size() > 128 || button.value.size() > 32767 ||
            button.label.find(L'\0') != std::wstring::npos || button.value.find(L'\0') != std::wstring::npos ||
            !ValidUnicode(button.label) || !ValidUnicode(button.value)) {
            error = L"A configura\u00e7\u00e3o do bot\u00e3o " + std::to_wstring(index + 1) + L" \u00e9 inv\u00e1lida.";
            return false;
        }
    }
    return true;
}

std::wstring WindowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length ? Trim(std::wstring(buffer, length)) : L"Erro " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}

std::wstring Escape(const std::wstring& value) {
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result;
    for (const wchar_t character : value) {
        switch (character) {
        case L'\\': result += L"\\\\"; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (character < 32 || character == 127) {
                const unsigned code = static_cast<unsigned short>(character);
                result += L"\\u";
                for (int shift = 12; shift >= 0; shift -= 4) result += hex[(code >> shift) & 15];
            } else result += character;
        }
    }
    return result;
}

bool Unescape(const std::wstring& value, std::wstring& result) {
    result.clear();
    for (std::size_t index = 0; index < value.size(); ++index) {
        wchar_t character = value[index];
        if (character != L'\\') { result += character; continue; }
        if (++index == value.size()) return false;
        switch (value[index]) {
        case L'\\': result += L'\\'; break;
        case L'n': result += L'\n'; break;
        case L'r': result += L'\r'; break;
        case L't': result += L'\t'; break;
        case L'u': {
            if (value.size() - index <= 4) return false;
            unsigned code = 0;
            for (unsigned digitIndex = 0; digitIndex < 4; ++digitIndex) {
                const wchar_t digit = value[++index];
                unsigned number = 0;
                if (digit >= L'0' && digit <= L'9') number = static_cast<unsigned>(digit - L'0');
                else if (digit >= L'a' && digit <= L'f') number = 10 + static_cast<unsigned>(digit - L'a');
                else if (digit >= L'A' && digit <= L'F') number = 10 + static_cast<unsigned>(digit - L'A');
                else return false;
                code = (code << 4) | number;
            }
            result += static_cast<wchar_t>(code);
            break;
        }
        default: return false;
        }
    }
    return true;
}

bool EncodeUtf8(const std::wstring& text, std::string& bytes) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    if (text.empty()) { bytes.clear(); return true; }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (!size) return false;
    bytes.resize(size);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        bytes.data(), size, nullptr, nullptr) == size;
}

bool DecodeUtf8(const std::string& bytes, std::wstring& text) {
    std::size_t start = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) start = 3;
    if (start == bytes.size()) { text.clear(); return true; }
    const int length = static_cast<int>(bytes.size() - start);
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + start, length, nullptr, 0);
    if (!size) return false;
    text.resize(size);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + start, length, text.data(), size) == size;
}

bool ParseConfig(const std::wstring& text, Config& config, std::wstring& error) {
    if (text.find(L'\0') != std::wstring::npos) { error = L"O arquivo de configura\u00e7\u00e3o cont\u00e9m caracteres inv\u00e1lidos."; return false; }
    Config candidate = DefaultConfig();
    std::wistringstream stream(text);
    std::wstring line;
    int section = -1; // -1 = global, -2 = a future/unknown section.
    unsigned lineNumber = 0;
    std::set<std::wstring> seen;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#') continue;
        const auto fail = [&]() {
            error = L"Configura\u00e7\u00e3o inv\u00e1lida na linha " + std::to_wstring(lineNumber) + L".";
            return false;
        };
        if (trimmed.front() == L'[') {
            if (trimmed.size() < 3 || trimmed.back() != L']') return fail();
            const auto name = LowerAscii(Trim(trimmed.substr(1, trimmed.size() - 2)));
            section = -2;
            if (name.compare(0, 6, L"button") == 0) {
                unsigned number = 0;
                if (ParseUnsigned(name.substr(6), 8, number) && number >= 1) section = static_cast<int>(number - 1);
            }
            continue;
        }
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) return fail();
        const std::wstring key = LowerAscii(Trim(line.substr(0, equals)));
        const std::wstring encoded = line.substr(equals + 1);
        bool recognized = section == -1 ? (key == L"version" || key == L"port" || key == L"baud" || key == L"minimize_to_tray") :
            section >= 0 && (key == L"label" || key == L"type" || key == L"value");
        if (!recognized) continue;
        if (!seen.insert(std::to_wstring(section) + L":" + key).second) return fail();
        std::wstring value;
        if (!Unescape(encoded, value)) return fail();
        unsigned number = 0;
        if (section == -1) {
            if (key == L"version") {
                if (!ParseUnsigned(Trim(value), 1, number) || number != 1) return fail();
            } else if (key == L"port") {
                candidate.port = Trim(value);
                for (auto& character : candidate.port)
                    if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
            } else if (key == L"baud") {
                if (!ParseUnsigned(Trim(value), 4000000, number) || !number) return fail();
                candidate.baud = number;
            } else {
                if (!ParseUnsigned(Trim(value), 1, number)) return fail();
                candidate.minimizeToTray = number != 0;
            }
        } else {
            auto& button = candidate.buttons[static_cast<std::size_t>(section)];
            if (key == L"label") button.label = std::move(value);
            else if (key == L"value") button.value = std::move(value);
            else {
                if (!ParseUnsigned(Trim(value), 3, number)) return fail();
                button.type = static_cast<ActionType>(number);
            }
        }
    }
    if (!seen.count(L"-1:version")) {
        error = L"O arquivo de configura\u00e7\u00e3o n\u00e3o tem uma vers\u00e3o v\u00e1lida.";
        return false;
    }
    if (!StructurallyValid(candidate, error)) return false;
    config = std::move(candidate);
    return true;
}

} // namespace

std::filesystem::path ConfigPath() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size) {
        std::wstring directory(size, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(), size);
        if (length && length < size) {
            directory.resize(length);
            const std::filesystem::path path(directory);
            if (path.is_absolute()) return path / L"MacroPill Control" / L"config.ini";
        }
    }
    wchar_t directory[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, directory)))
        return std::filesystem::path(directory) / L"MacroPill Control" / L"config.ini";
    return {};
}

bool LoadConfig(Config& config, std::wstring& error) { return LoadConfig(ConfigPath(), config, error); }
bool SaveConfig(const Config& config, std::wstring& error) { return SaveConfig(ConfigPath(), config, error); }

bool LoadConfig(const std::filesystem::path& path, Config& config, std::wstring& error) {
    error.clear();
    if (path.empty()) { error = L"N\u00e3o foi poss\u00edvel localizar a pasta de configura\u00e7\u00e3o."; return false; }
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) { config = DefaultConfig(); return true; }
        error = L"N\u00e3o foi poss\u00edvel ler a configura\u00e7\u00e3o: " + WindowsError(code);
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        size.QuadPart > static_cast<LONGLONG>(MaxConfigBytes)) {
        error = L"O arquivo de configura\u00e7\u00e3o \u00e9 inv\u00e1lido ou muito grande.";
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())) {
        error = L"N\u00e3o foi poss\u00edvel ler toda a configura\u00e7\u00e3o.";
        return false;
    }
    std::wstring text;
    if (!DecodeUtf8(bytes, text)) { error = L"O arquivo de configura\u00e7\u00e3o n\u00e3o est\u00e1 em UTF-8 v\u00e1lido."; return false; }
    return ParseConfig(text, config, error);
}

bool SaveConfig(const std::filesystem::path& path, const Config& config, std::wstring& error) {
    error.clear();
    if (path.empty() || path.filename().empty()) { error = L"O caminho da configura\u00e7\u00e3o \u00e9 inv\u00e1lido."; return false; }
    if (!StructurallyValid(config, error)) return false;
    std::wstring text = L"; MacroPill Control - UTF-8. Backslashes and line breaks are escaped.\nversion=1\nport=" + Escape(config.port) +
        L"\nbaud=" + std::to_wstring(config.baud) + L"\nminimize_to_tray=" + (config.minimizeToTray ? L"1" : L"0") + L"\n";
    for (std::size_t index = 0; index < config.buttons.size(); ++index) {
        const auto& button = config.buttons[index];
        text += L"\n[button" + std::to_wstring(index + 1) + L"]\nlabel=" + Escape(button.label) +
            L"\ntype=" + std::to_wstring(static_cast<int>(button.type)) + L"\nvalue=" + Escape(button.value) + L"\n";
    }
    std::string bytes;
    if (!EncodeUtf8(text, bytes)) { error = L"A configura\u00e7\u00e3o cont\u00e9m texto Unicode inv\u00e1lido."; return false; }
    bytes.insert(0, "\xEF\xBB\xBF");
    if (bytes.size() > MaxConfigBytes) { error = L"A configura\u00e7\u00e3o \u00e9 muito grande."; return false; }
    std::error_code filesystemError;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) { error = L"N\u00e3o foi poss\u00edvel criar a pasta de configura\u00e7\u00e3o."; return false; }
    static std::atomic<unsigned> counter{0};
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        temporary = path;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" +
                     std::to_wstring(counter.fetch_add(1));
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    FileHandle file(handle);
    if (!file.valid()) { error = L"N\u00e3o foi poss\u00edvel criar o arquivo tempor\u00e1rio: " + WindowsError(GetLastError()); return false; }
    DWORD written = 0;
    bool success = WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE && written == bytes.size();
    if (success) success = FlushFileBuffers(file.get()) != FALSE;
    DWORD code = success ? ERROR_SUCCESS : GetLastError();
    if (!file.close() && success) { success = false; code = GetLastError(); }
    if (success && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        success = false;
        code = GetLastError();
    }
    if (!success) {
        DeleteFileW(temporary.c_str());
        error = L"N\u00e3o foi poss\u00edvel salvar a configura\u00e7\u00e3o: " + WindowsError(code);
        return false;
    }
    return true;
}

} // namespace macro
