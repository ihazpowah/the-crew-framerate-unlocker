#pragma once

// Windows and C++ dependencies shared by the ASI implementation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <string>
#include <iterator>
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <cwchar>
#include <cstdio>
#include <utility>
#include <mutex>
#include <memory>
#include <intrin.h>
#include "MinHook.h"

#pragma comment(lib, "winmm.lib")
