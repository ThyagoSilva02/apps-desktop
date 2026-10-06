#pragma once

#include <array>
#include <string>

namespace macro {

enum class ActionType { None = 0, Program = 1, Shortcut = 2, Url = 3 };

struct ButtonConfig {
    std::wstring label;
    ActionType type = ActionType::None;
    std::wstring value;
};

struct Config {
    std::array<ButtonConfig, 8> buttons;
    std::wstring port;
    unsigned baud = 115200;
    bool minimizeToTray = true;
};

inline Config DefaultConfig() {
    Config config;
    for (std::size_t index = 0; index < config.buttons.size(); ++index)
        config.buttons[index].label = L"Bot\u00e3o " + std::to_wstring(index + 1);
    return config;
}

} // namespace macro
