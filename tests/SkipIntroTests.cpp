#include <cassert>
#include <vector>
#define SKIPINTRO_TESTS
#include "../source/SkipIntro/TheCrew.SkipIntro.cpp"
using namespace IntegratedController;
namespace FramerateMenu = NativeGfx;
#include "native/plugins/BootStateHookTests.h"
#include "native/plugins/IntroSkipTests.h"
#include "native/plugins/IntroScreenTests.h"
int main()
{
    BootStateHookRegression::Run();
    IntroSkipRegression::Run();
    IntroScreenRegression::Run();
}
