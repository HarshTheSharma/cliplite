#include "TexturePool.h"

bool TexturePool::Initialize(ID3D11Device* device, int width, int height, int count) {
    width_  = width;
    height_ = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width     = (UINT)width;
    desc.Height    = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage          = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    all_.reserve(count);
    for (int i = 0; i < count; ++i) {
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf())))
            break; // partial pool: ring buffer wraps sooner than configured duration
        free_.push(tex.Get());
        all_.push_back(std::move(tex));
    }

    return !all_.empty();
}

std::shared_ptr<void> TexturePool::Acquire(ID3D11Texture2D*& tex_out) {
    std::lock_guard lk(mu_);
    if (free_.empty()) {
        tex_out = nullptr;
        return {};
    }
    tex_out = free_.front();
    free_.pop();
    return std::shared_ptr<void>(tex_out, [this](void* p) {
        Release(static_cast<ID3D11Texture2D*>(p));
    });
}

void TexturePool::Release(ID3D11Texture2D* tex) {
    std::lock_guard lk(mu_);
    free_.push(tex);
}
