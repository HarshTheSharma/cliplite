#include "EncoderThread.h"
#include "CaptureEngine.h"
#include "Config.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

// ── BT.601 BGRA → NV12 (integer arithmetic, in-place planes) ─────────────────
static void BgraToNv12(
    const uint8_t* bgra, int src_stride,
    uint8_t*       y_plane, int y_stride,
    uint8_t*       uv_plane, int uv_stride,
    int width, int height)
{
    for (int row = 0; row < height; ++row) {
        const uint8_t* s = bgra    + row * src_stride;
        uint8_t*       d = y_plane + row * y_stride;
        for (int col = 0; col < width; ++col, s += 4, ++d) {
            int b = s[0], g = s[1], r = s[2];
            *d = (uint8_t)((16 * 256 + 65 * r + 129 * g + 25 * b) >> 8);
        }
    }
    for (int row = 0; row < height / 2; ++row) {
        const uint8_t* s0 = bgra    + (row * 2)     * src_stride;
        const uint8_t* s1 = bgra    + (row * 2 + 1) * src_stride;
        uint8_t*       uv = uv_plane + row * uv_stride;
        for (int col = 0; col < width / 2; ++col, s0 += 8, s1 += 8, uv += 2) {
            int b = ((int)s0[0] + s0[4] + s1[0] + s1[4]) >> 2;
            int g = ((int)s0[1] + s0[5] + s1[1] + s1[5]) >> 2;
            int r = ((int)s0[2] + s0[6] + s1[2] + s1[6]) >> 2;
            uv[0] = (uint8_t)((128 * 256 - 38 * r - 74 * g + 112 * b) >> 8);
            uv[1] = (uint8_t)((128 * 256 + 112 * r - 94 * g - 18 * b) >> 8);
        }
    }
}

