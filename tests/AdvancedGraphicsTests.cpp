#include <cassert>
#define ADVANCED_GRAPHICS_TESTS
#include "../source/AdvancedGraphics/TheCrew.AdvancedGraphics.cpp"
using namespace IntegratedController;
namespace FramerateMenu = IntegratedController::GraphicsMenu;
#include "AdvancedGraphicsMenuTests.h"
#include "AdvancedGraphicsFirstOpenTests.h"
#include "CubemapResolutionTests.h"
#include "ShadowMapResolutionTests.h"
int main()
{
    setvbuf(stdout,nullptr,_IONBF,0);
    CheckSunShadowResolution();
    AdvancedGraphicsFirstOpenTests::Check();
    AdvancedGraphicsMenuTests::Check();
    CubemapResolutionTests::Check();
}
