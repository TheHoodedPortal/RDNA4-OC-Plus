#include "../src/hardware.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures{};
void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
template<class Fn>
void Throws(Fn&& function, const char* name) {
    try { function(); Check(false, name); }
    catch (const std::exception&) {}
}
}

int main() {
    using namespace rdna;
    const ClockLimits stock{ 500,3300 };
    ValidateClockRequest({ ClockDomain::Gfx,600,3200,false }, stock);
    ValidateClockRequest({ ClockDomain::Gfx,{}, {},true }, stock);
    Check(true, "valid SMU limits accepted");
    Throws([&] { ValidateClockRequest({ ClockDomain::Gfx,3400,{},false }, stock); }, "out-of-range SMU minimum rejected");
    Throws([&] { ValidateClockRequest({ ClockDomain::Gfx,3200,3100,false }, stock); }, "inverted SMU limits rejected");
    Throws([&] { ValidateClockRequest({ ClockDomain::Soc,{}, {},true }, stock); }, "non-GFX automatic release rejected");
    Throws([&] { ValidateClockRequest({ ClockDomain::Gfx,{},3100,true }, stock); }, "release plus numeric maximum rejected");

    const Range offset{ -500,1000,1 };
    Check(RecoverGfxBaseLimits({500,4450},1000).maximum==3450, "applied positive ADLX offset removed from live GFX DPM ceiling");
    Check(RecoverGfxBaseLimits({500,3350},-100).maximum==3450, "applied negative ADLX offset removed from live GFX DPM ceiling");
    Check(RecoverGfxBaseLimits({4450,4450},1000).minimum==3450, "high live GFX soft minimum does not abort startup range recovery");
    Throws([&] { RecoverGfxBaseLimits({500,600},1000); }, "inconsistent live GFX/ADLX ranges rejected");
    auto raised = PlanLinkedGfx(3850, 0, stock, offset);
    Check(raised.releaseMaximum && !raised.softMaximum && raised.target == 3850 && raised.offset == 550, "GFX extension releases SMU ceiling and computes ADLX offset");
    auto backedOff = PlanLinkedGfx(3600, raised.offset, stock, offset);
    Check(backedOff.releaseMaximum && !backedOff.softMaximum && backedOff.target == 3600 && backedOff.offset == 550, "backing off GFX maximum preserves the raised ADLX offset");
    auto native = PlanLinkedGfx(3200, backedOff.offset, stock, offset);
    Check(!native.releaseMaximum && native.softMaximum == 3200 && native.offset == 550, "native-range target keeps numeric SMU maximum without lowering ADLX offset");
    auto stepped = PlanLinkedGfx(3551, 0, stock, { -500,1000,100 });
    Check(stepped.target == 3600 && stepped.offset == 300, "GFX extension snaps to a supported coarse ADLX offset step");
    auto coarseBackoff = PlanLinkedGfx(3451, stepped.offset, stock, { -500,1000,100 });
    Check(coarseBackoff.target == 3500 && coarseBackoff.offset == 300, "coarse-step backoff stays aligned and retains offset");
    Throws([&] { PlanLinkedGfx(400, 0, stock, offset); }, "GFX target below DPM minimum rejected");
    Throws([&] { PlanLinkedGfx(4320, 0, stock, { -500,1000,100 }); }, "GFX target above linked driver range rejected");

    const Range fanPercent{ 20,100,5 }, fanTemperature{ 30,100,1 };
    const std::vector<AdlxSnapshot::FanPoint> fanCurve{{30,25},{50,35},{70,55},{90,85}};
    ValidateFanCurve(fanCurve, fanPercent, fanTemperature);
    Check(true, "valid monotonic fan curve accepted");
    Throws([&] { ValidateFanCurve({{30,25},{50,45},{70,40}}, fanPercent, fanTemperature); }, "decreasing fan speed rejected");
    Throws([&] { ValidateFanCurve({{30,25},{30,45}}, fanPercent, fanTemperature); }, "duplicate fan temperatures rejected");
    Throws([&] { ValidateFanCurve({{30,25},{50,105}}, fanPercent, fanTemperature); }, "fan speed outside live range rejected");

    if (failures) {
        std::cerr << failures << " core test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All core validation and GFX-link policy tests passed.\n";
    return EXIT_SUCCESS;
}
