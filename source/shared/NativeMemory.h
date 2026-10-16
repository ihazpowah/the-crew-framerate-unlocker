#pragma once

// Exception-contained native memory access and callback replacement.
namespace IntegratedController
{
template <typename T>
static bool SafeRead(uintptr_t address, T* value)
{
    __try
    {
        *value = *reinterpret_cast<const T*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        memset(value, 0, sizeof(T));
        return false;
    }
}

static bool SafeCopyFrom(uintptr_t address, void* destination, size_t size)
{
    __try
    {
        memcpy(destination, reinterpret_cast<const void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        memset(destination, 0, size);
        return false;
    }
}

static bool SafeCopyTo(uintptr_t address, const void* source, size_t size)
{
    __try
    {
        memcpy(reinterpret_cast<void*>(address), source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool ReadPtr(uintptr_t address, uintptr_t* value)
{
    if (!SafeRead(address, value))
        return false;
    return *value != 0;
}

static bool Finite(float value)
{
    return std::isfinite(static_cast<double>(value));
}

static LONG64 ReadI64(uintptr_t address)
{
    LONG64 value = 0;
    SafeRead(address, &value);
    return value;
}

static void WriteI64(uintptr_t address, LONG64 value)
{
    __try
    {
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(address), value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void WritePtr(uintptr_t address, uintptr_t value)
{
    __try
    {
        *reinterpret_cast<uintptr_t*>(address) = value;
        MemoryBarrier();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static bool ReplaceCallback(uintptr_t address, uintptr_t expected, uintptr_t replacement)
{
    if (address % alignof(void*) != 0)
        return false;
    __try
    {
        return InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(address), reinterpret_cast<void*>(replacement),
            reinterpret_cast<void*>(expected)) == reinterpret_cast<void*>(expected);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace IntegratedController
