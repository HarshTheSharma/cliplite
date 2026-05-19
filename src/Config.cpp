#include "Config.h"
#include <nlohmann/json.hpp>
#include <ShlObj.h>
#include <fstream>

using json = nlohmann::json;

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p);
    std::wstring result = p ? p : L"";
    CoTaskMemFree(p);
    return result;
}

std::filesystem::path Config::ConfigFilePath() {
    return std::filesystem::path(KnownFolder(FOLDERID_RoamingAppData))
           / L"Cliplite" / L"config.json";
}

std::filesystem::path Config::TempDirPath() {
    return std::filesystem::path(KnownFolder(FOLDERID_LocalAppData))
           / L"Cliplite" / L"temp";
}

Config Config::Load() {
    Config cfg;
    cfg.save_path = KnownFolder(FOLDERID_Videos) + L"\\Cliplite";

    auto path = ConfigFilePath();
    if (!std::filesystem::exists(path))
        return cfg;

    try {
        std::ifstream f(path);
        json j = json::parse(f);
        cfg.duration_seconds = j.value("duration_seconds", 60);
        cfg.save_path  = Utf8ToWide(j.value("save_path",   WideToUtf8(cfg.save_path)));
        cfg.hotkey_mod = Utf8ToWide(j.value("hotkey_mod",  "CTRL+ALT"));
        cfg.hotkey_key = Utf8ToWide(j.value("hotkey_key",  "F10"));
        cfg.enable_mic = j.value("enable_mic", true);
    } catch (...) {}

    return cfg;
}

void Config::Save() const {
    auto path = ConfigFilePath();
    std::filesystem::create_directories(path.parent_path());

    json j;
    j["duration_seconds"] = duration_seconds;
    j["save_path"]   = WideToUtf8(save_path);
    j["hotkey_mod"]  = WideToUtf8(hotkey_mod);
    j["hotkey_key"]  = WideToUtf8(hotkey_key);
    j["enable_mic"]  = enable_mic;

    std::ofstream f(path);
    f << j.dump(4) << '\n';
}

void Config::EnsureDirectories() const {
    std::filesystem::create_directories(save_path);
    std::filesystem::create_directories(TempDirPath());
    std::filesystem::create_directories(ConfigFilePath().parent_path());
}
