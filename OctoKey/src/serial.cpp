#include "serial.h"
#include "stream_parser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <limits>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace macro {
namespace {

constexpr DWORD kReadTimeoutMilliseconds = 50;
constexpr auto kIdlePeriod = std::chrono::milliseconds(50);

bool PortNumber(std::wstring_view name, unsigned& number) noexcept {
    if (name.size() < 4 ||
        (name[0] != L'C' && name[0] != L'c') ||
        (name[1] != L'O' && name[1] != L'o') ||
        (name[2] != L'M' && name[2] != L'm')) {
        return false;
    }
    unsigned result = 0;
    for (std::size_t index = 3; index < name.size(); ++index) {
        const wchar_t digit = name[index];
        if (digit < L'0' || digit > L'9') {
            return false;
        }
        const unsigned value = static_cast<unsigned>(digit - L'0');
        if (result > ((std::numeric_limits<unsigned>::max)() - value) / 10) {
            return false;
        }
        result = result * 10 + value;
    }
    if (result == 0) {
        return false;
    }
    number = result;
    return true;
}

bool NormalizePort(const std::wstring& input, std::wstring& port) {
    std::wstring_view name(input);
    if (name.substr(0, 4) == L"\\\\.\\") {
        name.remove_prefix(4);
    }
    unsigned number = 0;
    if (!PortNumber(name, number)) {
        return false;
    }
    port = L"COM" + std::to_wstring(number);
    return true;
}

std::wstring WindowsError(DWORD code) {
    wchar_t* allocated = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&allocated), 0, nullptr);
    std::wstring message;
    if (length != 0 && allocated != nullptr) {
        message.assign(allocated, length);
        LocalFree(allocated);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ' || message.back() == L'\t')) {
            message.pop_back();
        }
    }
    if (message.empty()) {
        message = L"Erro do Windows " + std::to_wstring(code) + L".";
    } else {
        message += L" (erro " + std::to_wstring(code) + L").";
    }
    return message;
}

std::wstring PortError(const std::wstring& operation,
                       const std::wstring& port, DWORD code) {
    if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION) {
        return operation + L" " + port +
               L". A porta está em uso por outro programa ou o acesso foi negado. " +
               WindowsError(code);
    }
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
        code == ERROR_DEVICE_NOT_CONNECTED || code == ERROR_NO_SUCH_DEVICE) {
        return operation + L" " + port +
               L". A placa não está disponível; verifique o cabo USB e a porta. " +
               WindowsError(code);
    }
    return operation + L" " + port + L". " + WindowsError(code);
}

void CloseIfValid(HANDLE& handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

// Keeps the OVERLAPPED object and its buffer alive even if parsing, allocating
// an error message, or another C++ operation throws while a read is pending.
class PendingRead {
public:
    PendingRead(HANDLE port, OVERLAPPED& overlap) noexcept
        : port_(port), overlap_(overlap) {}

    ~PendingRead() { CancelAndDrain(); }

    void MarkPending() noexcept { pending_ = true; }
    void MarkCompleted() noexcept { pending_ = false; }

    void CancelAndDrain() noexcept {
        if (!pending_) {
            return;
        }
        CancelIoEx(port_, &overlap_);
        DWORD ignored = 0;
        GetOverlappedResult(port_, &overlap_, &ignored, TRUE);
        pending_ = false;
    }

private:
    HANDLE port_;
    OVERLAPPED& overlap_;
    bool pending_ = false;
};

} // namespace

std::vector<std::wstring> EnumerateSerialPorts() {
    std::vector<wchar_t> buffer(4096);
    DWORD length = 0;
    for (;;) {
        length = QueryDosDeviceW(nullptr, buffer.data(),
                                 static_cast<DWORD>(buffer.size()));
        if (length != 0) {
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            buffer.size() >= 1024 * 1024) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }

    std::vector<std::pair<unsigned, std::wstring>> numbered;
    for (std::size_t offset = 0;
         offset < length && buffer[offset] != L'\0';) {
        const std::wstring name(buffer.data() + offset);
        unsigned number = 0;
        if (PortNumber(name, number)) {
            numbered.emplace_back(number, L"COM" + std::to_wstring(number));
        }
        offset += name.size() + 1;
    }
    std::sort(numbered.begin(), numbered.end());
    numbered.erase(std::unique(numbered.begin(), numbered.end()), numbered.end());
    std::vector<std::wstring> ports;
    ports.reserve(numbered.size());
    for (auto& entry : numbered) {
        ports.push_back(std::move(entry.second));
    }
    return ports;
}

struct SerialConnection::Impl {
    HANDLE port = nullptr;
    HANDLE stopEvent = nullptr;
    HANDLE readEvent = nullptr;
    std::wstring portName;
    std::function<void(const SerialEvent&)> callback;
    std::thread reader;
    std::mutex lifecycleMutex;
    std::atomic<bool> connected{false};

