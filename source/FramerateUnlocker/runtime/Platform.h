#pragma once

// Windows, D3D11 and C++ dependencies shared by the ASI implementation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <string>
#include <iterator>
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <cwchar>
#include <cstdio>
#include <utility>
#include <mutex>
#include <intrin.h>
#include "MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")
