#pragma once
#include <string>
#include <cstdint>

struct EventData {
    std::string  game_id;
    std::string  game_name;
    std::string  event;         // "killfeed" — kill or death involving the local player
    std::wstring log_path;
    int64_t      timestamp_ms = 0;
};
