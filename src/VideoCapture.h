#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>

using Microsoft::WRL::ComPtr;
class CaptureEngine;

class VideoCapture {
public:
    explicit VideoCapture(CaptureEngine& engine);
    ~VideoCapture();

    bool Initialize();
    void Start();
    void Stop();

private:
    void CaptureLoop();
    bool InitDXGI();
    // Returns false if the duplicator must be recreated (e.g. mode change).
    bool CaptureFrame();

    CaptureEngine& engine_;

    ComPtr<IDXGIOutputDuplication> dup_;
    int width_  = 0;
    int height_ = 0;

    std::thread      thread_;
    std::atomic_bool running_{false};
};
