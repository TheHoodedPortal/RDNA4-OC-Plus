#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rdna {

enum class ClockDomain : std::uint16_t { Gfx = 0, Soc = 1, Uclk = 2, Fclk = 3 };

struct ClockLimits { int minimum{}; int maximum{}; };
struct SmuSnapshot { std::wstring backend; ClockLimits gfx, fclk, soc; };
struct ClockRequest {
    ClockDomain domain{};
    std::optional<int> minimum;
    std::optional<int> maximum;
    bool releaseMaximum{};
};

struct Range { int minimum{}; int maximum{}; int step{ 1 }; };
struct TuningValue {
    int value{};
    Range range{};
    bool available{};
};
struct AdlxSnapshot {
    std::wstring gpuName;
    std::wstring version;
    TuningValue gfxMax, voltage, vramMax, memoryTiming, powerLimit;
    struct FanPoint { int temperature{}; int speed{}; bool operator==(const FanPoint&) const = default; };
    struct Fan {
        bool available{}, zeroRpmSupported{}, zeroRpm{}, zeroRpmDefault{};
        bool minSpeedSupported{};
        Range speedRange{}, temperatureRange{};
        TuningValue minSpeed, defaultMinSpeed;
        std::vector<FanPoint> curve, defaultCurve;
    } fan;
};
struct AdlxRequest {
    std::optional<int> gfxMax, voltage, vramMax, memoryTiming, powerLimit;
};
struct FanRequest {
    std::optional<bool> zeroRpm;
    std::optional<int> minSpeed;
    std::optional<std::vector<AdlxSnapshot::FanPoint>> curve;
};
struct GfxLinkedPlan { int target{}; std::optional<int> softMaximum; bool releaseMaximum{}; int offset{}; };

std::wstring DomainName(ClockDomain domain);
ClockLimits ReadSmuLimits(ClockDomain domain);
SmuSnapshot ReadSmu();
std::vector<std::wstring> ApplySmu(const std::vector<ClockRequest>& requests);
std::vector<std::wstring> RestoreSmuDefaults();
void ValidateClockRequest(const ClockRequest& request, ClockLimits limits);

AdlxSnapshot ReadAdlx();
std::vector<std::wstring> ApplyAdlx(const AdlxRequest& request);
std::vector<std::wstring> ApplyFan(const FanRequest& request);
std::vector<std::wstring> ResetFanToDefault();
void ValidateFanCurve(const std::vector<AdlxSnapshot::FanPoint>& curve, Range speed, Range temperature);

GfxLinkedPlan PlanLinkedGfx(int target, int currentOffset, ClockLimits native, Range offsetRange);
ClockLimits RecoverGfxBaseLimits(ClockLimits current, int appliedOffset);

} // namespace rdna
