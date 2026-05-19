#include "AudioLoopback.h"
#include "CaptureEngine.h"
#include <avrt.h>
#include <algorithm>

// {00000003-0000-0010-8000-00AA00389B71}  KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
static constexpr GUID kSubFmtFloat = {
    0x00000003, 0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
};

static uint64_t Timestamp100ns() {
    static const LONGLONG freq = [] {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart;
    }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 10'000'000ULL / (ULONGLONG)freq);
}

static int16_t F32ToI16(float f) {
    f = std::max(-1.0f, std::min(1.0f, f));
    return static_cast<int16_t>(f * 32767.0f);
}

AudioLoopback::AudioLoopback(CaptureEngine& engine) : engine_(engine) {}
AudioLoopback::~AudioLoopback() { Stop(); }

bool AudioLoopback::Initialize() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.GetAddressOf()))))
        return false;

    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                    device_.GetAddressOf())))
        return false;

    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client_.GetAddressOf()))))
        return false;

    WAVEFORMATEX* fmt = nullptr;
    if (FAILED(client_->GetMixFormat(&fmt))) return false;

    src_channels_ = fmt->nChannels;
    src_rate_     = fmt->nSamplesPerSec;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        is_float_ = true;
    } else if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt);
        is_float_ = IsEqualGUID(ex->SubFormat, kSubFmtFloat);
    }

    ready_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HRESULT hr = client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0, 0, fmt, nullptr);
    CoTaskMemFree(fmt);
    if (FAILED(hr)) return false;

    client_->SetEventHandle(ready_event_);
    return SUCCEEDED(client_->GetService(IID_PPV_ARGS(capture_.GetAddressOf())));
}

void AudioLoopback::Start() {
    if (!client_) return;
    client_->Start();
    running_ = true;
    thread_ = std::thread(&AudioLoopback::CaptureLoop, this);
}

void AudioLoopback::Stop() {
    running_ = false;
    if (ready_event_) SetEvent(ready_event_);
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    if (ready_event_) { CloseHandle(ready_event_); ready_event_ = nullptr; }
}

void AudioLoopback::CaptureLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_idx = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &task_idx);

    while (running_) {
        if (WaitForSingleObject(ready_event_, 200) == WAIT_TIMEOUT) continue;
        if (!running_) break;

        UINT32 frames_avail = 0;
        BYTE*  data  = nullptr;
        DWORD  flags = 0;

        while (SUCCEEDED(capture_->GetNextPacketSize(&frames_avail)) && frames_avail) {
            if (FAILED(capture_->GetBuffer(&data, &frames_avail, &flags, nullptr, nullptr)))
                break;

            AudioPacket pkt;
            pkt.timestamp_100ns = Timestamp100ns();
            pkt.sample_rate     = 48000;
            pkt.channels        = 2;
            pkt.bits_per_sample = 16;
            pkt.pcm.resize(frames_avail * 2 * sizeof(int16_t));
            auto* dst = reinterpret_cast<int16_t*>(pkt.pcm.data());

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(dst, dst + frames_avail * 2, int16_t{0});
            } else if (is_float_) {
                const auto* src = reinterpret_cast<const float*>(data);
                for (UINT32 i = 0; i < frames_avail; ++i) {
                    float L = (src_channels_ >= 1) ? src[i * src_channels_] : 0.f;
                    float R = (src_channels_ >= 2) ? src[i * src_channels_ + 1] : L;
                    dst[i * 2 + 0] = F32ToI16(L);
                    dst[i * 2 + 1] = F32ToI16(R);
                }
            } else {
                const auto* src = reinterpret_cast<const int16_t*>(data);
                for (UINT32 i = 0; i < frames_avail; ++i) {
                    int16_t L = src[i * src_channels_];
                    int16_t R = (src_channels_ >= 2) ? src[i * src_channels_ + 1] : L;
                    dst[i * 2 + 0] = L;
                    dst[i * 2 + 1] = R;
                }
            }

            capture_->ReleaseBuffer(frames_avail);
            engine_.GetLoopbackRing().push(std::move(pkt));
        }
    }

    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}
