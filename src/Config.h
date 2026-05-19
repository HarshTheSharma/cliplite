#pragma once
#include <string>
#include <filesystem>

struct Config {
    int          duration_seconds = 60;
    std::wstring save_path;       // final clip directory
    std::wstring hotkey_mod = L"CTRL+ALT";
    std::wstring hotkey_key = L"F10";
    bool         enable_mic = true;

    static std::filesystem::path ConfigFilePath();
    static std::filesystem::path TempDirPath();

    // save_path defaults to Videos\Cliplite even when no config file exists
    static Config Load();
    void Save() const;
    void EnsureDirectories() const;
};
