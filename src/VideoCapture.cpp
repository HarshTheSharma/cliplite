#include "VideoCapture.h"
#include "CaptureEngine.h"
#include <avrt.h>
#include <cstring>
#pragma comment(lib, "avrt.lib")

static uint64_t Timestamp100ns() {
    static const LONGLONG freq = [] {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart;
    }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 10'000'000ULL / (ULONGLONG)freq);
}

VideoCapture::VideoCapture(CaptureEngine& engine) : engine_(engine) {}

VideoCapture::~VideoCapture() { Stop(); }

bool VideoCapture::Initialize() {
    return InitDXGI();
}

bool VideoCapture::InitDXGI() {
    auto* device = engine_.GetDevice();

    ComPtr<IDXGIDevice> dxgi_dev;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi_dev.GetAddressOf()))))
        return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_dev->GetAdapter(adapter.GetAddressOf())))
        return false;

    ComPtr<IDXGIOutput> output;
    if (FAILED(adapter->EnumOutputs(0, output.GetAddressOf())))
        return false;

    DXGI_OUTPUT_DESC desc{};
    output->GetDesc(&desc);
    width_  = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
    height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

    ComPtr<IDXGIOutput1> output1;
    if (FAILED(output.As(&output1)))
        return false;

    HRESULT hr = output1->DuplicateOutput(device, dup_.GetAddressOf());
    return SUCCEEDED(hr);
}

void VideoCapture::Start() {
    running_ = true;
    thread_ = std::thread(&VideoCapture::CaptureLoop, this);
}

void VideoCapture::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void VideoCapture::CaptureLoop() {
    DWORD task_idx = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Games", &task_idx);

    while (running_) {
        if (!CaptureFrame()) {
            // exclusive fullscreen or a resolution change kills the duplicator
            dup_.Reset();
            Sleep(200);
            InitDXGI();
        }
    }

    if (task) AvRevertMmThreadCharacteristics(task);
}

bool VideoCapture::CaptureFrame() {
    if (!dup_) return false;

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource>   res;

    HRESULT hr = dup_->AcquireNextFrame(16 /*ms timeout*/, &info, res.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true;  // no new frame; keep going
    if (hr == DXGI_ERROR_ACCESS_LOST)  return false; // need to recreate
    if (FAILED(hr)) { Sleep(1); return true; }

    struct FrameGuard {
        IDXGIOutputDuplication* d;
        ~FrameGuard() { d->ReleaseFrame(); }
    } guard{ dup_.Get() };

    ComPtr<ID3D11Texture2D> src_tex;
    if (FAILED(res->QueryInterface(IID_PPV_ARGS(src_tex.GetAddressOf()))))
        return true;

    TexturePool* pool = engine_.GetTexturePool();
    if (!pool) return true;
    const int W = pool->Width(), H = pool->Height();

    // Trim ring buffer to the configured clip duration.
    const size_t max_frames = (size_t)(engine_.GetConfig().duration_seconds * 30 + 30);
    if (engine_.GetVideoRing().size() >= max_frames)
        engine_.GetVideoRing().evict_oldest();

    // Acquire one of the two readback staging textures.
    ID3D11Texture2D* staging = nullptr;
    auto pool_ref = pool->Acquire(staging);
    if (!staging) {
        // Both staging textures busy — wait one frame and retry once.
        Sleep(1);
        pool_ref = pool->Acquire(staging);
        if (!staging) return true;
    }

    {
        std::lock_guard lk(engine_.GetContextMutex());
        D3D11_TEXTURE2D_DESC src_desc{}, dst_desc{};
        src_tex->GetDesc(&src_desc);
        staging->GetDesc(&dst_desc);
        if (src_desc.Width != dst_desc.Width || src_desc.Height != dst_desc.Height)
            return true;
        engine_.GetContext()->CopyResource(staging, src_tex.Get());
    }

    // Read back immediately to heap memory — avoids holding hundreds of staging textures.
    auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)W * H * 4);
    {
        std::lock_guard lk(engine_.GetContextMutex());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(engine_.GetContext()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
            return true;

        const auto* src_row = static_cast<const uint8_t*>(mapped.pData);
        uint8_t*    dst     = pixels->data();
        for (int row = 0; row < H; ++row, src_row += mapped.RowPitch, dst += W * 4)
            std::memcpy(dst, src_row, (size_t)W * 4);

        engine_.GetContext()->Unmap(staging, 0);
    }
    // pool_ref dtor returns staging texture to the 2-texture pool.

    VideoFrame frame;
    frame.timestamp_100ns = Timestamp100ns();
    frame.width  = W;
    frame.height = H;
    frame.bgra   = std::move(pixels);

    engine_.GetVideoRing().push(std::move(frame));
    return true;
}
