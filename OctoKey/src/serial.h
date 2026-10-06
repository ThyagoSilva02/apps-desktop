#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace macro {

struct SerialEvent {
    enum class Kind { Button, Disconnected };
    Kind kind;
    int button = 0;
    std::wstring message;
};

// Returns existing COM names, ordered numerically (COM2 precedes COM10).
// Enumeration failure returns an empty vector. Opening a port may still fail
// if it is removed or another process owns it after enumeration.
std::vector<std::wstring> EnumerateSerialPorts();

class SerialConnection {
public:
    SerialConnection();
    ~SerialConnection();
    SerialConnection(const SerialConnection&) = delete;
    SerialConnection& operator=(const SerialConnection&) = delete;
    SerialConnection(SerialConnection&&) = delete;
    SerialConnection& operator=(SerialConnection&&) = delete;

    // Lifecycle methods belong to the application's owning/UI thread.
    // callback runs on the reader thread. It must return promptly, copy the
    // event before queuing it, and never call lifecycle methods or destroy
    // this connection. The callback must not capture objects already destroyed
    // when Disconnect is called. IsConnected is safe on any thread.
    // Connect first disconnects an earlier session, even if this attempt fails.
    // Port accepts COMn or \\.\COMn; baud must be positive. The port is 8N1,
    // without hardware/software flow control, with DTR and RTS enabled.
    bool Connect(const std::wstring& port, unsigned baud,
                 std::function<void(const SerialEvent&)> callback,
                 std::wstring& error);

    // Cancels pending I/O, joins the reader, then closes handles. No callback
    // can run after this returns. Intentional disconnect emits no event.
    void Disconnect();
    bool IsConnected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace macro
