#pragma once
#include "EventTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <regex>
#include <cstdint>

// Detects kill/death events by tailing game log files defined in configs/games/*.json.
// Plays a sound immediately on detection. The EventCallback hook is reserved for the
// clip engine: check data.event against cfg_.clip_kills / cfg_.clip_deaths there.
class EventDetector {
public:
    // Future clip-engine hook signature:
    //   detector_->SetEventCallback([this](const EventData& d) {
    //       if (d.event == "kill"  && cfg_.clip_kills)  engine_->RequestClipSave();
    //       if (d.event == "death" && cfg_.clip_deaths) engine_->RequestClipSave();
    //   });
    using EventCallback = std::function<void(const EventData&)>;

    EventDetector();
    ~EventDetector();

    // Reads every *.json under config_dir. Defaults to <exe>\configs\games\.
    bool LoadConfigs(const std::filesystem::path& config_dir = {});

    // Register clip-engine hook. Called on the monitor thread — keep it fast.
    void SetEventCallback(EventCallback cb);

    void Start();
    void Stop();

private:
    struct EventRule {
        std::string event;
        std::string match;
        bool        use_regex = false;
        std::regex  pattern;
    };

    struct GameConfig {
        std::string               game_id;
        std::string               name;
        std::vector<std::wstring> log_paths;  // env vars already expanded
        std::vector<EventRule>    events;
        int                       poll_ms = 200;
    };

    struct GameWatcher {
        GameConfig   config;
        std::wstring active_log;
        std::wstring log_dir;       // set when active_log was found via directory scan
        int64_t      file_offset  = 0;
        int64_t      file_size    = 0;
        std::string  partial_line;
        std::chrono::steady_clock::time_point last_polled{};
        std::chrono::steady_clock::time_point last_dir_check{};
    };

    void MonitorLoop();
    void PollGame(GameWatcher& watcher);
    void ProcessLine(GameWatcher& watcher, const std::string& line);
    void PlayEventSound(const std::string& event) const;

    static std::wstring ResolveEnvVars(const std::wstring& raw);
    static std::wstring GetSteamAppsPath();
    static std::wstring GetExeDir();

    std::vector<GameWatcher> watchers_;
    EventCallback            on_event_;
    std::thread              monitor_thread_;
    std::atomic<bool>        running_{false};
    std::wstring             sounds_dir_;
};
