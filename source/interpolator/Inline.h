#pragma once
// Keep compile-time adapter composition from introducing an extra call layer.
// The fallback remains valid C++17 on compilers without an inlining attribute.
#if defined(_MSC_VER)
#define INTERPOLATOR_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define INTERPOLATOR_FORCEINLINE inline __attribute__((always_inline))
#else
#define INTERPOLATOR_FORCEINLINE inline
#endif
