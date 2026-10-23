#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "GameBuildAddresses.h"

// Canonical RVAs name the Ubisoft/Uplay executable throughout the plugins. Each ASI
// selects its own immutable executable profile before publishing any hooks.
namespace GameBuild
{
static std::atomic<bool> alternate{false};
static bool Select(uint32_t imageSize, uint32_t timestamp)
{
    if (imageSize == 0x535a000) { alternate.store(false); return true; }
    if (imageSize == 0x5252000 && timestamp == 0x58bf0020)
    { alternate.store(true); return true; }
    return false;
}
static uintptr_t ImageSize() { return alternate.load() ? 0x5252000 : 0x535a000; }
static const AddressPair* Find(uintptr_t canonical)
{
    size_t first = 0, last = sizeof(addresses) / sizeof(addresses[0]);
    while (first < last)
    {
        const size_t mid = first + (last - first) / 2;
        if (addresses[mid].canonical < canonical) first = mid + 1;
        else last = mid;
    }
    return first < sizeof(addresses) / sizeof(addresses[0]) &&
        addresses[first].canonical == canonical ? &addresses[first] : nullptr;
}
static uintptr_t Rva(uintptr_t canonical)
{
    if (!alternate.load()) return canonical;
    const auto* pair = Find(canonical);
    // An unaudited address never falls back to the other executable's layout.
    return pair ? pair->alternate : 0;
}
static uintptr_t At(uintptr_t base, uintptr_t canonical)
{
    if (!alternate.load()) return base + canonical;
    const uintptr_t rva = Rva(canonical);
    return rva ? base + rva : 0;
}
static uintptr_t Canonical(uintptr_t rva)
{
    if (!alternate.load()) return rva;
    size_t first = 0, last = sizeof(reverseAddresses) / sizeof(reverseAddresses[0]);
    while (first < last)
    {
        const size_t mid = first + (last - first) / 2;
        if (reverseAddresses[mid].alternate < rva) first = mid + 1;
        else last = mid;
    }
    return first < sizeof(reverseAddresses) / sizeof(reverseAddresses[0]) &&
        reverseAddresses[first].alternate == rva ? reverseAddresses[first].canonical : UINTPTR_MAX;
}
static const uint8_t* Signature(uintptr_t canonical, const uint8_t* expected, size_t size)
{
    if (alternate.load())
        for (const auto& signature : signatures)
            if (signature.canonical == canonical && size <= signature.size &&
                !memcmp(expected, signature.original, size)) return signature.alternate;
    return expected;
}
static bool Matches(uintptr_t canonical, const void* actual, const void* expected, size_t size)
{
    return !memcmp(actual, Signature(canonical, static_cast<const uint8_t*>(expected), size), size);
}
}