static std::wstring TimestampStem() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"clip_%04d%02d%02d_%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static bool WriteClip(
    CaptureEngine&                    engine,
    const std::vector<VideoFrame>&    frames,
    const std::vector<AudioPacket>&   loopback_pkts,
    const std::vector<AudioPacket>&   mic_pkts,
    const std::wstring&               output_path)
{
    if (frames.empty()) return false;

    const int W = frames.front().width;
    const int H = frames.front().height;
    if (W == 0 || H == 0) return false;

    const Config& cfg    = engine.GetConfig();
    const int     fps    = 30;
    const LONGLONG frame_dur = 10'000'000LL / fps;
    const uint64_t base_ts  = frames.front().timestamp_100ns;

    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.GetAddressOf(), 4))) return false;
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    ComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(output_path.c_str(), nullptr,
                                          attrs.Get(), writer.GetAddressOf())))
        return false;

    ComPtr<IMFMediaType> vid_out;
    if (FAILED(MFCreateMediaType(vid_out.GetAddressOf()))) return false;
    vid_out->SetGUID(MF_MT_MAJOR_TYPE,       MFMediaType_Video);
    vid_out->SetGUID(MF_MT_SUBTYPE,          MFVideoFormat_H264);
    vid_out->SetUINT32(MF_MT_AVG_BITRATE,    12'000'000);
    vid_out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vid_out.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(vid_out.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(vid_out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    ComPtr<IMFMediaType> vid_in;
    if (FAILED(MFCreateMediaType(vid_in.GetAddressOf()))) return false;
    vid_in->SetGUID(MF_MT_MAJOR_TYPE,       MFMediaType_Video);
    vid_in->SetGUID(MF_MT_SUBTYPE,          MFVideoFormat_NV12);
    vid_in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vid_in.Get(), MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(vid_in.Get(), MF_MT_FRAME_RATE, fps, 1);

    DWORD vid_stream = 0;
    if (FAILED(writer->AddStream(vid_out.Get(), &vid_stream)))         return false;
    if (FAILED(writer->SetInputMediaType(vid_stream, vid_in.Get(), nullptr))) return false;

    ComPtr<IMFMediaType> aud_out;
    if (FAILED(MFCreateMediaType(aud_out.GetAddressOf()))) return false;
    aud_out->SetGUID(MF_MT_MAJOR_TYPE,                   MFMediaType_Audio);
    aud_out->SetGUID(MF_MT_SUBTYPE,                      MFAudioFormat_AAC);
    aud_out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   48000);
    aud_out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         2);
    aud_out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
    aud_out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbps

    ComPtr<IMFMediaType> aud_in;
    if (FAILED(MFCreateMediaType(aud_in.GetAddressOf()))) return false;
    aud_in->SetGUID(MF_MT_MAJOR_TYPE,                   MFMediaType_Audio);
    aud_in->SetGUID(MF_MT_SUBTYPE,                      MFAudioFormat_PCM);
    aud_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   48000);
    aud_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         2);
    aud_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
    aud_in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,      4);
    aud_in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);

    DWORD aud_stream = 0;
    if (FAILED(writer->AddStream(aud_out.Get(), &aud_stream)))              return false;
    if (FAILED(writer->SetInputMediaType(aud_stream, aud_in.Get(), nullptr))) return false;

    if (FAILED(writer->BeginWriting())) return false;

    {
        const int y_sz  = W * H;
        const int uv_sz = W * (H / 2);
        std::vector<uint8_t> nv12(y_sz + uv_sz);

        for (const auto& vf : frames) {
            if (!vf.bgra || vf.bgra->empty()) continue;

            BgraToNv12(vf.bgra->data(),
                       W * 4,          // no row padding — written tightly in CaptureFrame
                       nv12.data(), W,
                       nv12.data() + y_sz, W,
                       W, H);

            ComPtr<IMFMediaBuffer> mbuf;
            if (FAILED(MFCreateMemoryBuffer((DWORD)nv12.size(), mbuf.GetAddressOf())))
                continue;
            BYTE* dst = nullptr;
            mbuf->Lock(&dst, nullptr, nullptr);
            std::memcpy(dst, nv12.data(), nv12.size());
            mbuf->Unlock();
            mbuf->SetCurrentLength((DWORD)nv12.size());

            ComPtr<IMFSample> sample;
            MFCreateSample(sample.GetAddressOf());
            sample->AddBuffer(mbuf.Get());
            sample->SetSampleTime((LONGLONG)(vf.timestamp_100ns - base_ts));
            sample->SetSampleDuration(frame_dur);
            writer->WriteSample(vid_stream, sample.Get());
        }
    }

    {
        const uint64_t end_ts  = frames.back().timestamp_100ns;
        const uint64_t span    = (end_ts > base_ts) ? (end_ts - base_ts) : 0;
        // +1600: one video frame of audio slack so the last frame isn't cut short.
        const size_t   n_samps = (size_t)(span * 48000ULL / 10'000'000ULL) + 1600;

        std::vector<int32_t> mix(n_samps * 2, 0);

        auto blend = [&](const std::vector<AudioPacket>& pkts) {
            for (const auto& pkt : pkts) {
                if (pkt.timestamp_100ns < base_ts) continue;
                const uint64_t off_s = (pkt.timestamp_100ns - base_ts) * 48000ULL / 10'000'000ULL;
                const size_t   n     = pkt.pcm.size() / sizeof(int16_t);
                const auto*    src   = reinterpret_cast<const int16_t*>(pkt.pcm.data());
                for (size_t i = 0; i < n && (off_s * 2 + i) < mix.size(); ++i)
                    mix[off_s * 2 + i] += (int32_t)src[i];
            }
        };
        blend(loopback_pkts);
        if (cfg.enable_mic) blend(mic_pkts);

        const size_t  chunk  = 960; // 20 ms at 48 kHz
        const LONGLONG chunk_dur = (LONGLONG)chunk * 10'000'000LL / 48000;

        for (size_t s = 0; s < n_samps; s += chunk) {
            const size_t cnt = std::min(chunk, n_samps - s);
            std::vector<int16_t> pcm(cnt * 2);
            for (size_t i = 0; i < cnt * 2; ++i) {
                int32_t v = std::max(std::min(mix[s * 2 + i], 32767), -32768);
                pcm[i] = (int16_t)v;
            }
            ComPtr<IMFMediaBuffer> abuf;
            if (FAILED(MFCreateMemoryBuffer((DWORD)(pcm.size() * 2), abuf.GetAddressOf())))
                continue;
            BYTE* p = nullptr;
            abuf->Lock(&p, nullptr, nullptr);
            std::memcpy(p, pcm.data(), pcm.size() * 2);
            abuf->Unlock();
            abuf->SetCurrentLength((DWORD)(pcm.size() * 2));

            ComPtr<IMFSample> asample;
            MFCreateSample(asample.GetAddressOf());
            asample->AddBuffer(abuf.Get());
            asample->SetSampleTime((LONGLONG)s * 10'000'000LL / 48000);
            asample->SetSampleDuration(chunk_dur);
            writer->WriteSample(aud_stream, asample.Get());
        }
    }

    return SUCCEEDED(writer->Finalize());
}