    bool Stopping() const noexcept {
        return WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
    }

    void CloseHandles() noexcept {
        CloseIfValid(readEvent);
        CloseIfValid(stopEvent);
        CloseIfValid(port);
    }

    void DisconnectLocked() {
        connected.store(false, std::memory_order_release);
        if (stopEvent != nullptr) {
            SetEvent(stopEvent);
        }
        if (port != nullptr) {
            // ERROR_NOT_FOUND simply means there was no pending request.
            CancelIoEx(port, nullptr);
        }
        if (reader.joinable()) {
            reader.join();
        }
        // The OVERLAPPED object and its buffer have now gone out of scope only
        // after completion/cancellation was drained in ReadLoop.
        CloseHandles();
        callback = {};
        portName.clear();
    }

    bool DeliverButtons(const std::vector<int>& buttons) noexcept {
        for (const int button : buttons) {
            if (Stopping()) {
                return true;
            }
            try {
                callback(SerialEvent{SerialEvent::Kind::Button, button, {}});
            } catch (...) {
                return false;
            }
        }
        return true;
    }

    void ReadLoop() noexcept {
        std::wstring failure;
        try {
            ButtonStreamParser parser;
            auto lastByte = std::chrono::steady_clock::now();
            bool awaitingIdle = false;
            auto flushIdle = [&]() {
                if (awaitingIdle &&
                    std::chrono::steady_clock::now() - lastByte >= kIdlePeriod) {
                    awaitingIdle = false;
                    return DeliverButtons(parser.FlushIdle());
                }
                return true;
            };
            auto checkDevice = [&]() {
                DWORD communicationErrors = 0;
                COMSTAT status{};
                if (!ClearCommError(port, &communicationErrors, &status)) {
                    failure = PortError(L"A comunicação foi interrompida em",
                                        portName, GetLastError());
                    return false;
                }
                if (communicationErrors != 0) {
                    failure = L"A porta " + portName +
                              L" informou erro de comunicação (código " +
                              std::to_wstring(communicationErrors) +
                              L"). Reconecte a placa e verifique o baud rate.";
                    return false;
                }
                return true;
            };

            while (!Stopping()) {
                char bytes[256]{};
                OVERLAPPED overlap{};
                overlap.hEvent = readEvent;
                PendingRead pendingRead(port, overlap);
                ResetEvent(readEvent);
                DWORD count = 0;
                const BOOL immediate = ReadFile(port, bytes, sizeof(bytes),
                                                &count, &overlap);
                if (!immediate) {
                    const DWORD readError = GetLastError();
                    if (readError != ERROR_IO_PENDING) {
                        if (!Stopping()) {
                            failure = PortError(L"Não foi possível ler",
                                                portName, readError);
                        }
                        break;
                    }
                    pendingRead.MarkPending();

                    bool completed = false;
                    while (!completed) {
                        const HANDLE waits[] = {stopEvent, readEvent};
                        const DWORD waitResult = WaitForMultipleObjects(
                            2, waits, FALSE, kReadTimeoutMilliseconds);
                        if (waitResult == WAIT_OBJECT_0) {
                            pendingRead.CancelAndDrain();
                            break;
                        }
                        if (waitResult == WAIT_OBJECT_0 + 1) {
                            if (!GetOverlappedResult(port, &overlap, &count, FALSE)) {
                                const DWORD completionError = GetLastError();
                                if (completionError == ERROR_IO_INCOMPLETE) {
                                    failure = PortError(L"A porta informou uma conclusão de leitura inválida em",
                                                        portName, completionError);
                                    pendingRead.CancelAndDrain();
                                    break;
                                }
                                pendingRead.MarkCompleted();
                                if (!Stopping()) {
                                    failure = PortError(L"A leitura foi interrompida em",
                                                        portName, completionError);
                                }
                            } else {
                                pendingRead.MarkCompleted();
                                completed = true;
                            }
                            break;
                        }
                        if (waitResult == WAIT_TIMEOUT) {
                            // Prefer bytes already completed over an idle flush.
                            if (WaitForSingleObject(readEvent, 0) == WAIT_OBJECT_0) {
                                continue;
                            }
                            if (!checkDevice() || !flushIdle()) {
                                if (failure.empty()) {
                                    failure = L"Não foi possível processar os eventos da placa.";
                                }
                                pendingRead.CancelAndDrain();
                                break;
                            }
                            continue;
                        }
                        const DWORD waitError = waitResult == WAIT_FAILED
                            ? GetLastError() : ERROR_GEN_FAILURE;
                        failure = PortError(L"Não foi possível aguardar dados de",
                                            portName, waitError);
                        pendingRead.CancelAndDrain();
                        break;
                    }
                    if (!completed) {
                        break;
                    }
                }

                if (Stopping()) {
                    break;
                }
                if (count != 0) {
                    lastByte = std::chrono::steady_clock::now();
                    awaitingIdle = true;
                    if (!DeliverButtons(parser.Feed(std::string_view(bytes, count)))) {
                        failure = L"Não foi possível processar os eventos da placa.";
                        break;
                    }
                } else {
                    // A read timeout is idle, not a disconnect. ClearCommError
                    // still detects unplugged devices/drivers returning no data.
                    if (!checkDevice() || !flushIdle()) {
                        if (failure.empty()) {
                            failure = L"Não foi possível processar os eventos da placa.";
                        }
                        break;
                    }
                    // Avoid a hot loop with drivers that complete empty reads
                    // immediately despite their configured timeout.
                    WaitForSingleObject(stopEvent, 5);
                }
            }
        } catch (...) {
            failure = L"A leitura da placa foi interrompida por um erro interno.";
        }
        connected.store(false, std::memory_order_release);
        if (!failure.empty() && !Stopping()) {
            try {
                callback(SerialEvent{SerialEvent::Kind::Disconnected, 0, failure});
            } catch (...) {
                // Client exceptions must never unwind through std::thread.
            }
        }
    }
};

