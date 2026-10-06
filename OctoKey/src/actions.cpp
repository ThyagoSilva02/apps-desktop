#include "actions.h"

#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>

namespace macro {
namespace {

std::wstring Trim(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring UpperAscii(std::wstring text) {
    for (auto& character : text)
        if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
    return text;
}

bool IsModifier(WORD key) {
    return key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || key == VK_LWIN;
}

WORD TokenKey(const std::wstring& token) {
    if (token == L"CTRL" || token == L"CONTROL") return VK_CONTROL;
    if (token == L"SHIFT") return VK_SHIFT;
    if (token == L"ALT") return VK_MENU;
    if (token == L"WIN" || token == L"WINDOWS") return VK_LWIN;
    if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') ||
                             (token[0] >= L'0' && token[0] <= L'9')))
        return static_cast<WORD>(token[0]);
    if (token.size() >= 2 && token.size() <= 3 && token[0] == L'F') {
        unsigned number = 0;
        for (std::size_t index = 1; index < token.size(); ++index) {
            if (token[index] < L'0' || token[index] > L'9') return 0;
            number = number * 10 + static_cast<unsigned>(token[index] - L'0');
        }
        if (number >= 1 && number <= 24) return static_cast<WORD>(VK_F1 + number - 1);
    }
    struct Alias { const wchar_t* name; WORD key; };
    static const Alias aliases[] = {
        {L"ENTER", VK_RETURN}, {L"RETURN", VK_RETURN},
        {L"SPACE", VK_SPACE}, {L"SPACEBAR", VK_SPACE}, {L"TAB", VK_TAB},
        {L"ESC", VK_ESCAPE}, {L"ESCAPE", VK_ESCAPE},
        {L"BACKSPACE", VK_BACK}, {L"BACK", VK_BACK},
        {L"DELETE", VK_DELETE}, {L"DEL", VK_DELETE}, {L"INSERT", VK_INSERT}, {L"INS", VK_INSERT},
        {L"LEFT", VK_LEFT}, {L"ARROWLEFT", VK_LEFT},
        {L"RIGHT", VK_RIGHT}, {L"ARROWRIGHT", VK_RIGHT},
        {L"UP", VK_UP}, {L"ARROWUP", VK_UP},
        {L"DOWN", VK_DOWN}, {L"ARROWDOWN", VK_DOWN},
        {L"HOME", VK_HOME}, {L"END", VK_END},
        {L"PGUP", VK_PRIOR}, {L"PAGEUP", VK_PRIOR},
        {L"PGDN", VK_NEXT}, {L"PAGEDOWN", VK_NEXT}
    };
    for (const auto& alias : aliases)
        if (token == alias.name) return alias.key;
    return 0;
}

bool IsExtendedKey(WORD key) {
    switch (key) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
    case VK_UP: case VK_DOWN: case VK_LWIN: case VK_RWIN:
        return true;
    default: return false;
    }
}

INPUT KeyInput(WORD key, bool release) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = (release ? KEYEVENTF_KEYUP : 0) |
                       (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0);
    return input;
}

bool HasForbiddenCharacters(const std::wstring& value) {
    return std::any_of(value.begin(), value.end(), [](wchar_t character) {
        return character < 32 || character == 127;
    });
}

std::wstring WindowsError(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    const std::wstring message = length ? Trim(std::wstring(buffer, length)) : L"Erro " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return message;
}

} // namespace

bool ParseShortcut(const std::wstring& text, std::vector<WORD>& keys, std::wstring& error) {
    keys.clear();
    error.clear();
    if (text.empty() || text.size() > 256 || text.find(L'\0') != std::wstring::npos) {
        error = L"Informe um atalho, por exemplo CTRL+SHIFT+M.";
        return false;
    }
    std::vector<WORD> parsed;
    WORD mainKey = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto separator = text.find(L'+', start);
        const auto count = separator == std::wstring::npos ? std::wstring::npos : separator - start;
        const std::wstring token = UpperAscii(Trim(text.substr(start, count)));
        const WORD key = TokenKey(token);
        if (!key) {
            error = token.empty() ? L"O atalho tem uma tecla vazia entre os sinais +." :
                L"Tecla desconhecida: " + token + L". Use letras, n\u00fameros, F1 a F24 ou nomes como ENTER e SPACE.";
            return false;
        }
        if (std::find(parsed.begin(), parsed.end(), key) != parsed.end()) {
            error = L"O atalho repete a mesma tecla: " + token + L".";
            return false;
        }
        if (!IsModifier(key)) {
            if (mainKey) {
                error = L"Use uma tecla principal e, se quiser, CTRL, SHIFT, ALT ou WIN.";
                return false;
            }
            mainKey = key;
        }
        parsed.push_back(key);
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }
    if (!mainKey) {
        error = L"Acrescente uma tecla principal, por exemplo CTRL+C.";
        return false;
    }
    for (const WORD modifier : { static_cast<WORD>(VK_CONTROL), static_cast<WORD>(VK_SHIFT),
                                 static_cast<WORD>(VK_MENU), static_cast<WORD>(VK_LWIN) })
        if (std::find(parsed.begin(), parsed.end(), modifier) != parsed.end()) keys.push_back(modifier);
    keys.push_back(mainKey);
    return true;
}

