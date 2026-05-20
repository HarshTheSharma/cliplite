#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <mutex>
#include <vector>
#include "Config.h"
#include "RingBuffer.h"
#include "TexturePool.h"

using Microsoft::WRL::ComPtr;

struct VideoFrame {
    uint64_t timestamp_100ns = 0;
    int      width  = 0;
    int      height = 0;
    // Shared so ring-buffer snapshot copies are O(1); data freed when last holder drops it.
    std::shared_ptr<const std::vector<uint8_t>> bgra; // width * height * 4, row-major

    explicit operator bool() const noexcept { return bgra && !bgra->empty(); }
};

struct AudioPacket {
    uint64_t             timestamp_100ns = 0;
    uint32_t             sample_rate     = 48000;
    uint16_t             channels        = 2;
    uint16_t             bits_per_sample = 16;  // always int16 after normalisation
    std::vector<uint8_t> pcm;                   // interleaved int16 samples
};

// Ring buffer capacities (powers of two).
// Video  : 30 fps × 320 s headroom → 9600 → 16384 slots
//          Each slot is a shared_ptr; only live slots hold frame data.
// Audio  : ~100 packets/s × 320 s → 32000 → 32768
static constexpr size_t kVideoRingSize  = 16384;
static constexpr size_t kAudioRingSize  = 32768;

using VideoRing = RingBuffer<VideoFrame, kVideoRingSize>;
using AudioRing = RingBuffer<AudioPacket, kAudioRingSize>;

class VideoCapture;
class AudioLoopback;
class AudioMic;
class EncoderThread;

class CaptureEngine {
public:
    explicit CaptureEngine(Config& cfg);
    ~CaptureEngine();

    bool Initialize();
    void Start();
    void Stop();

    // Thread-safe: signals the encoder to save a clip of the last N seconds.
    void RequestClipSave();

    Config&              GetConfig()        noexcept { return cfg_; }
    ID3D11Device*        GetDevice()        noexcept { return device_.Get(); }
    ID3D11DeviceContext* GetContext()       noexcept { return ctx_.Get(); }
    std::mutex&          GetContextMutex()  noexcept { return ctx_mu_; }
    TexturePool*         GetTexturePool()   noexcept { return pool_.get(); }
    VideoRing&           GetVideoRing()     noexcept { return video_ring_; }
    AudioRing&           GetLoopbackRing()  noexcept { return loopback_ring_; }
    AudioRing&           GetMicRing()       noexcept { return mic_ring_; }

private:
    bool InitD3D11();
    bool InitTexturePool();

    Config& cfg_;

    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    std::mutex                  ctx_mu_; // guards all D3D11 immediate-context calls

    std::unique_ptr<TexturePool>  pool_;
    VideoRing                     video_ring_;
    AudioRing                     loopback_ring_;
    AudioRing                     mic_ring_;

    std::unique_ptr<VideoCapture>  video_;
    std::unique_ptr<AudioLoopback> loopback_;
    std::unique_ptr<AudioMic>      mic_;
    std::unique_ptr<EncoderThread> encoder_;
};
