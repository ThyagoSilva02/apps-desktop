#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "model.h"
#include "config.h"
#include "actions.h"
#include "serial.h"

namespace {
constexpr wchar_t kClassName[] = L"MacroPillControl.MainWindow";
constexpr UINT kSerialMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kRestoreMessage = WM_APP + 3;
constexpr UINT_PTR kPulseTimer = 1;
constexpr UINT_PTR kTestTimer = 2;
constexpr UINT kTrayId = 1;
constexpr int kPort = 101, kRefresh = 102, kBaud = 103, kConnect = 104;
constexpr int kSave = 105, kHide = 106, kCloseToTray = 107;
constexpr int kFirstCard = 1000;
constexpr COLORREF kBackground = RGB(245, 247, 251);
constexpr COLORREF kText = RGB(27, 38, 59);
constexpr COLORREF kMuted = RGB(91, 105, 126);
constexpr COLORREF kAccent = RGB(38, 99, 220);
constexpr COLORREF kBorder = RGB(218, 225, 237);
constexpr std::array<unsigned, 6> kBaudRates{9600, 19200, 38400, 57600, 115200, 230400};

std::wstring WindowText(HWND window) {
    const int count = GetWindowTextLengthW(window);
    std::wstring result(static_cast<size_t>(count) + 1, L'\0');
    GetWindowTextW(window, result.data(), count + 1);
    result.resize(static_cast<size_t>(count));
    return result;
}

HICON CreateBrandIcon(int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    auto* pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int gridX = x * 32 / size, gridY = y * 32 / size;
            const bool cell = gridX >= 5 && gridX < 27 && gridY >= 5 && gridY < 27 &&
                              ((gridX - 5) % 6 < 4) && ((gridY - 5) % 12 < 9);
            pixels[y * size + x] = cell ? 0xFFE6EEFF : 0xFF2663DC;
        }
    }
    const size_t maskStride = (static_cast<size_t>(size) + 15) / 16 * 2;
    std::vector<BYTE> maskBits(maskStride * static_cast<size_t>(size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = color;
    iconInfo.hbmMask = mask;
    HICON result = mask ? CreateIconIndirect(&iconInfo) : nullptr;
    if (mask) DeleteObject(mask);
    DeleteObject(color);
    return result;
}

struct Card {
    HWND label{}, type{}, value{}, browse{}, test{}, hint{};
    RECT bounds{};
    ULONGLONG pulseUntil{};
};

class App {
public:
    HINSTANCE instance{};
    HWND window{};
    macro::Config config = macro::DefaultConfig();
    std::filesystem::path settingsPath = macro::ConfigPath();
    macro::SerialConnection serial;
    std::array<Card, 8> cards{};
    HWND port{}, refresh{}, baud{}, connect{}, save{}, hide{}, closeToTray{};
    HWND status{};
    HBRUSH background = CreateSolidBrush(kBackground);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    HFONT font{}, titleFont{}, headingFont{}, smallFont{};
    HICON icon{}, smallIcon{};
    UINT dpi = 96, explorerRestart{};
    int scroll = 0, contentHeight = 0;
    bool initializing = true, dirty = false, trayAdded = false;
    bool enableTray = true;
    bool statusError = false;
    std::atomic<bool> closing{false};
    ULONG_PTR generation = 0;
    std::wstring notice = L"Escolha a porta da placa e clique em Conectar.";
    std::optional<macro::ButtonConfig> pendingTest;
    ULONGLONG testAt{};

    ~App() {
        serial.Disconnect();
        ClearFonts();
        if (icon) DestroyIcon(icon);
        if (smallIcon) DestroyIcon(smallIcon);
        DeleteObject(background);
        DeleteObject(white);
    }

    int Px(int logical) const { return MulDiv(logical, static_cast<int>(dpi), 96); }
    int Logical(int pixels) const { return MulDiv(pixels, 96, static_cast<int>(dpi)); }

    HWND Control(const wchar_t* className, const wchar_t* text, DWORD style, int id) {
        if (std::wstring(className) == WC_BUTTONW && (style & BS_TYPEMASK) == BS_PUSHBUTTON)
            style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
        HWND result = CreateWindowExW(0, className, text,
            WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (result) SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return result;
    }

    void ClearFonts() {
        for (HFONT f : {font, titleFont, headingFont, smallFont}) if (f) DeleteObject(f);
        font = titleFont = headingFont = smallFont = nullptr;
    }

    void MakeFonts() {
        ClearFonts();
        auto create = [this](int height, int weight) {
            return CreateFontW(-Px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        font = create(14, FW_NORMAL);
        titleFont = create(28, FW_SEMIBOLD);
        headingFont = create(15, FW_SEMIBOLD);
        smallFont = create(12, FW_NORMAL);
        EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(data), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font));
        for (auto& card : cards) if (card.hint)
            SendMessageW(card.hint, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    }

    bool Initialize() {
        dpi = GetDpiForWindow(window);
        if (!dpi) dpi = 96;
        MakeFonts();
        icon = CreateBrandIcon(64);
        smallIcon = CreateBrandIcon(32);
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        std::wstring error;
        if (!macro::LoadConfig(settingsPath, config, error) && !error.empty()) {
            notice = L"Não foi possível carregar a configuração: " + error;
            statusError = true;
        }
        port = Control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kPort);
        refresh = Control(WC_BUTTONW, L"Atualizar", BS_PUSHBUTTON | WS_TABSTOP, kRefresh);
        baud = Control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kBaud);
        for (size_t i = 0; i < kBaudRates.size(); ++i) {
            const std::wstring text = std::to_wstring(kBaudRates[i]);
            SendMessageW(baud, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (config.baud == kBaudRates[i]) SendMessageW(baud, CB_SETCURSEL, i, 0);
        }
        if (SendMessageW(baud, CB_GETCURSEL, 0, 0) == CB_ERR)
            SendMessageW(baud, CB_SETCURSEL, 4, 0);
        connect = Control(WC_BUTTONW, L"Conectar", BS_PUSHBUTTON | WS_TABSTOP, kConnect);
        status = Control(WC_STATICW, notice.c_str(), SS_LEFT | SS_NOPREFIX, 110);
        for (size_t i = 0; i < cards.size(); ++i) {
            Card& card = cards[i];
            const int base = kFirstCard + static_cast<int>(i) * 10;
            const auto& button = config.buttons[i];
            card.label = Control(WC_EDITW, button.label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, base);
            SendMessageW(card.label, EM_SETLIMITTEXT, 64, 0);
            card.type = Control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, base + 1);
            for (const wchar_t* action : {L"Sem ação", L"Abrir programa / arquivo", L"Atalho de teclado", L"Abrir URL"})
                SendMessageW(card.type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(action));
            SendMessageW(card.type, CB_SETCURSEL, static_cast<WPARAM>(button.type), 0);
            card.value = Control(WC_EDITW, button.value.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, base + 2);
            SendMessageW(card.value, EM_SETLIMITTEXT, 4096, 0);
            card.browse = Control(WC_BUTTONW, L"...", BS_PUSHBUTTON | WS_TABSTOP, base + 3);
            card.test = Control(WC_BUTTONW, L"Testar", BS_PUSHBUTTON | WS_TABSTOP, base + 4);
            card.hint = Control(WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, base + 5);
            SendMessageW(card.hint, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
            UpdateType(i);
        }
        save = Control(WC_BUTTONW, L"Salvar configurações", BS_PUSHBUTTON | WS_TABSTOP, kSave);
        hide = Control(WC_BUTTONW, L"Minimizar para bandeja", BS_PUSHBUTTON | WS_TABSTOP, kHide);
        closeToTray = Control(WC_BUTTONW, L"Ao fechar, continuar na bandeja", BS_AUTOCHECKBOX | WS_TABSTOP, kCloseToTray);
        SendMessageW(closeToTray, BM_SETCHECK, config.minimizeToTray ? BST_CHECKED : BST_UNCHECKED, 0);
        explorerRestart = RegisterWindowMessageW(L"TaskbarCreated");
        if (enableTray) AddTray();
        RefreshPorts();
        initializing = false;
        Layout();
        UpdateConnectionControls();
        return port && refresh && baud && connect && save && hide && closeToTray && status;
    }

    void Position(HWND control, int x, int y, int width, int height) {
        if (control) MoveWindow(control, Px(x), Px(y) - scroll, Px(width), Px(height), TRUE);
    }

    void Layout() {
        RECT client{};
        GetClientRect(window, &client);
        const int width = Logical(client.right);
        const int columns = width >= 900 ? 2 : 1;
        const int gap = 16, padding = 28;
        const int cardWidth = (width - padding * 2 - (columns - 1) * gap) / columns;
        const int rows = static_cast<int>(cards.size()) / columns;
        const int footer = 186 + rows * 158 + 4;
        contentHeight = Px(footer + 94);
        scroll = std::clamp(scroll, 0, std::max(0, contentHeight - static_cast<int>(client.bottom)));
        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scrollInfo.nMin = 0;
        scrollInfo.nMax = contentHeight - 1;
        scrollInfo.nPage = static_cast<UINT>(client.bottom);
        scrollInfo.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &scrollInfo, TRUE);
        Position(port, 28, 108, 128, 260);
        Position(refresh, 166, 108, 94, 30);
        Position(baud, 280, 108, 112, 260);
        Position(connect, 408, 108, 130, 30);
        Position(status, 28, 146, width - 56, 24);
        for (size_t i = 0; i < cards.size(); ++i) {
            auto& card = cards[i];
            const int x = padding + static_cast<int>(i % columns) * (cardWidth + gap);
            const int y = 186 + static_cast<int>(i / columns) * 158;
            card.bounds = {Px(x), Px(y), Px(x + cardWidth), Px(y + 144)};
            Position(card.label, x + 56, y + 14, cardWidth - 126, 26);
            Position(card.type, x + 16, y + 50, cardWidth - 32, 250);
            Position(card.value, x + 16, y + 84, cardWidth - 139, 28);
            Position(card.browse, x + cardWidth - 114, y + 84, 36, 28);
            Position(card.test, x + cardWidth - 72, y + 84, 56, 28);
            Position(card.hint, x + 16, y + 118, cardWidth - 32, 20);
        }
        Position(save, 28, footer, 184, 32);
        Position(hide, 224, footer, 202, 32);
        Position(closeToTray, 28, footer + 44, width - 56, 24);
        InvalidateRect(window, nullptr, TRUE);
    }

    void DrawTextAt(HDC dc, const std::wstring& text, RECT rect, HFONT textFont,
                    COLORREF color, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
        HGDIOBJ old = SelectObject(dc, textFont);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
        SelectObject(dc, old);
    }

    void PaintTo(HDC dc) {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, background);
        SetViewportOrgEx(dc, 0, -scroll, nullptr);
        DrawTextAt(dc, L"MacroPill Control", {Px(28), Px(20), client.right - Px(28), Px(58)}, titleFont, kText);
        DrawTextAt(dc, L"Oito botões. Seus programas, atalhos e links em um toque.",
            {Px(28), Px(60), client.right - Px(28), Px(82)}, font, kMuted);
        DrawTextAt(dc, L"Porta COM", {Px(28), Px(88), Px(156), Px(106)}, smallFont, kMuted);
        DrawTextAt(dc, L"Velocidade (baud)", {Px(280), Px(88), Px(406), Px(106)}, smallFont, kMuted);
        if (Logical(client.right) >= 680) {
            DrawTextAt(dc, serial.IsConnected() ? L"● Conectado" : L"○ Desconectado",
                {Px(558), Px(108), client.right - Px(28), Px(138)}, headingFont,
                serial.IsConnected() ? RGB(20, 128, 90) : kMuted);
        }
        const ULONGLONG now = GetTickCount64();
        for (size_t i = 0; i < cards.size(); ++i) {
            const auto& card = cards[i];
            const bool active = card.pulseUntil > now;
            HPEN pen = CreatePen(PS_SOLID, Px(active ? 2 : 1), active ? kAccent : kBorder);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, white);
            RoundRect(dc, card.bounds.left, card.bounds.top, card.bounds.right, card.bounds.bottom, Px(14), Px(14));
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            RECT numberRect{card.bounds.left + Px(16), card.bounds.top + Px(14),
                card.bounds.left + Px(46), card.bounds.top + Px(42)};
            HBRUSH numberBrush = CreateSolidBrush(active ? kAccent : RGB(234, 240, 254));
            FillRect(dc, &numberRect, numberBrush);
            DeleteObject(numberBrush);
            DrawTextAt(dc, std::to_wstring(i + 1), numberRect, headingFont,
                active ? RGB(255, 255, 255) : kAccent, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            DrawTextAt(dc, L"BTN" + std::to_wstring(i + 1),
                {card.bounds.right - Px(64), card.bounds.top + Px(14), card.bounds.right - Px(16), card.bounds.top + Px(42)},
                smallFont, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        SetViewportOrgEx(dc, 0, 0, nullptr);
    }

    void Paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        PaintTo(dc);
        EndPaint(window, &ps);
    }

    void DrawButton(const DRAWITEMSTRUCT& item) {
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool primary = item.CtlID == kSave || item.CtlID == kConnect;
        const COLORREF fill = disabled ? RGB(247, 248, 250) :
            primary ? (pressed ? RGB(24, 76, 183) : kAccent) :
            pressed ? RGB(232, 238, 248) : RGB(255, 255, 255);
        const COLORREF border = primary && !disabled ? fill : kBorder;
        const int saved = SaveDC(item.hDC);
        FillRect(item.hDC, &item.rcItem, background);
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        SelectObject(item.hDC, brush);
        SelectObject(item.hDC, pen);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
            item.rcItem.right - 1, item.rcItem.bottom - 1, Px(8), Px(8));
        RECT labelRect = item.rcItem;
        InflateRect(&labelRect, -Px(3), -Px(2));
        DrawTextAt(item.hDC, WindowText(item.hwndItem), labelRect, font,
            disabled ? RGB(155, 163, 176) : primary ? RGB(255, 255, 255) : kText,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focusRect = item.rcItem;
            InflateRect(&focusRect, -Px(4), -Px(4));
            DrawFocusRect(item.hDC, &focusRect);
        }
        RestoreDC(item.hDC, saved);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void SetNotice(const std::wstring& message, bool error = false) {
        notice = message;
        statusError = error;
        if (status) {
            SetWindowTextW(status, notice.c_str());
            InvalidateRect(status, nullptr, TRUE);
        }
        if (error && !IsWindowVisible(window) && trayAdded) {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = kTrayId;
            data.uFlags = NIF_INFO;
            lstrcpynW(data.szInfoTitle, L"MacroPill Control", ARRAYSIZE(data.szInfoTitle));
            lstrcpynW(data.szInfo, message.c_str(), ARRAYSIZE(data.szInfo));
            data.dwInfoFlags = NIIF_ERROR;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
    }

    void MarkDirty() {
        if (initializing || dirty) return;
        dirty = true;
        SetWindowTextW(window, L"MacroPill Control — alterações não salvas");
    }

    void ReadForm() {
        for (size_t i = 0; i < cards.size(); ++i) {
            config.buttons[i].label = WindowText(cards[i].label);
            const LRESULT selection = SendMessageW(cards[i].type, CB_GETCURSEL, 0, 0);
            config.buttons[i].type = selection >= 0 && selection <= 3
                ? static_cast<macro::ActionType>(selection) : macro::ActionType::None;
            config.buttons[i].value = WindowText(cards[i].value);
        }
        config.port = WindowText(port);
        const LRESULT speed = SendMessageW(baud, CB_GETCURSEL, 0, 0);
        if (speed >= 0 && static_cast<size_t>(speed) < kBaudRates.size())
            config.baud = kBaudRates[static_cast<size_t>(speed)];
        config.minimizeToTray = SendMessageW(closeToTray, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void UpdateType(size_t index) {
        const LRESULT type = SendMessageW(cards[index].type, CB_GETCURSEL, 0, 0);
        const wchar_t* hint = L"Nenhuma ação será executada para esta tecla.";
        const wchar_t* cue = L"Escolha o tipo de ação acima";
        if (type == 1) { hint = L"Use ... para selecionar um programa ou arquivo."; cue = L"Caminho do programa ou arquivo"; }
        if (type == 2) { hint = L"Ex.: CTRL + SHIFT + M • Testar aguarda 3 segundos."; cue = L"CTRL + SHIFT + M"; }
        if (type == 3) { hint = L"Informe um endereço começando com https:// ou http://"; cue = L"https://youtube.com"; }
        SetWindowTextW(cards[index].hint, hint);
        SendMessageW(cards[index].value, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(cue));
        EnableWindow(cards[index].value, type != 0);
        EnableWindow(cards[index].browse, type == 1);
        EnableWindow(cards[index].test, type != 0);
    }

    void RefreshPorts() {
        const std::wstring previous = WindowText(port).empty() ? config.port : WindowText(port);
        SendMessageW(port, CB_RESETCONTENT, 0, 0);
        const auto ports = macro::EnumerateSerialPorts();
        for (size_t i = 0; i < ports.size(); ++i) {
            SendMessageW(port, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ports[i].c_str()));
            if (ports[i] == previous) SendMessageW(port, CB_SETCURSEL, i, 0);
        }
        if (SendMessageW(port, CB_GETCURSEL, 0, 0) == CB_ERR && !ports.empty())
            SendMessageW(port, CB_SETCURSEL, 0, 0);
        if (!initializing) {
            SetNotice(ports.empty() ? L"Nenhuma porta COM encontrada. Conecte a placa e clique em Atualizar."
                                   : L"Lista de portas atualizada. Escolha a porta da placa.");
        } else if (ports.empty() && !statusError) {
            SetNotice(L"Nenhuma porta COM encontrada. Conecte a placa e clique em Atualizar.");
        }
        UpdateConnectionControls();
    }

    void UpdateConnectionControls() {
        const bool connected = serial.IsConnected();
        SetWindowTextW(connect, connected ? L"Desconectar" : L"Conectar");
        EnableWindow(port, !connected);
        EnableWindow(baud, !connected);
        EnableWindow(refresh, !connected);
        EnableWindow(connect, connected || SendMessageW(port, CB_GETCURSEL, 0, 0) != CB_ERR);
        EnableWindow(hide, trayAdded);
        InvalidateRect(window, nullptr, FALSE);
        if (trayAdded) {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = kTrayId;
            data.uFlags = NIF_TIP;
            const std::wstring tooltip = connected ? L"MacroPill Control — " + config.port : L"MacroPill Control — desconectado";
            lstrcpynW(data.szTip, tooltip.c_str(), ARRAYSIZE(data.szTip));
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
    }

    void ToggleConnection() {
        if (serial.IsConnected()) {
            ++generation;
            serial.Disconnect();
            SetNotice(L"Desconectado. Suas configurações continuam disponíveis.");
        } else {
            ReadForm();
            if (config.port.empty()) { SetNotice(L"Selecione uma porta COM.", true); return; }
            std::wstring error;
            const ULONG_PTR currentGeneration = ++generation;
            const HWND target = window;
            const bool result = serial.Connect(config.port, config.baud,
                [this, currentGeneration, target](const macro::SerialEvent& event) {
                    if (closing.load()) return;
                    auto message = std::make_unique<macro::SerialEvent>(event);
                    if (PostMessageW(target, kSerialMessage, currentGeneration, reinterpret_cast<LPARAM>(message.get())))
                        message.release();
                }, error);
            if (!result) SetNotice(L"Falha ao conectar: " + error, true);
            else SetNotice(L"Conectado a " + config.port + L". Aguardando BTN1 a BTN8.");
        }
        UpdateConnectionControls();
    }

    bool Save(bool announce = true) {
        ReadForm();
        std::wstring error;
        for (size_t i = 0; i < config.buttons.size(); ++i) {
            if (!macro::ValidateAction(config.buttons[i], error)) {
                SetNotice(L"Botão " + std::to_wstring(i + 1) + L": " + error, true);
                RevealCard(i);
                SetFocus(cards[i].value);
                return false;
            }
        }
        if (!macro::SaveConfig(settingsPath, config, error)) { SetNotice(L"Falha ao salvar: " + error, true); return false; }
        dirty = false;
        SetWindowTextW(window, L"MacroPill Control");
        if (announce) SetNotice(L"Configurações salvas. Serão restauradas na próxima abertura.");
        return true;
    }

    void RevealCard(size_t index) {
        RECT client{};
        GetClientRect(window, &client);
        const RECT bounds = cards[index].bounds;
        if (bounds.top < scroll || bounds.bottom > scroll + client.bottom) {
            scroll = std::max(0L, bounds.top - Px(20));
            Layout();
        }
    }

    void Browse(size_t index) {
        std::vector<wchar_t> path(32768, 0);
        const std::wstring current = WindowText(cards[index].value);
        if (current.size() < path.size()) std::copy(current.begin(), current.end(), path.begin());
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"Programas (*.exe)\0*.exe\0Todos os arquivos (*.*)\0*.*\0\0";
        dialog.lpstrFile = path.data();
        dialog.nMaxFile = static_cast<DWORD>(path.size());
        dialog.lpstrTitle = L"Escolha o programa ou arquivo";
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            SetWindowTextW(cards[index].value, path.data());
            MarkDirty();
        } else {
            const DWORD error = CommDlgExtendedError();
            if (error) SetNotice(L"Não foi possível abrir a seleção de arquivo (erro " + std::to_wstring(error) + L").", true);
        }
    }

    void RunAction(const macro::ButtonConfig& button, const std::wstring& description) {
        std::wstring error;
        if (!macro::ExecuteAction(button, window, error)) SetNotice(description + L": " + error, true);
        else if (button.type == macro::ActionType::None) SetNotice(description + L" recebido; nenhuma ação configurada.");
        else SetNotice(description + L" — ação executada.");
    }

    void Test(size_t index) {
        ReadForm();
        std::wstring error;
        if (!macro::ValidateAction(config.buttons[index], error)) { SetNotice(error, true); return; }
        if (config.buttons[index].type == macro::ActionType::Shortcut) {
            pendingTest = config.buttons[index];
            testAt = GetTickCount64() + 3000;
            SetTimer(window, kTestTimer, 100, nullptr);
            SetNotice(L"Atalho em 3 segundos. Dê foco à janela que deve receber as teclas.");
        } else RunAction(config.buttons[index], L"Teste do botão " + std::to_wstring(index + 1));
    }

    void OnSerial(ULONG_PTR eventGeneration, std::unique_ptr<macro::SerialEvent> event) {
        if (eventGeneration != generation || closing.load()) return;
        if (event->kind == macro::SerialEvent::Kind::Disconnected) {
            SetNotice(event->message.empty() ? L"A conexão com a placa foi interrompida." : event->message, true);
            UpdateConnectionControls();
            return;
        }
        if (event->button < 1 || event->button > 8) return;
        const size_t index = static_cast<size_t>(event->button - 1);
        ReadForm();
        cards[index].pulseUntil = GetTickCount64() + 450;
        SetTimer(window, kPulseTimer, 60, nullptr);
        InvalidateRect(window, nullptr, FALSE);
        RunAction(config.buttons[index], L"BTN" + std::to_wstring(event->button));
    }

    void AddTray() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kTrayId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = smallIcon ? smallIcon : LoadIconW(nullptr, IDI_APPLICATION);
        lstrcpynW(data.szTip, L"MacroPill Control", ARRAYSIZE(data.szTip));
        trayAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (trayAdded) {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }
        if (hide) EnableWindow(hide, trayAdded);
    }

    void RemoveTray() {
        if (!trayAdded) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayAdded = false;
    }

    void Restore() {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }

    void Hide() {
        if (!trayAdded) { SetNotice(L"A bandeja do Windows não está disponível.", true); return; }
        ShowWindow(window, SW_HIDE);
    }

    void TrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Abrir MacroPill Control");
        AppendMenuW(menu, MF_STRING, 2, L"Salvar configurações");
        if (serial.IsConnected()) AppendMenuW(menu, MF_STRING, 3, L"Desconectar placa");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"Sair");
        SetMenuDefaultItem(menu, 1, FALSE);
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(window);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        PostMessageW(window, WM_NULL, 0, 0);
        if (command == 1) Restore();
        if (command == 2 && !Save()) Restore();
        if (command == 3) ToggleConnection();
        if (command == 4) Exit();
    }

    void Exit() {
        if (dirty) {
            Restore();
            const int answer = MessageBoxW(window, L"Existem alterações não salvas. Deseja salvar antes de sair?",
                L"MacroPill Control", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL || (answer == IDYES && !Save(false))) return;
        }
        closing.store(true);
        ++generation;
        serial.Disconnect();
        DestroyWindow(window);
    }

    void OnCommand(int id, int code) {
        if (id >= kFirstCard && id < kFirstCard + 80) {
            const size_t index = static_cast<size_t>((id - kFirstCard) / 10);
            const int field = (id - kFirstCard) % 10;
            if ((field == 0 || field == 2) && code == EN_CHANGE) MarkDirty();
            if (field == 1 && code == CBN_SELCHANGE) { UpdateType(index); MarkDirty(); }
            if (field == 3 && code == BN_CLICKED) Browse(index);
            if (field == 4 && code == BN_CLICKED) Test(index);
            return;
        }
        if ((id == kPort || id == kBaud) && code == CBN_SELCHANGE) MarkDirty();
        if (code != BN_CLICKED) return;
        if (id == kRefresh) RefreshPorts();
        if (id == kConnect) ToggleConnection();
        if (id == kSave) Save();
        if (id == kHide) Hide();
        if (id == kCloseToTray) { ReadForm(); MarkDirty(); }
    }

    void Scroll(int command, int position = 0) {
        RECT client{};
        GetClientRect(window, &client);
        if (command == SB_LINEUP) scroll -= Px(38);
        if (command == SB_LINEDOWN) scroll += Px(38);
        if (command == SB_PAGEUP) scroll -= client.bottom - Px(40);
        if (command == SB_PAGEDOWN) scroll += client.bottom - Px(40);
        if (command == SB_THUMBTRACK || command == SB_THUMBPOSITION) scroll = position;
        if (command == SB_TOP) scroll = 0;
        if (command == SB_BOTTOM) scroll = contentHeight;
        Layout();
    }

    LRESULT Message(UINT message, WPARAM wParam, LPARAM lParam) {
        if (explorerRestart && message == explorerRestart) { trayAdded = false; AddTray(); UpdateConnectionControls(); return 0; }
        switch (message) {
        case WM_CREATE: return Initialize() ? 0 : -1;
        case WM_SIZE: if (wParam != SIZE_MINIMIZED && port) Layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Px(660), Px(520)};
            return 0;
        }
        case WM_DPICHANGED: {
            dpi = HIWORD(wParam);
            const auto* rect = reinterpret_cast<const RECT*>(lParam);
            MakeFonts();
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_COMMAND: OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_PAINT: Paint(); return 0;
        case WM_PRINTCLIENT: PaintTo(reinterpret_cast<HDC>(wParam)); return 0;
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item && item->CtlType == ODT_BUTTON) { DrawButton(*item); return TRUE; }
            break;
        }
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND child = reinterpret_cast<HWND>(lParam);
            const int id = GetDlgCtrlID(child);
            SetTextColor(dc, child == status && statusError ? RGB(183, 49, 49) : kMuted);
            SetBkColor(dc, id >= kFirstCard ? RGB(255, 255, 255) : kBackground);
            return reinterpret_cast<LRESULT>(id >= kFirstCard ? white : background);
        }
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(window, SB_VERT, &info);
            Scroll(LOWORD(wParam), info.nTrackPos);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            scroll -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), Px(76), WHEEL_DELTA);
            Layout();
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE && config.minimizeToTray && trayAdded) { Hide(); return 0; }
            break;
        case WM_CLOSE:
            ReadForm();
            if (config.minimizeToTray && trayAdded) Hide(); else Exit();
            return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wParam) { if (dirty) Save(false); closing.store(true); serial.Disconnect(); RemoveTray(); }
            return 0;
        case WM_DESTROY: {
            closing.store(true);
            serial.Disconnect();
            RemoveTray();
            KillTimer(window, kPulseTimer);
            KillTimer(window, kTestTimer);
            MSG pending{};
            while (PeekMessageW(&pending, window, kSerialMessage, kSerialMessage, PM_REMOVE))
                delete reinterpret_cast<macro::SerialEvent*>(pending.lParam);
            PostQuitMessage(0);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kPulseTimer) {
                const ULONGLONG now = GetTickCount64();
                bool active = false;
                for (auto& card : cards) if (card.pulseUntil > now) active = true;
                if (!active) KillTimer(window, kPulseTimer);
                InvalidateRect(window, nullptr, FALSE);
            }
            if (wParam == kTestTimer && pendingTest) {
                const ULONGLONG now = GetTickCount64();
                if (now >= testAt) {
                    KillTimer(window, kTestTimer);
                    const auto action = std::move(*pendingTest);
                    pendingTest.reset();
                    RunAction(action, L"Teste do atalho");
                }
            }
            return 0;
        case kSerialMessage:
            OnSerial(static_cast<ULONG_PTR>(wParam), std::unique_ptr<macro::SerialEvent>(reinterpret_cast<macro::SerialEvent*>(lParam)));
            return 0;
        case kTrayMessage:
            if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) TrayMenu();
            else if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) Restore();
            return 0;
        case kRestoreMessage: Restore(); return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->Message(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

#ifndef MACROPILL_UI_TEST
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\MacroPillControl.Singleton");
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClassName, nullptr)) PostMessageW(existing, kRestoreMessage, 0, 0);
        else MessageBoxW(nullptr, L"O MacroPill Control já está em execução.", L"MacroPill Control", MB_OK | MB_ICONINFORMATION);
        CloseHandle(singleton);
        return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    App app;
    app.instance = instance;
    // An optional explicit settings path is useful for portable use and for
    // integration tests, without modifying the user's normal configuration.
    int argumentCount = 0;
    PWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    bool validArguments = arguments != nullptr;
    if (arguments && argumentCount > 1) {
        validArguments = argumentCount == 3 && std::wstring(arguments[1]) == L"--config";
        if (validArguments) {
            app.settingsPath = std::filesystem::path(arguments[2]);
            validArguments = app.settingsPath.is_absolute() && !app.settingsPath.filename().empty();
        }
    }
    if (arguments) LocalFree(arguments);
    if (!validArguments) {
        MessageBoxW(nullptr, L"Uso: MacroPillControl.exe [--config caminho-absoluto-do-config.ini]",
            L"MacroPill Control", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comResult)) CoUninitialize();
        if (singleton) CloseHandle(singleton);
        return 1;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    windowClass.hbrBackground = app.background;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    bool created = false;
    if (RegisterClassExW(&windowClass)) {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const UINT systemDpi = GetDpiForSystem();
        const int width = std::min(MulDiv(1120, systemDpi ? systemDpi : 96, 96), static_cast<int>(workArea.right - workArea.left));
        const int height = std::min(MulDiv(950, systemDpi ? systemDpi : 96, 96), static_cast<int>(workArea.bottom - workArea.top));
        created = CreateWindowExW(0, kClassName, L"MacroPill Control",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
            workArea.left + ((workArea.right - workArea.left) - width) / 2,
            workArea.top + ((workArea.bottom - workArea.top) - height) / 2,
            width, height, nullptr, nullptr, instance, &app) != nullptr;
    }
    int result = 1;
    if (created) {
        ShowWindow(app.window, show);
        UpdateWindow(app.window);
        MSG message{};
        BOOL read = 0;
        while ((read = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(app.window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        result = read == -1 ? 1 : static_cast<int>(message.wParam);
    } else {
        MessageBoxW(nullptr, L"Não foi possível criar a janela do MacroPill Control.",
            L"MacroPill Control", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
    if (singleton) CloseHandle(singleton);
    return result;
}
#endif
