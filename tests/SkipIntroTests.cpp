#include <cassert>
#include <vector>
#define SKIPINTRO_TESTS
#include "../source/SkipIntro/TheCrew.SkipIntro.cpp"
using namespace IntegratedController;
namespace FramerateMenu = NativeGfx;
#include "BootStateHookTests.h"
#include "IntroSkipTests.h"
#include "IntroScreenTests.h"
int main()
{
    BootStateHookRegression::Run();
    IntroSkipRegression::Run();
    IntroScreenRegression::Run();
}