EncoderThread::EncoderThread(CaptureEngine& engine) : engine_(engine) {}
EncoderThread::~EncoderThread() { Stop(); }

void EncoderThread::Start() {
    running_ = true;
    thread_ = std::thread(&EncoderThread::EncoderLoop, this);
}

void EncoderThread::Stop() {
    { std::lock_guard lk(mu_); running_ = false; }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EncoderThread::RequestClip() {
    { std::lock_guard lk(mu_); clip_requested_ = true; }
    cv_.notify_one();
}

void EncoderThread::EncoderLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true) {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [this] { return clip_requested_.load() || !running_.load(); });
        if (!running_) break;
        clip_requested_ = false;
        lk.unlock();
        SaveClip();
    }
    CoUninitialize();
}

void EncoderThread::SaveClip() {
    const Config& cfg = engine_.GetConfig();
    const int     dur = cfg.duration_seconds;
    const int     fps = 30;

    std::vector<VideoFrame>  vid_frames;
    std::vector<AudioPacket> loop_pkts, mic_pkts;

    engine_.GetVideoRing().snapshot(vid_frames,  (size_t)(dur * fps + fps));
    engine_.GetLoopbackRing().snapshot(loop_pkts, (size_t)(dur * 200));
    engine_.GetMicRing().snapshot(mic_pkts,       (size_t)(dur * 200));

    if (vid_frames.empty()) return;

    const uint64_t clip_start = vid_frames.back().timestamp_100ns
                               - (uint64_t)dur * 10'000'000ULL;

    auto trim_vid = [&] {
        auto it = std::lower_bound(vid_frames.begin(), vid_frames.end(), clip_start,
            [](const VideoFrame& f, uint64_t t) { return f.timestamp_100ns < t; });
        vid_frames.erase(vid_frames.begin(), it);
    };
    auto trim_aud = [&](std::vector<AudioPacket>& pkts) {
        auto it = std::lower_bound(pkts.begin(), pkts.end(), clip_start,
            [](const AudioPacket& p, uint64_t t) { return p.timestamp_100ns < t; });
        pkts.erase(pkts.begin(), it);
    };
    trim_vid();
    trim_aud(loop_pkts);
    trim_aud(mic_pkts);

    if (vid_frames.empty()) return;

    const auto stem      = TimestampStem();
    const auto temp_path = (Config::TempDirPath() / (stem + L".mp4.part")).wstring();
    const auto fin_path  = (std::filesystem::path(cfg.save_path) / (stem + L".mp4")).wstring();

    if (WriteClip(engine_, vid_frames, loop_pkts, mic_pkts, temp_path)) {
        MoveFileExW(temp_path.c_str(), fin_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } else {
        DeleteFileW(temp_path.c_str());
    }
}
