#include "CaptureEngine.h"
#include "VideoCapture.h"
#include "AudioLoopback.h"
#include "AudioMic.h"
#include "EncoderThread.h"
#include <dxgi.h>
#include <dxgi1_2.h>
#include <algorithm>

CaptureEngine::CaptureEngine(Config& cfg) : cfg_(cfg) {}

CaptureEngine::~CaptureEngine() { Stop(); }

bool CaptureEngine::Initialize() {
    if (!InitD3D11())       return false;
    if (!InitTexturePool()) return false;

    video_    = std::make_unique<VideoCapture>(*this);
    loopback_ = std::make_unique<AudioLoopback>(*this);
    mic_      = std::make_unique<AudioMic>(*this);
    encoder_  = std::make_unique<EncoderThread>(*this);

    if (!video_->Initialize())    return false;
    if (!loopback_->Initialize()) return false;
    if (cfg_.enable_mic)
        mic_->Initialize(); // ok if this fails, not all machines have a mic

    return true;
}

void CaptureEngine::Start() {
    encoder_->Start();
    video_->Start();
    loopback_->Start();
    if (cfg_.enable_mic)
        mic_->Start();
}

void CaptureEngine::Stop() {
    if (video_)    video_->Stop();
    if (loopback_) loopback_->Stop();
    if (mic_)      mic_->Stop();
    if (encoder_)  encoder_->Stop();
}

void CaptureEngine::RequestClipSave() {
    if (encoder_) encoder_->RequestClip();
}

bool CaptureEngine::InitD3D11() {
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT
                     | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        device_.GetAddressOf(), nullptr, ctx_.GetAddressOf());
    if (FAILED(hr)) return false;
    return true;
}

bool CaptureEngine::InitTexturePool() {
    int w = 1920, h = 1080; // fallback if DXGI query fails
    {
        ComPtr<IDXGIDevice> dxgi_dev;
        if (SUCCEEDED(device_.As(&dxgi_dev))) {
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgi_dev->GetAdapter(adapter.GetAddressOf()))) {
                ComPtr<IDXGIOutput> output;
                if (SUCCEEDED(adapter->EnumOutputs(0, output.GetAddressOf()))) {
                    DXGI_OUTPUT_DESC desc{};
                    output->GetDesc(&desc);
                    w = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
                    h = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                }
            }
        }
    }

    // 1-second margin so the ring doesn't run dry exactly at the configured duration; capped to avoid exhausting VRAM
    const int fps       = 30;
    const int pool_size = std::min(cfg_.duration_seconds * fps + fps, 9000);

    pool_ = std::make_unique<TexturePool>();
    return pool_->Initialize(device_.Get(), w, h, pool_size);
}
