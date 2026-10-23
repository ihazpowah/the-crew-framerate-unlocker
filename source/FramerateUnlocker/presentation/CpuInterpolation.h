#pragma once
#include "../../interpolator/Session.h"
#include <map>
#include <optional>
#include <psapi.h>

// Producer-owned native endpoints -> immutable render snapshot -> private CPU
// inputs. See docs/Architecture.md for the composition and native boundaries.
namespace IntegratedController
{
#include "cpu/CpuPresentationState.h"
#include "cpu/CpuReadability.h"
#include "cpu/CpuResourceLifetime.h"
#include "cpu/CpuPoseEncoding.h"
#include "cpu/CpuModelBindings.h"
#include "cpu/CpuVehicleCapture.h"
#include "cpu/CpuFrameCapture.h"
#include "cpu/CpuRenderDelivery.h"
#include "PlayerTagPresentation.h"

#include "cpu/CpuHookInstallation.h"

} // namespace IntegratedController
