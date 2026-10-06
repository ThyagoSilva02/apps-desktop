#include "../src/stream_parser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../src/serial.h"
#endif

namespace {

void Require(bool value, const char* name) {
    if (!value) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
}

void Expect(const std::vector<int>& actual, std::initializer_list<int> expected,
            const char* name) {
    Require(actual == std::vector<int>(expected), name);
}

void ParserTests() {
    macro::ButtonStreamParser parser;
    Expect(parser.Feed(""), {}, "empty feed");
    Expect(parser.FlushIdle(), {}, "empty idle");
    Expect(parser.Feed("BTN1\r\nBTN8\nBTN4\r"), {1, 8, 4}, "CRLF and delimiters");

    Expect(parser.Feed("B"), {}, "fragment prefix B");
    Expect(parser.Feed("TN"), {}, "fragment prefix TN");
    Expect(parser.Feed("3"), {}, "last digit held until boundary");
    Expect(parser.Feed("\r"), {3}, "fragment completed by CR");
    Expect(parser.Feed("\n"), {}, "CRLF does not duplicate");

    Expect(parser.Feed("BTN6"), {}, "undelimited token held");
    Expect(parser.FlushIdle(), {6}, "idle completes final token");
    Expect(parser.FlushIdle(), {}, "idle does not duplicate");

    Expect(parser.Feed("BTN1BTN2BTN3\n"), {1, 2, 3}, "concatenated tokens");
    Expect(parser.Feed("BTN8BTN1"), {8}, "concatenation with pending last token");
    Expect(parser.FlushIdle(), {1}, "idle after concatenation");
    Expect(parser.Feed("BTN1B"), {}, "split concatenation B");
    Expect(parser.Feed("T"), {}, "split concatenation T");
    Expect(parser.Feed("N2\n"), {1, 2}, "split concatenation completed");

    Expect(parser.Feed("BTN1"), {}, "possible BTN10 prefix held");
    Expect(parser.Feed("0\r\n"), {}, "BTN10 never emits BTN1");
    Expect(parser.Feed("BTN1"), {}, "possible multidigit prefix held");
    Expect(parser.Feed("234"), {}, "multidigit token ignored");
    Expect(parser.FlushIdle(), {}, "invalid multidigit idle ignored");
    Expect(parser.Feed("DEBUG BTN1\nBTN0\nBTN9\nbtn2\nBTN1x\nBTN12\n"), {},
           "unknown and malformed records ignored");
    Expect(parser.Feed("BTN3\n"), {3}, "valid data after invalid records");

    Expect(parser.Feed("BT"), {}, "partial before idle");
    Expect(parser.FlushIdle(), {}, "partial idle discarded");
    Expect(parser.Feed("BTN5\n"), {5}, "recovery after partial idle");
    Expect(parser.Feed("BTN2"), {}, "token before reset");
    parser.Reset();
    Expect(parser.FlushIdle(), {}, "reset removes pending token");
    Expect(parser.Feed("invalid"), {}, "invalid record before reset");
    parser.Reset();
    Expect(parser.Feed("BTN7\n"), {7}, "reset clears discard state");

    const std::string oversized(1024 * 1024, 'X');
    Expect(parser.Feed(oversized), {}, "oversized unknown stream ignored");
    Expect(parser.Feed("BTN1\n"), {}, "oversized record does not expose substring");
    Expect(parser.Feed("BTN2\n"), {2}, "recovery after oversized record");

    const std::string framed = "BTN1\r\nBTN2BTN3\nBTN10\nunknown\nBTN8\nBTN4BTN5";
    const std::vector<int> expected = {1, 2, 3, 8, 4, 5};
    for (std::size_t chunk = 1; chunk <= framed.size(); ++chunk) {
        parser.Reset();
        std::vector<int> actual;
        for (std::size_t offset = 0; offset < framed.size(); offset += chunk) {
            const auto buttons = parser.Feed(std::string_view(framed).substr(offset, chunk));
            actual.insert(actual.end(), buttons.begin(), buttons.end());
        }
        const auto finalButtons = parser.FlushIdle();
        actual.insert(actual.end(), finalButtons.begin(), finalButtons.end());
        Require(actual == expected, "all stream partition sizes produce identical events");
    }
}

#ifdef _WIN32
void LifecycleSmokeTests() {
    const auto ports = macro::EnumerateSerialPorts();
    unsigned previous = 0;
    for (const auto& port : ports) {
        Require(port.size() > 3 && port.substr(0, 3) == L"COM", "COM enumeration names");
        const unsigned number = static_cast<unsigned>(std::stoul(port.substr(3)));
        Require(number > previous, "COM enumeration numeric order and uniqueness");
        previous = number;
    }

    unsigned missingNumber = 1000000;
    while (std::find(ports.begin(), ports.end(),
                     L"COM" + std::to_wstring(missingNumber)) != ports.end()) {
        ++missingNumber;
    }
    const std::wstring missing = L"COM" + std::to_wstring(missingNumber);
    unsigned callbacks = 0;
    const auto callback = [&](const macro::SerialEvent&) { ++callbacks; };
    std::wstring error;
    DWORD handlesBefore = 0;
    Require(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore) != 0,
            "get initial handle count");

    {
        macro::SerialConnection connection;
        Require(!connection.IsConnected(), "initially disconnected");
        connection.Disconnect();
        connection.Disconnect();
        Require(!connection.Connect(L"NUL", 115200, callback, error),
                "reject non-COM device paths");
        Require(!error.empty(), "invalid name has error message");
        Require(!connection.Connect(missing, 0, callback, error), "reject zero baud rate");
        Require(!connection.Connect(missing, 115200, {}, error), "reject empty callback");

        for (int iteration = 0; iteration < 100; ++iteration) {
            const std::wstring requested = iteration % 2 == 0
                ? missing : L"\\\\.\\" + missing;
            Require(!connection.Connect(requested, 115200, callback, error),
                    "nonexistent COM port fails cleanly");
            Require(!error.empty(), "open failure has error message");
            Require(!connection.IsConnected(), "failed open remains disconnected");
            connection.Disconnect();
            connection.Disconnect();
        }
    }
    Require(callbacks == 0, "failed connects and deliberate disconnects emit no events");
    DWORD handlesAfter = 0;
    Require(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) != 0,
            "get final handle count");
    Require(handlesAfter <= handlesBefore + 2,
            "repeated failed connects and destruction do not leak handles");
    std::cout << "Serial lifecycle handles: " << handlesBefore << " -> "
              << handlesAfter << " (100 failed connection attempts).\n";
}
#endif

} // namespace

int main() {
    ParserTests();
#ifdef _WIN32
    LifecycleSmokeTests();
    std::cout << "Parser and Windows serial lifecycle smoke tests passed.\n";
#else
    std::cout << "Parser tests passed (Windows lifecycle smoke tests skipped).\n";
#endif
    return 0;
}
