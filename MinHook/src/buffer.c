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

#include <windows.h>
#include "buffer.h"

// One private code page per trampoline. Published pages are never reopened for
// writes when other hooks are created or removed.
#define MEMORY_BLOCK_SIZE 0x1000

// Max range for seeking a memory block. (= 1024MB)
#define MAX_MEMORY_RANGE 0x40000000

// Memory protection flags to check the executable address.
#define PAGE_EXECUTE_FLAGS \
    (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

// Allocator records live on a separate, non-executable private heap.
typedef struct _MEMORY_BLOCK
{
    struct _MEMORY_BLOCK *pNext;
    LPVOID pCode;
    BOOL sealed;
} MEMORY_BLOCK, *PMEMORY_BLOCK;

//-------------------------------------------------------------------------
// Global Variables:
//-------------------------------------------------------------------------

// First element of the memory block list.
static PMEMORY_BLOCK g_pMemoryBlocks;
static HANDLE g_bufferHeap;

//-------------------------------------------------------------------------
BOOL InitializeBuffer(VOID)
{
    // No HEAP_CREATE_ENABLE_EXECUTE: metadata is writable, never executable.
    g_bufferHeap = HeapCreate(0, 0, 0);
    return g_bufferHeap != NULL;
}

//-------------------------------------------------------------------------
VOID UninitializeBuffer(VOID)
{
    PMEMORY_BLOCK pBlock = g_pMemoryBlocks;
    g_pMemoryBlocks = NULL;

    while (pBlock)
    {
        PMEMORY_BLOCK pNext = pBlock->pNext;
        VirtualFree(pBlock->pCode, 0, MEM_RELEASE);
        HeapFree(g_bufferHeap, 0, pBlock);
        pBlock = pNext;
    }
    if (g_bufferHeap != NULL)
        HeapDestroy(g_bufferHeap);
    g_bufferHeap = NULL;
}

//-------------------------------------------------------------------------
#if defined(_M_X64) || defined(__x86_64__)
static LPVOID FindPrevFreeRegion(LPVOID pAddress, LPVOID pMinAddr, DWORD dwAllocationGranularity)
{
    ULONG_PTR tryAddr = (ULONG_PTR)pAddress;

    // Round down to the allocation granularity.
    tryAddr -= tryAddr % dwAllocationGranularity;

    // Start from the previous allocation granularity multiply.
    tryAddr -= dwAllocationGranularity;

    while (tryAddr >= (ULONG_PTR)pMinAddr)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)tryAddr, &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_FREE)
            return (LPVOID)tryAddr;

        if ((ULONG_PTR)mbi.AllocationBase < dwAllocationGranularity)
            break;

        tryAddr = (ULONG_PTR)mbi.AllocationBase - dwAllocationGranularity;
    }

    return NULL;
}
#endif

//-------------------------------------------------------------------------
#if defined(_M_X64) || defined(__x86_64__)
static LPVOID FindNextFreeRegion(LPVOID pAddress, LPVOID pMaxAddr, DWORD dwAllocationGranularity)
{
    ULONG_PTR tryAddr = (ULONG_PTR)pAddress;

    // Round down to the allocation granularity.
    tryAddr -= tryAddr % dwAllocationGranularity;

    // Start from the next allocation granularity multiply.
    tryAddr += dwAllocationGranularity;

    while (tryAddr <= (ULONG_PTR)pMaxAddr)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)tryAddr, &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_FREE)
            return (LPVOID)tryAddr;

        tryAddr = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

        // Round up to the next allocation granularity.
        tryAddr += dwAllocationGranularity - 1;
        tryAddr -= tryAddr % dwAllocationGranularity;
    }

    return NULL;
}
#endif

