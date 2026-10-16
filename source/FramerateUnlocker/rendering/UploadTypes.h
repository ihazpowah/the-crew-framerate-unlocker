#pragma once

// Native D3D11 upload signatures and context/resource identity types.
typedef HRESULT(STDMETHODCALLTYPE* MapFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
    D3D11_MAPPED_SUBRESOURCE*);
typedef void(STDMETHODCALLTYPE* UnmapFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT);
typedef void(STDMETHODCALLTYPE* UpdateSubresourceFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*,
    const void*, UINT, UINT);

struct MappedKey
{
    ID3D11DeviceContext* context;
    ID3D11Resource* resource;
    bool operator==(const MappedKey& other) const
    {
        return context == other.context && resource == other.resource;
    }
};

struct MappedKeyHash
{
    size_t operator()(const MappedKey& value) const
    {
        // MSVC unordered_map uses power-of-two buckets. Resource << 7 left
        // the low seven bits identical for every resource on one context,
        // clustering tracker lookups into very few buckets. Avalanche both
        // pointer identities so aligned allocations also spread in low bits.
        uint64_t hash = (reinterpret_cast<uintptr_t>(value.context) >> 4) ^
            (reinterpret_cast<uintptr_t>(value.resource) * 0x9e3779b97f4a7c15ull);
        hash ^= hash >> 30;
        hash *= 0xbf58476d1ce4e5b9ull;
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebull;
        hash ^= hash >> 31;
        return static_cast<size_t>(hash);
    }
};

struct MappedBuffer
{
    void* data;
    UINT size;
};
