#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class CaptureEngine;

class EncoderThread {
public:
    explicit EncoderThread(CaptureEngine& engine);
    ~EncoderThread();

    void Start();
    void Stop();

    // Thread-safe: schedules a clip save.  Ignored if already encoding.
    void RequestClip();

private:
    void EncoderLoop();
    void SaveClip();

    CaptureEngine& engine_;

    std::thread              thread_;
    std::atomic_bool         running_{false};
    std::atomic_bool         clip_requested_{false};
    std::mutex               mu_;
    std::condition_variable  cv_;
};
