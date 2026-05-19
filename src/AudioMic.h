#pragma once
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>

using Microsoft::WRL::ComPtr;
class CaptureEngine;

// Same structure as AudioLoopback but eCapture instead of eRender with loopback
class AudioMic {
public:
    explicit AudioMic(CaptureEngine& engine);
    ~AudioMic();

    bool Initialize();
    void Start();
    void Stop();

private:
    void CaptureLoop();

    CaptureEngine& engine_;

    ComPtr<IMMDevice>           device_;
    ComPtr<IAudioClient>        client_;
    ComPtr<IAudioCaptureClient> capture_;
    HANDLE                      ready_event_ = nullptr;

    bool   is_float_     = false;
    UINT32 src_channels_ = 1;
    UINT32 src_rate_     = 48000;

    std::thread      thread_;
    std::atomic_bool running_{false};
};
