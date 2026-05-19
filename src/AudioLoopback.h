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

// WASAPI loopback capture of the default render endpoint (system audio)
class AudioLoopback {
public:
    explicit AudioLoopback(CaptureEngine& engine);
    ~AudioLoopback();

    bool Initialize();
    void Start();
    void Stop();

private:
    void CaptureLoop();

    CaptureEngine& engine_;

    ComPtr<IMMDevice>          device_;
    ComPtr<IAudioClient>       client_;
    ComPtr<IAudioCaptureClient> capture_;
    HANDLE                     ready_event_ = nullptr;

    UINT32   frame_size_  = 0; // bytes per frame after normalisation
    bool     is_float_    = false;
    UINT32   src_channels_= 2;
    UINT32   src_rate_    = 48000;

    std::thread      thread_;
    std::atomic_bool running_{false};
};
