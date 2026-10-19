#pragma once

// Opaque native provenance tokens. CPU publication does not own or dereference
// either token; serializer consumption uses the selected CPU camera identity.
struct PresentationKey
{
    void* context = nullptr;
    void* source = nullptr;
    bool operator==(const PresentationKey& other) const
    { return context==other.context && source==other.source; }
};
