#pragma once

// Shared storage and D3D11 delivery for already-qualified pending light jobs.
// Provider, camera and clock admission remain with each light family.
namespace IntegratedController::LateLightUploads
{
template<class Provider>
struct PendingBuffer
{
    MappedKey key{};
    ID3D11Buffer* buffer = nullptr;
    Provider provider{};
    LONG64 expires = -1;
    bool sameFrame = false;
    unsigned char bytes[624]{};
};

// Each retained vector entry owns one buffer and context reference. Copies used
// during insertion/erasure transfer that ownership; release remains explicit,
// so a temporary copy never releases references still owned by the vector.
template<class Provider>
static void Release(PendingBuffer<Provider>& item)
{
    item.buffer->Release(); item.key.context->Release();
}
template<class Provider>
static void Clear(std::vector<PendingBuffer<Provider>>& pending)
{
    for (auto& item : pending) Release(item);
    pending.clear();
}
template<class Provider>
static void Invalidate(std::vector<PendingBuffer<Provider>>& pending, const MappedKey& key)
{
    for (auto it = pending.begin(); it != pending.end();)
        if (it->key == key) { Release(*it); it = pending.erase(it); }
        else ++it;
}

// Caller has admitted an immediate context and a non-null resource. On success,
// the returned QueryInterface reference is transferred to a retained entry.
static ID3D11Buffer* AcquireBuffer(ID3D11Resource* resource)
{
    ID3D11Buffer* buffer = nullptr;
    if (FAILED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))))
        return nullptr;
    D3D11_BUFFER_DESC desc{}; buffer->GetDesc(&desc);
    if (desc.ByteWidth != 624 || !(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) ||
        (desc.Usage != D3D11_USAGE_DYNAMIC && desc.Usage != D3D11_USAGE_DEFAULT))
    { buffer->Release(); return nullptr; }
    return buffer;
}
template<class Provider>
static void Retain(std::vector<PendingBuffer<Provider>>& pending, const MappedKey& key,
    ID3D11Buffer* buffer, const Provider& provider, const void* data, LONG64 expires, bool sameFrame)
{
    Invalidate(pending, key);
    PendingBuffer<Provider> item{};
    item.key = key; item.buffer = buffer; item.provider = provider; item.expires = expires;
    item.sameFrame = sameFrame;
    memcpy(item.bytes, data, sizeof(item.bytes));
    // Always derive refreshes from this provider's native parameters, even
    // when its first upload already contained a presentation correction.
    memcpy(item.bytes + 0x20, provider.parameters, sizeof(provider.parameters));
    key.context->AddRef(); pending.push_back(item);
}

// Caller retains the shared recursion guard and validates its current provider.
static void Upload(const MappedKey& key, ID3D11Buffer* buffer, const unsigned char* bytes)
{
    D3D11_BUFFER_DESC desc{}; buffer->GetDesc(&desc);
    if (desc.Usage == D3D11_USAGE_DYNAMIC)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(key.context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        { memcpy(mapped.pData, bytes, 624); key.context->Unmap(buffer, 0); }
    }
    else key.context->UpdateSubresource(buffer, 0, nullptr, bytes, 0, 0);
}
} // namespace IntegratedController::LateLightUploads
