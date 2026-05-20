#include "EventDetector.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <ShlObj.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <fstream>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

// ─── file-local helpers ──────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Returns the most recently written regular file in dir (non-recursive).
static std::wstring LatestFileInDir(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::wstring best;
    FILETIME     best_time{};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (CompareFileTime(&fd.ftLastWriteTime, &best_time) > 0) {
            best_time = fd.ftLastWriteTime;
            best      = dir + L'\\' + fd.cFileName;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

// Case-insensitive find of needle in haystack.
static size_t FindCI(const std::wstring& haystack, const std::wstring& needle) {
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(),   needle.end(),
                          [](wchar_t a, wchar_t b) {
                              return ::towlower(a) == ::towlower(b);
                          });
    return it == haystack.end() ? std::wstring::npos
                                : static_cast<size_t>(it - haystack.begin());
}

// ─── EventDetector ───────────────────────────────────────────────────────────

EventDetector::EventDetector()  = default;
EventDetector::~EventDetector() { Stop(); }

std::wstring EventDetector::GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().wstring();
}

std::wstring EventDetector::GetSteamAppsPath() {
    for (const wchar_t* sub : { L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                                 L"SOFTWARE\\Valve\\Steam" }) {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &hk) != ERROR_SUCCESS)
            continue;
        wchar_t path[MAX_PATH];
        DWORD sz = sizeof(path);
        LSTATUS st = RegQueryValueExW(hk, L"InstallPath", nullptr, nullptr,
                                      reinterpret_cast<LPBYTE>(path), &sz);
        RegCloseKey(hk);
        if (st == ERROR_SUCCESS)
            return std::wstring(path) + L"\\steamapps";
    }
    return L"C:\\Program Files (x86)\\Steam\\steamapps";
}

// Expands %USERPROFILE%, %LOCALAPPDATA% etc. via Win32, then handles
// %steamapps% and %documents% which the OS doesn't know.
std::wstring EventDetector::ResolveEnvVars(const std::wstring& raw) {
    wchar_t expanded[MAX_PATH * 4];
    DWORD r = ExpandEnvironmentStringsW(raw.c_str(), expanded, ARRAYSIZE(expanded));
    std::wstring result = (r > 0 && r <= ARRAYSIZE(expanded)) ? expanded : raw;

    auto replaceCI = [&](const std::wstring& token, const std::wstring& value) {
        size_t pos = 0;
        while ((pos = FindCI(result, token)) != std::wstring::npos)
            result.replace(pos, token.size(), value);
    };

    replaceCI(L"%steamapps%", GetSteamAppsPath());

    wchar_t docs[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
                                   SHGFP_TYPE_CURRENT, docs)))
        replaceCI(L"%documents%", docs);

    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

bool EventDetector::LoadConfigs(const fs::path& config_dir) {
    fs::path dir = config_dir.empty()
        ? fs::path(GetExeDir()) / L"configs" / L"games"
        : config_dir;

    if (!fs::exists(dir)) return false;

    watchers_.clear();

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != L".json") continue;
        try {
            std::ifstream f(entry.path());
            json j = json::parse(f);

            GameConfig cfg;
            cfg.game_id = j.value("game_id", "");
            cfg.name    = j.value("name",    "");
            cfg.poll_ms = j.value("poll_ms", 200);

            for (const auto& p : j["log_paths"])
                cfg.log_paths.push_back(ResolveEnvVars(Utf8ToWide(p.get<std::string>())));

            for (const auto& e : j["events"]) {
                EventRule rule;
                rule.event     = e.value("event", "");
                rule.match     = e.value("match", "");
                rule.use_regex = e.value("regex", false);
                if (rule.use_regex)
                    rule.pattern = std::regex(rule.match,
                                              std::regex::icase | std::regex::optimize);
                cfg.events.push_back(std::move(rule));
            }

            if (!cfg.events.empty())
                watchers_.push_back({ std::move(cfg) });
        } catch (...) { /* skip malformed config */ }
    }

    return !watchers_.empty();
}

void EventDetector::SetEventCallback(EventCallback cb) {
    on_event_ = std::move(cb);
}

void EventDetector::Start() {
    if (running_.exchange(true)) return;
    sounds_dir_     = GetExeDir() + L"\\assets\\sounds";
    monitor_thread_ = std::thread(&EventDetector::MonitorLoop, this);
}

