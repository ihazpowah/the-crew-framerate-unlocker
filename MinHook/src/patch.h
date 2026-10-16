/*
 *  MinHook - The Minimalistic API Hooking Library for x64/x86
 *  Copyright (C) 2009-2017 Tsuda Kageyu.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* MinHook local patch transaction; covered by the dependency license. */
#pragma once
#include <intrin.h>

// The caller holds MinHook's lock and keeps other threads suspended. A native
// prologue/hotpatch can straddle two pages with different original protections.
static BOOL PatchCode(LPBYTE target, const BYTE* replacement, SIZE_T size)
{
    SYSTEM_INFO system;
    BYTE previous[8];
    LPBYTE addresses[2];
    SIZE_T lengths[2];
    DWORD protections[2], ignored;
    UINT count = 1, i, writable = 0;
    BOOL restored = TRUE;

    if (!size || size > sizeof(previous)) return FALSE;
    GetSystemInfo(&system);
    addresses[0] = target;
    lengths[0] = system.dwPageSize - ((ULONG_PTR)target % system.dwPageSize);
    if (lengths[0] < size)
    {
        count = 2;
        addresses[1] = target + lengths[0];
        lengths[1] = size - lengths[0];
    }
    else lengths[0] = size;
    memcpy(previous, target, size);

    for (i = 0; i < count; ++i)
    {
        if (!VirtualProtect(addresses[i], lengths[i], PAGE_EXECUTE_READWRITE, &protections[i]))
            break;
        ++writable;
    }
    if (writable != count)
    {
        // Nothing was written. Restore any pages already opened for writing.
        for (i = 0; i < writable; ++i)
            if (!VirtualProtect(addresses[i], lengths[i], protections[i], &ignored)) restored = FALSE;
        if (!restored) __fastfail(FAST_FAIL_FATAL_APP_EXIT);
        return FALSE;
    }

    memcpy(target, replacement, size);
    for (i = 0; i < count; ++i)
        if (!VirtualProtect(addresses[i], lengths[i], protections[i], &ignored)) restored = FALSE;
    if (restored && FlushInstructionCache(GetCurrentProcess(), target, size)) return TRUE;

    // Restore the actual pre-operation bytes, including an enabled detour when
    // disabling failed. Hook state is unchanged until this transaction succeeds.
    for (i = 0; i < count; ++i)
        if (!VirtualProtect(addresses[i], lengths[i], PAGE_EXECUTE_READWRITE, &ignored))
            __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    memcpy(target, previous, size);
    restored = TRUE;
    for (i = 0; i < count; ++i)
        if (!VirtualProtect(addresses[i], lengths[i], protections[i], &ignored)) restored = FALSE;
    if (!restored || !FlushInstructionCache(GetCurrentProcess(), target, size))
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    return FALSE;
}
