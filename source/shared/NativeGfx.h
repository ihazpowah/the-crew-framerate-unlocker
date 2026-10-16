#pragma once
#include <cstddef>
#include <cstdint>
// Native GFx ABI and ownership helpers shared by independent plugins.
namespace NativeGfx
{
struct Value
{
    void* interfacePtr = nullptr;
    unsigned type = 0;
    unsigned padding = 0;
    union Payload
    {
        double number;
        const char* string;
        void* object;
        Payload() : object(nullptr) {}
    } data;
    uintptr_t extra = 0;
};
static_assert(sizeof(Value) == 32);
static_assert(offsetof(Value, data) == 16);

static Value Number(double number)
{
    Value value;
    value.type = 5;
    value.data.number = number;
    return value;
}

static void Release(Value* value)
{
    if ((value->type & 0x40) != 0 && value->interfacePtr)
    {
        using Fn = void(__fastcall*)(void*, Value*, void*);
        void** table = *reinterpret_cast<void***>(value->interfacePtr);
        reinterpret_cast<Fn>(table[0x10 / 8])(
            value->interfacePtr, value, value->data.object);
    }
    *value = Value{};
}

static bool PushBack(Value* array, const Value* value)
{
    if (!array->interfacePtr || !array->data.object)
        return false;
    using Fn = bool(__fastcall*)(void*, void*, const Value*);
    void** table = *reinterpret_cast<void***>(array->interfacePtr);
    return reinterpret_cast<Fn>(table[0x78 / 8])(
        array->interfacePtr, array->data.object, value);
}

static bool SetMember(Value* object, const char* name, const Value* value)
{
    if (!object->interfacePtr || !object->data.object)
        return false;
    using Fn = bool(__fastcall*)(void*, void*, const char*, const Value*, bool);
    void** table = *reinterpret_cast<void***>(object->interfacePtr);
    return reinterpret_cast<Fn>(table[0x28 / 8])(
        object->interfacePtr, object->data.object, name, value,
        (object->type & 0x8f) == 10);
}

}