void EventDetector::Stop() {
    if (!running_.exchange(false)) return;
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

void EventDetector::MonitorLoop() {
    while (running_) {
        for (auto& w : watchers_)
            PollGame(w);
        std::this_thread::sleep_for(50ms); // resolution; per-watcher timers gate actual work
    }
}

void EventDetector::PollGame(GameWatcher& w) {
    auto now       = std::chrono::steady_clock::now();
    bool has_log   = !w.active_log.empty();
    // Back off to 5-second scans when the game isn't detected.
    long long interval = has_log ? w.config.poll_ms : 5000LL;
    long long elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - w.last_polled).count();
    if (elapsed < interval) return;
    w.last_polled = now;

    // Helper: attach to a newly found log file, seeking to EOF so stale
    // events from a prior session aren't replayed on startup.
    auto attach = [&](const std::wstring& path, const std::wstring& dir = {}) {
        w.active_log   = path;
        w.log_dir      = dir;
        w.file_offset  = 0;
        w.file_size    = 0;
        w.partial_line.clear();

        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz;
        if (GetFileSizeEx(hf, &sz)) {
            w.file_offset = sz.QuadPart;
            w.file_size   = sz.QuadPart;
        }
        CloseHandle(hf);
    };

    // ── Find a log if we don't have one yet ──────────────────────────────────
    if (w.active_log.empty()) {
        for (const auto& p : w.config.log_paths) {
            if (fs::is_directory(p)) {
                std::wstring latest = LatestFileInDir(p);
                if (!latest.empty() && fs::exists(latest)) { attach(latest, p); break; }
            } else if (fs::exists(p)) {
                attach(p); break;
            }
        }
        if (w.active_log.empty()) return;
    }

    // ── For directory-based watches, check every 60 s for a newer log file ──
    // Apex and similar games rotate log names each session.
    if (!w.log_dir.empty()) {
        long long since_dir = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now - w.last_dir_check).count();
        if (since_dir > 60'000) {
            w.last_dir_check = now;
            std::wstring latest = LatestFileInDir(w.log_dir);
            if (!latest.empty() && latest != w.active_log)
                attach(latest, w.log_dir);
        }
    }

    // ── Verify log still exists ──────────────────────────────────────────────
    if (!fs::exists(w.active_log)) {
        w.active_log.clear();
        w.log_dir.clear();
        w.file_offset = 0;
        w.file_size   = 0;
        return;
    }

    // ── Read new bytes ───────────────────────────────────────────────────────
    HANDLE hf = CreateFileW(w.active_log.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER cur_size;
    if (!GetFileSizeEx(hf, &cur_size)) { CloseHandle(hf); return; }

    // Truncation means the log was rotated in-place — restart from the top.
    if (cur_size.QuadPart < w.file_size) {
        w.file_offset = 0;
        w.partial_line.clear();
    }
    w.file_size = cur_size.QuadPart;

    if (w.file_offset > 0) {
        LARGE_INTEGER pos{};
        pos.QuadPart = w.file_offset;
        SetFilePointerEx(hf, pos, nullptr, FILE_BEGIN);
    }

    char  buf[4096];
    DWORD n;
    while (ReadFile(hf, buf, sizeof(buf), &n, nullptr) && n > 0) {
        w.file_offset += n;
        for (DWORD i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                ProcessLine(w, w.partial_line);
                w.partial_line.clear();
            } else if (c != '\r') {
                w.partial_line += c;
            }
        }
    }

    CloseHandle(hf);
}

void EventDetector::ProcessLine(GameWatcher& w, const std::string& line) {
    if (line.empty()) return;
    for (const auto& rule : w.config.events) {
        bool hit = rule.use_regex
            ? std::regex_search(line, rule.pattern)
            : line.find(rule.match) != std::string::npos;
        if (!hit) continue;

        EventData data;
        data.game_id      = w.config.game_id;
        data.game_name    = w.config.name;
        data.event        = rule.event;
        data.log_path     = w.active_log;
        data.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();

        PlayEventSound(rule.event);
        if (on_event_) on_event_(data);
    }
}

void EventDetector::PlayEventSound(const std::string& event) const {
    std::wstring file;
    UINT         beep;
    if (event == "kill") {
        file = sounds_dir_ + L"\\kill.wav";
        beep = MB_OK;
    } else if (event == "death") {
        file = sounds_dir_ + L"\\death.wav";
        beep = MB_ICONASTERISK;
    } else {
        return;
    }

    if (fs::exists(file))
        PlaySoundW(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC);
    else
        MessageBeep(beep); // fallback until user places WAVs in assets/sounds/
}
