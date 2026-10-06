// Runs the real Win32 controls in an invisible window, with isolated settings.
// No serial hardware, external application or injected keyboard input is used.
#define MACROPILL_UI_TEST
#include "../src/main.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}

struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        (L"MacroPill-ui-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Fixture() { Check(std::filesystem::create_directory(directory), "isolated test directory"); }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove(directory / L"config.ini", ignored);
        std::filesystem::remove(directory, ignored);
    }
};

HWND MakeWindow(App& app, const std::filesystem::path& settings) {
    app.instance = GetModuleHandleW(nullptr);
    app.settingsPath = settings;
    app.enableTray = false;
    HWND result = CreateWindowExW(0, L"MacroPillControl.HiddenUITest", L"MacroPill UI test",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 1120, 950, nullptr, nullptr, app.instance, &app);
    Check(result != nullptr, "create actual Win32 window and child controls");
    return result;
}

void ChangeType(App& app, size_t index, macro::ActionType type) {
    SendMessageW(app.cards[index].type, CB_SETCURSEL, static_cast<WPARAM>(type), 0);
    SendMessageW(app.window, WM_COMMAND,
        MAKEWPARAM(kFirstCard + static_cast<int>(index) * 10 + 1, CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(app.cards[index].type));
}

void SaveScreenshot(App& app, const std::filesystem::path& path) {
    SetWindowPos(app.window, nullptr, -20000, -20000, 0, 0,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);
    RECT client{};
    GetClientRect(app.window, &client);
    const int width = client.right, height = client.bottom;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    Check(bitmap && dc, "create screenshot surface");
    HGDIOBJ previous = SelectObject(dc, bitmap);
    const BOOL printed = PrintWindow(app.window, dc, PW_CLIENTONLY);
    if (!printed) SendMessageW(app.window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN);
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + width * height * 4;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(static_cast<const char*>(pixels), width * height * 4);
    Check(output.good(), "save real window screenshot");
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
}

void TestUI(const std::filesystem::path& settings, const std::filesystem::path& screenshot) {
    App app;
    MakeWindow(app, settings);
    try {
        Check(!app.dirty && !app.serial.IsConnected(), "startup clean and disconnected");
        Check(WindowText(app.cards[0].label) == L"Bot\u00e3o 1", "first default label");
        Check(WindowText(app.cards[7].label) == L"Bot\u00e3o 8", "eighth default label");
        for (size_t i = 0; i < app.cards.size(); ++i) {
            Check(SendMessageW(app.cards[i].type, CB_GETCOUNT, 0, 0) == 4, "four action choices per card");
            Check(!IsWindowEnabled(app.cards[i].value) && !IsWindowEnabled(app.cards[i].browse), "none action disables fields");
            Check(app.cards[i].bounds.right > app.cards[i].bounds.left, "card has nonzero layout width");
        }
        Check(SendMessageW(app.baud, CB_GETCOUNT, 0, 0) == 6, "baud choices");
        Check(WindowText(app.baud) == L"115200", "default baud is 115200");
        ChangeType(app, 0, macro::ActionType::Program);
        Check(IsWindowEnabled(app.cards[0].browse) && IsWindowEnabled(app.cards[0].value), "program enables picker");
        ChangeType(app, 0, macro::ActionType::Shortcut);
        Check(!IsWindowEnabled(app.cards[0].browse) && IsWindowEnabled(app.cards[0].test), "shortcut disables file picker");
        SetWindowTextW(app.cards[0].value, L"CTRL + SHIFT + M");
        app.Test(0);
        Check(app.pendingTest.has_value() && app.testAt > GetTickCount64(), "shortcut test is deferred");
        KillTimer(app.window, kTestTimer);
        app.pendingTest.reset();
        ChangeType(app, 0, macro::ActionType::Url);
        SetWindowTextW(app.cards[0].label, L"YouTube");
        SetWindowTextW(app.cards[0].value, L"https://www.youtube.com");
        SendMessageW(app.closeToTray, BM_SETCHECK, BST_UNCHECKED, 0);
        Check(app.dirty, "editing marks unsaved state");
        Check(app.Save(), "save eight UI configurations to isolated path");
        Check(!app.dirty, "save clears dirty state");
        macro::Config restored;
        std::wstring error;
        Check(macro::LoadConfig(settings, restored, error), "saved UI settings load");
        Check(restored.buttons[0].label == L"YouTube" && restored.buttons[0].type == macro::ActionType::Url &&
              restored.buttons[0].value == L"https://www.youtube.com" && !restored.minimizeToTray, "form values persisted");
        // Never execute this configured URL: dispatch tests use inactive actions.
        for (size_t i = 0; i < app.cards.size(); ++i) ChangeType(app, i, macro::ActionType::None);
        app.SetNotice(L"sentinel");
        app.OnSerial(app.generation + 1, std::make_unique<macro::SerialEvent>(macro::SerialEvent{macro::SerialEvent::Kind::Button, 1, {}}));
        Check(app.notice == L"sentinel", "stale reader generation ignored");
        for (int button = 1; button <= 8; ++button) {
            app.OnSerial(app.generation, std::make_unique<macro::SerialEvent>(macro::SerialEvent{macro::SerialEvent::Kind::Button, button, {}}));
            Check(app.notice.find(L"BTN" + std::to_wstring(button)) == 0, "button signal reaches correct card");
            Check(app.cards[button - 1].pulseUntil > GetTickCount64(), "received button gets visual feedback");
        }
        for (auto& card : app.cards) card.pulseUntil = 0;
        KillTimer(app.window, kPulseTimer);
        SetWindowPos(app.window, nullptr, 0, 0, 720, 650, SWP_NOZORDER | SWP_NOACTIVATE);
        Check(app.cards[1].bounds.top > app.cards[0].bounds.top, "narrow layout becomes one column");
        app.Scroll(SB_BOTTOM);
        Check(app.scroll > 0, "small window scrolls to lower cards");
        app.Scroll(SB_TOP);
        Check(app.scroll == 0, "scroll returns to top");
        SetWindowPos(app.window, nullptr, 0, 0, 1120, 950, SWP_NOZORDER | SWP_NOACTIVATE);
        Check(app.cards[1].bounds.top == app.cards[0].bounds.top, "wide layout becomes two columns");
        if (!screenshot.empty()) {
            ChangeType(app, 0, macro::ActionType::Program);
            SetWindowTextW(app.cards[0].label, L"Bloco de Notas");
            SetWindowTextW(app.cards[0].value, L"C:\\Windows\\System32\\notepad.exe");
            ChangeType(app, 1, macro::ActionType::Shortcut);
            SetWindowTextW(app.cards[1].label, L"Microfone");
            SetWindowTextW(app.cards[1].value, L"CTRL + SHIFT + M");
            ChangeType(app, 2, macro::ActionType::Url);
            SetWindowTextW(app.cards[2].label, L"YouTube");
            SetWindowTextW(app.cards[2].value, L"https://www.youtube.com");
            app.SetNotice(L"Prévia da interface • configure cada botão e salve.");
            SaveScreenshot(app, screenshot);
        }
        app.dirty = false;
        SendMessageW(app.window, WM_CLOSE, 0, 0);
        Check(!IsWindow(app.window), "exit option destroys window cleanly");
    } catch (...) {
        if (IsWindow(app.window)) DestroyWindow(app.window);
        throw;
    }
    MSG quit{};
    while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}
} // namespace

int main(int argc, char** argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"MacroPillControl.HiddenUITest";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    int result = 1;
    try {
        Check(RegisterClassW(&cls) != 0, "register isolated test window class");
        Fixture fixture;
        TestUI(fixture.directory / L"config.ini", argc == 2 ? std::filesystem::u8path(argv[1]) : std::filesystem::path{});
        // Verify startup restoration with the real Initialize() path.
        App restored;
        MakeWindow(restored, fixture.directory / L"config.ini");
        Check(WindowText(restored.cards[0].label) == L"YouTube", "next startup restores persisted label");
        Check(WindowText(restored.cards[0].value) == L"https://www.youtube.com", "next startup restores action value");
        DestroyWindow(restored.window);
        result = 0;
        std::cout << "Passed " << checks << " Win32 UI assertions.\n";
    } catch (const std::exception& error) {
        std::cerr << "UI test failed: " << error.what() << '\n';
    }
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