SerialConnection::SerialConnection() : impl_(std::make_unique<Impl>()) {}

SerialConnection::~SerialConnection() {
    Disconnect();
}

bool SerialConnection::Connect(const std::wstring& requestedPort, unsigned baud,
                               std::function<void(const SerialEvent&)> callback,
                               std::wstring& error) {
    std::lock_guard<std::mutex> lock(impl_->lifecycleMutex);
    impl_->DisconnectLocked();
    error.clear();

    std::wstring portName;
    if (!NormalizePort(requestedPort, portName)) {
        error = L"Selecione uma porta válida, como COM3.";
        return false;
    }
    if (baud == 0) {
        error = L"O baud rate deve ser maior que zero.";
        return false;
    }
    if (!callback) {
        error = L"O receptor dos eventos da placa não foi configurado.";
        return false;
    }

    const std::wstring device = L"\\\\.\\" + portName;
    impl_->port = CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                             nullptr);
    if (impl_->port == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        impl_->port = nullptr;
        error = PortError(L"Não foi possível abrir", portName, code);
        return false;
    }

    auto failConfiguration = [&](const std::wstring& operation, DWORD code) {
        error = PortError(operation, portName, code);
        impl_->CloseHandles();
        return false;
    };

    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(impl_->port, &settings)) {
        return failConfiguration(L"Não foi possível consultar", GetLastError());
    }
    settings.BaudRate = baud;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fParity = FALSE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fDsrSensitivity = FALSE;
    settings.fTXContinueOnXoff = TRUE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    settings.fErrorChar = FALSE;
    settings.fNull = FALSE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    settings.fAbortOnError = FALSE;
    if (!SetCommState(impl_->port, &settings)) {
        return failConfiguration(L"Não foi possível configurar", GetLastError());
    }

    COMMTIMEOUTS timeouts{};
    // Documented mode: buffered bytes return immediately; otherwise complete
    // on the first byte or after the specified timeout, including overlapped I/O.
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = kReadTimeoutMilliseconds;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (!SetCommTimeouts(impl_->port, &timeouts)) {
        return failConfiguration(L"Não foi possível definir os tempos de leitura de",
                                 GetLastError());
    }
    if (!PurgeComm(impl_->port, PURGE_RXCLEAR | PURGE_TXCLEAR |
                               PURGE_RXABORT | PURGE_TXABORT)) {
        return failConfiguration(L"Não foi possível preparar", GetLastError());
    }

    impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl_->stopEvent == nullptr) {
        return failConfiguration(L"Não foi possível preparar a parada de",
                                 GetLastError());
    }
    impl_->readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl_->readEvent == nullptr) {
        return failConfiguration(L"Não foi possível preparar a leitura de",
                                 GetLastError());
    }
    impl_->portName = std::move(portName);
    impl_->callback = std::move(callback);
    impl_->connected.store(true, std::memory_order_release);
    try {
        impl_->reader = std::thread([state = impl_.get()] { state->ReadLoop(); });
    } catch (...) {
        impl_->connected.store(false, std::memory_order_release);
        error = L"Não foi possível iniciar a leitura da placa.";
        impl_->CloseHandles();
        impl_->callback = {};
        impl_->portName.clear();
        return false;
    }
    return true;
}

void SerialConnection::Disconnect() {
    std::lock_guard<std::mutex> lock(impl_->lifecycleMutex);
    impl_->DisconnectLocked();
}

bool SerialConnection::IsConnected() const noexcept {
    return impl_->connected.load(std::memory_order_acquire);
}

} // namespace macro