//-------------------------------------------------------------------------
static LPVOID AllocateCodePage(LPVOID pOrigin)
{
    LPVOID pCode = NULL;
#if defined(_M_X64) || defined(__x86_64__)
    ULONG_PTR minAddr;
    ULONG_PTR maxAddr;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    minAddr = (ULONG_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (ULONG_PTR)si.lpMaximumApplicationAddress;

    // pOrigin +/- 1GB, preserving x64 relative-jump reachability.
    if ((ULONG_PTR)pOrigin > MAX_MEMORY_RANGE && minAddr < (ULONG_PTR)pOrigin - MAX_MEMORY_RANGE)
        minAddr = (ULONG_PTR)pOrigin - MAX_MEMORY_RANGE;

    if (maxAddr > (ULONG_PTR)pOrigin + MAX_MEMORY_RANGE)
        maxAddr = (ULONG_PTR)pOrigin + MAX_MEMORY_RANGE;

    // Make room for MEMORY_BLOCK_SIZE bytes.
    maxAddr -= MEMORY_BLOCK_SIZE - 1;
#endif

#if defined(_M_X64) || defined(__x86_64__)
    // Alloc a new block above if not found.
    {
        LPVOID pAlloc = pOrigin;
        while ((ULONG_PTR)pAlloc >= minAddr)
        {
            pAlloc = FindPrevFreeRegion(pAlloc, (LPVOID)minAddr, si.dwAllocationGranularity);
            if (pAlloc == NULL)
                break;

            pCode = VirtualAlloc(
                pAlloc, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (pCode != NULL)
                break;
        }
    }

    // Alloc a new block below if not found.
    if (pCode == NULL)
    {
        LPVOID pAlloc = pOrigin;
        while ((ULONG_PTR)pAlloc <= maxAddr)
        {
            pAlloc = FindNextFreeRegion(pAlloc, (LPVOID)maxAddr, si.dwAllocationGranularity);
            if (pAlloc == NULL)
                break;

            pCode = VirtualAlloc(
                pAlloc, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (pCode != NULL)
                break;
        }
    }
#else
    // In x86 mode, a memory block can be placed anywhere.
    pCode = VirtualAlloc(
        NULL, MEMORY_BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#endif

    return pCode;
}

//-------------------------------------------------------------------------
LPVOID AllocateBuffer(LPVOID pOrigin)
{
    PMEMORY_BLOCK pBlock;
    if (g_bufferHeap == NULL)
        return NULL;
    pBlock = (PMEMORY_BLOCK)HeapAlloc(g_bufferHeap, 0, sizeof(MEMORY_BLOCK));
    if (pBlock == NULL)
        return NULL;

    pBlock->pCode = AllocateCodePage(pOrigin);
    if (pBlock->pCode == NULL)
    {
        HeapFree(g_bufferHeap, 0, pBlock);
        return NULL;
    }
    pBlock->sealed = FALSE;
    pBlock->pNext = g_pMemoryBlocks;
    g_pMemoryBlocks = pBlock;
#ifdef _DEBUG
    // Fill the slot with INT3 for debugging.
    memset(pBlock->pCode, 0xCC, MEMORY_SLOT_SIZE);
#endif
    return pBlock->pCode;
}

//-------------------------------------------------------------------------
BOOL SealBuffer(LPVOID pBuffer)
{
    PMEMORY_BLOCK pBlock;
    for (pBlock = g_pMemoryBlocks; pBlock != NULL; pBlock = pBlock->pNext)
    {
        if (pBlock->pCode == pBuffer && !pBlock->sealed)
        {
            DWORD oldProtect;
            if (!VirtualProtect(pBuffer, MEMORY_BLOCK_SIZE, PAGE_EXECUTE_READ, &oldProtect) ||
                !FlushInstructionCache(GetCurrentProcess(), pBuffer, MEMORY_BLOCK_SIZE))
                return FALSE;
            pBlock->sealed = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

//-------------------------------------------------------------------------
VOID FreeBuffer(LPVOID pBuffer)
{
    PMEMORY_BLOCK pBlock = g_pMemoryBlocks;
    PMEMORY_BLOCK pPrev = NULL;

    while (pBlock != NULL)
    {
        if (pBlock->pCode == pBuffer)
        {
            if (pPrev)
                pPrev->pNext = pBlock->pNext;
            else
                g_pMemoryBlocks = pBlock->pNext;
            // No writes to sealed code, including in debug builds.
            VirtualFree(pBlock->pCode, 0, MEM_RELEASE);
            HeapFree(g_bufferHeap, 0, pBlock);
            break;
        }

        pPrev = pBlock;
        pBlock = pBlock->pNext;
    }
}

//-------------------------------------------------------------------------
BOOL IsExecutableAddress(LPVOID pAddress)
{
    MEMORY_BASIC_INFORMATION mi;
    return VirtualQuery(pAddress, &mi, sizeof(mi)) && mi.State == MEM_COMMIT &&
           (mi.Protect & PAGE_EXECUTE_FLAGS);
}
