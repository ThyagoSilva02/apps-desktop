#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace macro {

// Recommended wire protocol: one ASCII BTN1..BTN8 token followed by '\n'
// per button press. CRLF is also accepted. Exact concatenated tokens are
// tolerated; the final token is held until a delimiter or FlushIdle().
// Call FlushIdle only after about 50 ms with no new bytes. Without framing,
// a gap longer than that is necessarily interpreted as an end of token:
// firmware should use newlines rather than depend on this compatibility mode.
// Unknown records are discarded through CR/LF or the next idle boundary.
class ButtonStreamParser {
public:
    std::vector<int> Feed(std::string_view bytes) {
        std::vector<int> buttons;
        for (const char byte : bytes) {
            if (byte == '\r' || byte == '\n') {
                Finish(buttons);
                continue;
            }
            if (discarding_) {
                continue;
            }
            if (pending_.size() >= kMaximumPendingBytes) {
                Discard();
                continue;
            }
            pending_.push_back(byte);

            if (pending_.size() <= 3) {
                if (std::string_view(pending_) != kPrefix.substr(0, pending_.size())) {
                    Discard();
                }
            } else if (pending_.size() == 4) {
                if (!IsButtonDigit(pending_[3])) {
                    Discard();
                }
            } else {
                // A second BTN prefix is an unambiguous concatenation
                // boundary. A digit following BTN1 (such as BTN10) is not.
                const std::string_view suffix(pending_.data() + 4, pending_.size() - 4);
                if (suffix != kPrefix.substr(0, suffix.size())) {
                    Discard();
                } else if (suffix.size() == kPrefix.size()) {
                    buttons.push_back(pending_[3] - '0');
                    pending_.assign(kPrefix);
                }
            }
        }
        return buttons;
    }

    std::vector<int> FlushIdle() {
        std::vector<int> buttons;
        Finish(buttons);
        return buttons;
    }

    void Reset() noexcept {
        pending_.clear();
        discarding_ = false;
    }

private:
    static constexpr std::string_view kPrefix = "BTN";
    static constexpr std::size_t kMaximumPendingBytes = 256;

    static bool IsButtonDigit(char digit) noexcept {
        return digit >= '1' && digit <= '8';
    }

    void Discard() noexcept {
        pending_.clear();
        discarding_ = true;
    }

    void Finish(std::vector<int>& buttons) {
        if (!discarding_ && pending_.size() == 4 &&
            IsButtonDigit(pending_[3])) {
            buttons.push_back(pending_[3] - '0');
        }
        Reset();
    }

    std::string pending_;
    bool discarding_ = false;
};

} // namespace macro