std::vector<INPUT> BuildShortcutInputs(const std::vector<WORD>& keys) {
    std::vector<INPUT> inputs;
    inputs.reserve(keys.size() * 2);
    for (const WORD key : keys) inputs.push_back(KeyInput(key, false));
    for (auto key = keys.rbegin(); key != keys.rend(); ++key) inputs.push_back(KeyInput(*key, true));
    return inputs;
}

bool ValidateAction(const ButtonConfig& button, std::wstring& error) {
    error.clear();
    if (button.label.size() > 128 || button.label.find(L'\0') != std::wstring::npos) {
        error = L"O nome do bot\u00e3o deve ter at\u00e9 128 caracteres.";
        return false;
    }
    if (button.value.size() > 32767 || button.value.find(L'\0') != std::wstring::npos) {
        error = L"O valor da a\u00e7\u00e3o \u00e9 inv\u00e1lido ou muito longo.";
        return false;
    }
    switch (button.type) {
    case ActionType::None: return true;
    case ActionType::Shortcut: {
        std::vector<WORD> keys;
        return ParseShortcut(button.value, keys, error);
    }
    case ActionType::Program: {
        if (button.value.empty() || HasForbiddenCharacters(button.value)) {
            error = L"Escolha o caminho de um programa ou arquivo.";
            return false;
        }
        const DWORD attributes = GetFileAttributesW(button.value.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            error = L"O programa ou arquivo n\u00e3o foi encontrado. Escolha um arquivo existente.";
            return false;
        }
        return true;
    }
    case ActionType::Url: {
        const std::wstring uppercase = UpperAscii(button.value);
        std::size_t schemeLength = 0;
        if (uppercase.compare(0, 7, L"HTTP://") == 0) schemeLength = 7;
        if (uppercase.compare(0, 8, L"HTTPS://") == 0) schemeLength = 8;
        if (!schemeLength || HasForbiddenCharacters(button.value) ||
            button.value.find_first_of(L" \t\"\\") != std::wstring::npos) {
            error = L"Informe um endere\u00e7o completo come\u00e7ando com http:// ou https://, sem espa\u00e7os.";
            return false;
        }
        const std::size_t hostEnd = button.value.find_first_of(L"/?#", schemeLength);
        if ((hostEnd == std::wstring::npos ? button.value.size() : hostEnd) == schemeLength) {
            error = L"O endere\u00e7o precisa ter um servidor depois de http:// ou https://.";
            return false;
        }
        return true;
    }
    default:
        error = L"Tipo de a\u00e7\u00e3o desconhecido.";
        return false;
    }
}

bool ExecuteAction(const ButtonConfig& button, HWND owner, std::wstring& error) {
    if (!ValidateAction(button, error)) return false;
    if (button.type == ActionType::None) return true;
    if (button.type == ActionType::Shortcut) {
        for (const int modifier : { VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN }) {
            if (GetAsyncKeyState(modifier) & 0x8000) {
                error = L"Solte CTRL, SHIFT, ALT e WIN antes de executar este atalho.";
                return false;
            }
        }
        std::vector<WORD> keys;
        if (!ParseShortcut(button.value, keys, error)) return false;
        std::vector<INPUT> inputs = BuildShortcutInputs(keys);
        SetLastError(ERROR_SUCCESS);
        const UINT inserted = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        const DWORD code = GetLastError();
        if (inserted == inputs.size()) return true;
        // Only release key-downs that were accepted and have not been released yet.
        const std::size_t downCount = std::min<std::size_t>(inserted, keys.size());
        const std::size_t releasedCount = inserted > keys.size() ? inserted - keys.size() : 0;
        std::vector<INPUT> release;
        for (std::size_t index = downCount - std::min(downCount, releasedCount); index > 0; --index)
            release.push_back(KeyInput(keys[index - 1], true));
        if (!release.empty()) SendInput(static_cast<UINT>(release.size()), release.data(), sizeof(INPUT));
        error = L"O Windows n\u00e3o aceitou o atalho completo. Uma janela aberta como administrador pode bloquear "
                L"atalhos de um programa sem a mesma permiss\u00e3o.";
        if (code != ERROR_SUCCESS) error += L" " + WindowsError(code);
        return false;
    }
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI;
    execute.hwnd = owner;
    execute.lpVerb = L"open";
    execute.lpFile = button.value.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&execute)) return true;
    error = L"N\u00e3o foi poss\u00edvel abrir a a\u00e7\u00e3o: " + WindowsError(GetLastError());
    return false;
}

} // namespace macro
