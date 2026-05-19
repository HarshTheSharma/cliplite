#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>

using Microsoft::WRL::ComPtr;

// Pre-allocated STAGING textures for CPU readback of DXGI captured frames.
// Returned via shared_ptr custom deleter so callers don't manage lifetimes.
// Acquire() returns empty when the pool is exhausted; caller should drop the frame.
class TexturePool {
public:
    // width/height: desktop resolution; count: ring-buffer depth.
    bool Initialize(ID3D11Device* device, int width, int height, int count);

    int    Width()     const noexcept { return width_; }
    int    Height()    const noexcept { return height_; }
    int    Capacity()  const noexcept { return static_cast<int>(all_.size()); }
    size_t FreeCount() const noexcept {
        std::lock_guard lk(mu_);
        return free_.size();
    }

    // Acquires one texture from the free list.
    // tex_out receives the raw pointer (valid while the returned shared_ptr lives).
    // Returns empty shared_ptr (and sets tex_out = nullptr) when pool is exhausted.
    std::shared_ptr<void> Acquire(ID3D11Texture2D*& tex_out);

private:
    void Release(ID3D11Texture2D* tex);

    int width_  = 0;
    int height_ = 0;
    std::vector<ComPtr<ID3D11Texture2D>> all_;
    std::queue<ID3D11Texture2D*>         free_;
    mutable std::mutex                   mu_;
};
