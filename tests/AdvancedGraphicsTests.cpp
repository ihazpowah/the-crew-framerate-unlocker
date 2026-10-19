#include <cassert>
#define ADVANCED_GRAPHICS_TESTS
#include "../source/AdvancedGraphics/TheCrew.AdvancedGraphics.cpp"
using namespace IntegratedController;
namespace FramerateMenu = IntegratedController::GraphicsMenu;
#include "native/plugins/AdvancedGraphicsMenuTests.h"
#include "native/plugins/AdvancedGraphicsFirstOpenTests.h"
#include "native/plugins/CubemapResolutionTests.h"
#include "native/plugins/ShadowMapResolutionTests.h"
int main()
{
    setvbuf(stdout,nullptr,_IONBF,0);
    CheckSunShadowResolution();
    AdvancedGraphicsFirstOpenTests::Check();
    AdvancedGraphicsMenuTests::Check();
    CubemapResolutionTests::Check();
}
