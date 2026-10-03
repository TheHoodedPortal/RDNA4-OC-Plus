#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devpkey.h>
#include <devguid.h>
#include "hardware.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#pragma comment(lib, "setupapi.lib")

namespace rdna {
namespace {
constexpr DWORD kInpOutResource = 101;
constexpr std::uint32_t kMessageRegister = 0x03B10A08;
constexpr std::uint32_t kParameterRegister = 0x03B10A48;
constexpr std::uint32_t kResponseRegister = 0x03B10A68;
constexpr std::uint32_t kResponseOk = 1;
constexpr std::uint32_t kGetSmuVersion = 0x02;
constexpr std::uint32_t kGetMinDpmFreq = 0x1D;
constexpr std::uint32_t kGetMaxDpmFreq = 0x1E;
constexpr std::uint32_t kSetSoftMinByFreq = 0x19;
constexpr std::uint32_t kSetSoftMaxByFreq = 0x1A;
std::mutex g_mailboxMutex;

std::wstring Number(std::uint64_t value) { return std::to_wstring(value); }
std::wstring Hex(std::uint32_t value, unsigned digits = 2) {
    wchar_t text[16]{};
    swprintf_s(text, L"%0*X", digits, value);
    return text;
}
std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string text(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, text.data(), count, nullptr, nullptr);
    text.pop_back();
    return text;
}
std::string HexNarrow(std::uint32_t value, unsigned digits = 2) {
    return Narrow(Hex(value, digits));
}
std::string HexNarrow(std::uint64_t value) {
    wchar_t text[24]{};
    swprintf_s(text, L"%llX", static_cast<unsigned long long>(value));
    return Narrow(text);
}

struct PciLocation { unsigned bus{}, device{}, function{}; };

PciLocation ParseLocation(const std::wstring& text) {
    const wchar_t* current = text.c_str();
    std::array<unsigned long, 3> numbers{};
    for (auto& value : numbers) {
        while (*current && (*current < L'0' || *current > L'9')) ++current;
        if (!*current) throw std::runtime_error("Windows did not return a usable PCI location for the AMD GPU.");
        wchar_t* end{};
        value = wcstoul(current, &end, 10);
        if (end == current || value > std::numeric_limits<unsigned>::max())
            throw std::runtime_error("The display adapter's PCI location is invalid.");
        current = end;
    }
    return { static_cast<unsigned>(numbers[0]), static_cast<unsigned>(numbers[1]), static_cast<unsigned>(numbers[2]) };
}

PciLocation FindAmdLocation() {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not enumerate display adapters.");
    struct DestroySet { HDEVINFO value; ~DestroySet() { SetupDiDestroyDeviceInfoList(value); } } cleanup{ set };

    std::optional<PciLocation> firstAmd, preferred;
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &device); ++index) {
        std::array<wchar_t, 512> instance{};
        if (!SetupDiGetDeviceInstanceIdW(set, &device, instance.data(), static_cast<DWORD>(instance.size()), nullptr)) continue;
        if (wcsstr(instance.data(), L"VEN_1002") == nullptr) continue;
        // Read the location string using the required-buffer-size query first.
        std::wstring locationText;
        DEVPROPTYPE type{};
        DWORD bytes{};
        SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_LocationInfo, &type, nullptr, 0, &bytes, 0);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || type != DEVPROP_TYPE_STRING || bytes < sizeof(wchar_t)) continue;
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
        if (!SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_LocationInfo, &type,
            reinterpret_cast<PBYTE>(buffer.data()), bytes, nullptr, 0)) continue;
        locationText.assign(buffer.data());
        try {
            const PciLocation parsed = ParseLocation(locationText);
            if (!firstAmd) firstAmd = parsed;
            std::array<wchar_t, 512> name{};
            if (SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_FriendlyName, &type,
                reinterpret_cast<PBYTE>(name.data()), static_cast<DWORD>(name.size() * sizeof(wchar_t)), nullptr, 0) &&
                wcsstr(name.data(), L"9070") != nullptr) preferred = parsed;
        } catch (...) {}
    }
    if (preferred) return *preferred;
    if (firstAmd) return *firstAmd;
    throw std::runtime_error("Windows did not return a usable PCI location for an enabled AMD display adapter.");
}

std::uint64_t FindEcamBase() {
    constexpr DWORD kAcpi = 0x41435049; // ACPI
    constexpr DWORD kMcfg = 0x4746434D; // MCFG
    const DWORD size = GetSystemFirmwareTable(kAcpi, kMcfg, nullptr, 0);
    if (size < 60) throw std::runtime_error("The ACPI MCFG table is unavailable.");
    std::vector<std::uint8_t> table(size);
    if (GetSystemFirmwareTable(kAcpi, kMcfg, table.data(), size) != size)
        throw std::runtime_error("Could not read the ACPI MCFG table.");
    std::uint64_t base{};
    std::uint16_t segment{};
    memcpy(&base, table.data() + 44, sizeof(base));
    memcpy(&segment, table.data() + 52, sizeof(segment));
    if (base == 0 || segment != 0) throw std::runtime_error("No usable PCI segment-0 MCFG entry is available.");
    return base;
}

template<typename Fn>
Fn Export(HMODULE module, const char* name) {
    auto function = reinterpret_cast<Fn>(GetProcAddress(module, name));
    if (!function) throw std::runtime_error(std::string("The AMD driver ADLX runtime is missing ") + name + ".");
    return function;
}

template<typename Fn>
Fn Method(void* object, std::size_t slot) {
    if (!object) throw std::runtime_error("An ADLX tuning interface is unavailable.");
    auto vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<Fn>(vtable[slot]);
}

using Result = std::int32_t;
using GetGpusFn = Result(__stdcall*)(void*, void**);
using SizeFn = std::uint32_t(__stdcall*)(void*);
using AtFn = Result(__stdcall*)(void*, std::uint32_t, void**);
using NameFn = Result(__stdcall*)(void*, const char**);
using TuningFn = Result(__stdcall*)(void*, void**);
using ManualFn = Result(__stdcall*)(void*, void*, void**);
using QueryFn = Result(__stdcall*)(void*, const wchar_t*, void**);
using GetIntFn = Result(__stdcall*)(void*, std::int32_t*);
using SetIntFn = Result(__stdcall*)(void*, std::int32_t);
using GetBoolFn = Result(__stdcall*)(void*, std::uint8_t*);
using SetBoolFn = Result(__stdcall*)(void*, bool);
using ReleaseFn = Result(__stdcall*)(void*);
using GetRangeFn = Result(__stdcall*)(void*, struct IntRange*);
using GetFanRangesFn = Result(__stdcall*)(void*, struct IntRange*, struct IntRange*);
using SetFanListFn = Result(__stdcall*)(void*, void*);
using IsFanListValidFn = Result(__stdcall*)(void*, void*, std::int32_t*);
using IsFanSupportedFn = Result(__stdcall*)(void*, void*, std::uint8_t*);
struct IntRange { int minimum{}, maximum{}, step{}; };

void AdlxRequire(Result result, const wchar_t* operation) {
    if (result != 0) throw std::runtime_error("ADLX call failed with code " + std::to_string(result) + " (" + Narrow(operation) + ").");
}

void AdlxRelease(void* object) {
    if (object) Method<ReleaseFn>(object, 1)(object);
}

std::wstring Wide(const char* text) {
    if (!text || !*text) return L"Unknown AMD GPU";
    const int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (count <= 1) return L"Unknown AMD GPU";
    std::wstring value(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, value.data(), count);
    value.pop_back();
    return value;
}

class AdlxSession {
public:
    using QueryVersionFn = Result(__cdecl*)(std::uint64_t*);
    using InitializeFn = Result(__cdecl*)(std::uint64_t, void**);
    using TerminateFn = Result(__cdecl*)();

    AdlxSession() {
        module_ = LoadLibraryW(L"amdadlx64.dll");
        if (!module_) throw std::runtime_error("Could not load amdadlx64.dll from the AMD driver installation.");
        try {
            auto queryVersion = Export<QueryVersionFn>(module_, "ADLXQueryFullVersion");
            initialize_ = Export<InitializeFn>(module_, "ADLXInitialize");
            terminate_ = Export<TerminateFn>(module_, "ADLXTerminate");
            AdlxRequire(queryVersion(&version_), L"ADLX version query");
            AdlxRequire(initialize_(version_, &system_), L"ADLX initialization");
            initialized_ = true;
            OpenInterfaces();
        } catch (...) { Close(); throw; }
    }
    ~AdlxSession() { Close(); }
    AdlxSession(const AdlxSession&) = delete;
    AdlxSession& operator=(const AdlxSession&) = delete;

    AdlxSnapshot Read() const {
        return { gpuName_, VersionText(),
            TryValueWithRange(gfx_, 7, 6), TryValueWithRange(gfx_, 10, 9),
            TryValueWithRange(vram_, 8, 7), TryMemoryTiming(vram_), TryValueWithRange(power_, 4, 3), ReadFan() };
    }

    std::vector<std::wstring> Apply(const AdlxRequest& request) const {
        std::vector<std::wstring> results;
        SetValue(gfx_, 8, request.gfxMax, L"Driver GFX max", results);
        SetValue(gfx_, 11, request.voltage, L"Driver voltage", results);
        SetValue(vram_, 9, request.vramMax, L"Driver VRAM max", results);
        SetValue(vram_, 6, request.memoryTiming, L"Driver memory timing", results);
        SetValue(power_, 5, request.powerLimit, L"Driver power limit", results);
        if (results.empty()) results.emplace_back(L"No stock-driver controls are staged.");
        return results;
    }
    void ApplyFanRequest(const FanRequest& request) const { ApplyFan(request); }
    void ResetFanDefaults() const { ResetFanToDefault(); }

private:
    HMODULE module_{};
    InitializeFn initialize_{};
    TerminateFn terminate_{};
    bool initialized_{};
    std::uint64_t version_{};
    void* system_{};
    void *gpus_{}, *gpu_{}, *tuning_{}, *manualGfx_{}, *manualVram_{}, *manualFan_{}, *manualPower_{}, *gfx_{}, *vram_{}, *power_{};
    std::wstring gpuName_;

    void OpenInterfaces() {
        AdlxRequire(Method<GetGpusFn>(system_, 1)(system_, &gpus_), L"ADLX GPU enumeration");
        const auto count = Method<SizeFn>(gpus_, 3)(gpus_);
        for (std::uint32_t i = 0; i < count; ++i) {
            void* candidate{};
            AdlxRequire(Method<AtFn>(gpus_, 11)(gpus_, i, &candidate), L"ADLX GPU lookup");
            const char* name{};
            const Result nameResult = Method<NameFn>(candidate, 7)(candidate, &name);
            if (nameResult != 0) { AdlxRelease(candidate); AdlxRequire(nameResult, L"ADLX GPU name"); }
            const std::wstring candidateName = Wide(name);
            if (!gpu_ || candidateName.find(L"9070") != std::wstring::npos) {
                AdlxRelease(gpu_); gpu_ = candidate; gpuName_ = candidateName;
            } else AdlxRelease(candidate);
        }
        if (!gpu_) throw std::runtime_error("ADLX did not find an AMD GPU.");
        AdlxRequire(Method<TuningFn>(system_, 8)(system_, &tuning_), L"ADLX tuning service");
        AdlxRequire(Method<ManualFn>(tuning_, 14)(tuning_, gpu_, &manualGfx_), L"manual GFX tuning service");
        AdlxRequire(Method<ManualFn>(tuning_, 15)(tuning_, gpu_, &manualVram_), L"manual VRAM tuning service");
        AdlxRequire(Method<ManualFn>(tuning_, 17)(tuning_, gpu_, &manualPower_), L"manual power tuning service");
        AdlxRequire(Method<QueryFn>(manualGfx_, 2)(manualGfx_, L"IADLXManualGraphicsTuning2", &gfx_), L"GFX tuning interface");
        AdlxRequire(Method<QueryFn>(manualVram_, 2)(manualVram_, L"IADLXManualVRAMTuning2", &vram_), L"VRAM tuning interface");
        AdlxRequire(Method<QueryFn>(manualPower_, 2)(manualPower_, L"IADLXManualPowerTuning", &power_), L"power tuning interface");
        std::uint8_t fanSupported{};
        if (Method<IsFanSupportedFn>(tuning_, 10)(tuning_, gpu_, &fanSupported) == 0 && fanSupported)
            (void)Method<ManualFn>(tuning_, 16)(tuning_, gpu_, &manualFan_);
    }

    static std::vector<AdlxSnapshot::FanPoint> ReadFanStates(void* list) {
        std::vector<AdlxSnapshot::FanPoint> points;
        const auto count = Method<SizeFn>(list, 3)(list);
        points.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            void* state{};
            AdlxRequire(Method<AtFn>(list, 11)(list, i, &state), L"fan curve point");
            try {
                points.push_back({ ReadInt(state, 5), ReadInt(state, 3) });
            } catch (...) { AdlxRelease(state); throw; }
            AdlxRelease(state);
        }
        return points;
    }

    AdlxSnapshot::Fan ReadFan() const {
        // Slot indices are matched to AMD's IGPUTuning.h and IGPUManualFanTuning.h.
        AdlxSnapshot::Fan fan{};
        if (!manualFan_) return fan;
        fan.available = true;
        IntRange speed{}, temperature{};
        const bool rangesRead = Method<GetFanRangesFn>(manualFan_, 3)(manualFan_, &speed, &temperature) == 0;
        if (rangesRead) {
            fan.speedRange = { speed.minimum, speed.maximum, std::max(1, speed.step) };
            fan.temperatureRange = { temperature.minimum, temperature.maximum, std::max(1, temperature.step) };
        } else fan.available = false;
        void* states{};
        if (Method<GetGpusFn>(manualFan_, 4)(manualFan_, &states) == 0 && states) {
            try { fan.curve = ReadFanStates(states);if(fan.curve.size()<2)fan.available=false; }
            catch (...) { fan.available = false; }
            AdlxRelease(states);
        } else fan.available = false;
        std::uint8_t supported{};
        if (Method<GetBoolFn>(manualFan_, 8)(manualFan_, &supported) == 0 && supported) {
            fan.zeroRpmSupported = true;
            std::uint8_t value{};
            if (Method<GetBoolFn>(manualFan_, 9)(manualFan_, &value) == 0) fan.zeroRpm = value != 0;
        }
        supported = 0;
        if (Method<GetBoolFn>(manualFan_, 15)(manualFan_, &supported) == 0 && supported) {
            fan.minSpeedSupported = true;
            fan.minSpeed = TryValueWithRange(manualFan_, 17, 16);
        }
        void* extended{};
        if (Method<QueryFn>(manualFan_, 2)(manualFan_, L"IADLXManualFanTuning1", &extended) == 0 && extended) {
            // The extension appends default-curve, acoustic, minimum-speed,
            // target-speed, then Zero RPM accessors after the base interface.
            void* defaults{};
            if (Method<GetGpusFn>(extended, 23)(extended, &defaults) == 0 && defaults) {
                try { fan.defaultCurve = ReadFanStates(defaults); } catch (...) { fan.defaultCurve.clear(); }
                AdlxRelease(defaults);
            }
            std::uint8_t defaultZero{};
            if (Method<GetBoolFn>(extended, 27)(extended, &defaultZero) == 0) fan.zeroRpmDefault = defaultZero;
            std::int32_t defaultSpeed{};
            if (Method<GetIntFn>(extended, 25)(extended, &defaultSpeed) == 0 && fan.minSpeed.available)
                fan.defaultMinSpeed = { defaultSpeed, fan.minSpeed.range, true };
            AdlxRelease(extended);
        }
        return fan;
    }

    void ApplyFan(const FanRequest& request) const {
        if (!manualFan_) throw std::runtime_error("This GPU/driver does not expose manual fan tuning through ADLX.");
        if (request.curve) {
            IntRange speed{}, temperature{};
            AdlxRequire(Method<GetFanRangesFn>(manualFan_, 3)(manualFan_, &speed, &temperature), L"fan range read");
            ValidateFanCurve(*request.curve, { speed.minimum,speed.maximum,std::max(1,speed.step) },
                { temperature.minimum,temperature.maximum,std::max(1,temperature.step) });
            void* states{};
            AdlxRequire(Method<GetGpusFn>(manualFan_, 4)(manualFan_, &states), L"fan curve read");
            try {
                if (Method<SizeFn>(states, 3)(states) != request.curve->size())
                    throw std::runtime_error("The fan curve point count changed since readback. Refresh fan settings before applying.");
                const auto count = static_cast<std::uint32_t>(request.curve->size());
                for (std::uint32_t i = 0; i < count; ++i) {
                    void* state{};
                    AdlxRequire(Method<AtFn>(states, 11)(states, i, &state), L"fan curve point");
                    const auto& point = (*request.curve)[i];
                    const Result speedResult = Method<SetIntFn>(state, 4)(state, point.speed);
                    const Result tempResult = speedResult == 0 ? Method<SetIntFn>(state, 6)(state, point.temperature) : speedResult;
                    AdlxRelease(state);
                    AdlxRequire(tempResult, L"fan curve point edit");
                }
                std::int32_t errorIndex = -1;
                AdlxRequire(Method<IsFanListValidFn>(manualFan_, 6)(manualFan_, states, &errorIndex), L"fan curve validation");
                if (errorIndex >= 0) throw std::runtime_error("The AMD driver rejected fan curve point " + std::to_string(errorIndex + 1) + ".");
                AdlxRequire(Method<SetFanListFn>(manualFan_, 7)(manualFan_, states), L"fan curve apply");
            } catch (...) { AdlxRelease(states); throw; }
            AdlxRelease(states);
        }
        if (request.minSpeed) {
            std::uint8_t supported{};
            AdlxRequire(Method<GetBoolFn>(manualFan_, 15)(manualFan_, &supported), L"minimum fan speed support query");
            if (!supported) throw std::runtime_error("Minimum fan speed is not supported by this GPU/driver.");
            IntRange range{};
            AdlxRequire(Method<GetRangeFn>(manualFan_, 16)(manualFan_, &range), L"minimum fan speed range read");
            if (*request.minSpeed < range.minimum || *request.minSpeed > range.maximum ||
                (*request.minSpeed - range.minimum) % std::max(1,range.step) != 0)
                throw std::runtime_error("Minimum fan speed is outside the live ADLX range or step.");
            AdlxRequire(Method<SetIntFn>(manualFan_, 18)(manualFan_, *request.minSpeed), L"minimum fan speed apply");
        }
        if (request.zeroRpm) {
            std::uint8_t supported{};
            AdlxRequire(Method<GetBoolFn>(manualFan_, 8)(manualFan_, &supported), L"Zero RPM support query");
            if (!supported) throw std::runtime_error("Zero RPM control is not supported by this GPU/driver.");
            AdlxRequire(Method<SetBoolFn>(manualFan_, 10)(manualFan_, *request.zeroRpm), L"Zero RPM apply");
        }
    }

    void ResetFanToDefault() const {
        if (!manualFan_) throw std::runtime_error("This GPU/driver does not expose manual fan tuning through ADLX.");
        void* extended{};
        AdlxRequire(Method<QueryFn>(manualFan_, 2)(manualFan_, L"IADLXManualFanTuning1", &extended), L"fan defaults interface");
        try {
            void* defaults{};
            AdlxRequire(Method<GetGpusFn>(extended, 23)(extended, &defaults), L"default fan curve read");
            try { AdlxRequire(Method<SetFanListFn>(manualFan_, 7)(manualFan_, defaults), L"default fan curve apply"); }
            catch (...) { AdlxRelease(defaults); throw; }
            AdlxRelease(defaults);
            std::uint8_t supported{};
            if (Method<GetBoolFn>(manualFan_, 15)(manualFan_, &supported) == 0 && supported) {
                std::int32_t defaultSpeed{};
                AdlxRequire(Method<GetIntFn>(extended, 25)(extended, &defaultSpeed), L"default fan speed read");
                AdlxRequire(Method<SetIntFn>(manualFan_, 18)(manualFan_, defaultSpeed), L"default fan speed apply");
            }
            std::uint8_t zeroSupported{};
            if (Method<GetBoolFn>(manualFan_, 8)(manualFan_, &zeroSupported) == 0 && zeroSupported) {
                std::uint8_t defaultZero{};
                AdlxRequire(Method<GetBoolFn>(extended, 27)(extended, &defaultZero), L"default Zero RPM read");
                AdlxRequire(Method<SetBoolFn>(manualFan_, 10)(manualFan_, defaultZero), L"default Zero RPM apply");
            }
        } catch (...) { AdlxRelease(extended); throw; }
        AdlxRelease(extended);
    }

    static int ReadInt(void* object, std::size_t method) {
        int value{};
        AdlxRequire(Method<GetIntFn>(object, method)(object, &value), L"ADLX read");
        return value;
    }
    static bool ReadBool(void* object, std::size_t method) {
        std::uint8_t value{};
        return Method<GetBoolFn>(object, method)(object, &value) == 0 && value != 0;
    }
    static TuningValue ValueWithRange(void* object, std::size_t getMethod, std::size_t rangeMethod) {
        const int value = ReadInt(object, getMethod);
        IntRange raw{};
        const Result result = Method<GetRangeFn>(object, rangeMethod)(object, &raw);
        return { value,{ raw.minimum,raw.maximum,std::max(1,raw.step) },result == 0 && raw.minimum <= raw.maximum };
    }
    static TuningValue TryValueWithRange(void* object, std::size_t getMethod, std::size_t rangeMethod) {
        if (!object) return {};
        try { return ValueWithRange(object, getMethod, rangeMethod); }
        catch (...) { return {}; }
    }
    static TuningValue TryMemoryTiming(void* object) {
        void* list{};
        try {
            if (!ReadBool(object, 3)) return {};
            const int current = ReadInt(object, 5);
            AdlxRequire(Method<GetGpusFn>(object, 4)(object, &list), L"memory timing capabilities");
            bool hasDefault = false, hasFast = false;
            const auto count = Method<SizeFn>(list, 3)(list);
            for (std::uint32_t i = 0; i < count; ++i) {
                void* item{};
                AdlxRequire(Method<AtFn>(list, 11)(list, i, &item), L"memory timing capability");
                try {
                    const int mode = ReadInt(item, 3);
                    hasDefault |= mode == 0; hasFast |= mode == 1;
                } catch (...) { AdlxRelease(item); throw; }
                AdlxRelease(item);
            }
            AdlxRelease(list); list = nullptr;
            return { current,{ 0,1,1 },hasDefault && hasFast && (current == 0 || current == 1) };
        } catch (...) { AdlxRelease(list); return {}; }
    }
    static void SetValue(void* object, std::size_t method, const std::optional<int>& value,
        const wchar_t* label, std::vector<std::wstring>& results) {
        if (!value) return;
        if (!object) throw std::runtime_error("The ADLX control interface is unavailable.");
        const wchar_t* name = label;
        AdlxRequire(Method<SetIntFn>(object, method)(object, *value), name);
        results.emplace_back(std::wstring(label) + L" " + Number(static_cast<unsigned>(*value)) + L" [ADLX OK]");
    }
    std::wstring VersionText() const {
        return Number((version_ >> 48) & 0xffff) + L"." + Number((version_ >> 32) & 0xffff) + L"." +
            Number((version_ >> 16) & 0xffff) + L"." + Number(version_ & 0xffff);
    }
    void Close() noexcept {
        try { AdlxRelease(power_); } catch (...) {} power_ = nullptr;
        try { AdlxRelease(manualFan_); } catch (...) {} manualFan_ = nullptr;
        try { AdlxRelease(vram_); } catch (...) {} vram_ = nullptr;
        try { AdlxRelease(gfx_); } catch (...) {} gfx_ = nullptr;
        try { AdlxRelease(manualPower_); } catch (...) {} manualPower_ = nullptr;
        try { AdlxRelease(manualVram_); } catch (...) {} manualVram_ = nullptr;
        try { AdlxRelease(manualGfx_); } catch (...) {} manualGfx_ = nullptr;
        try { AdlxRelease(tuning_); } catch (...) {} tuning_ = nullptr;
        try { AdlxRelease(gpu_); } catch (...) {} gpu_ = nullptr;
        try { AdlxRelease(gpus_); } catch (...) {} gpus_ = nullptr;
        if (initialized_ && terminate_) { terminate_(); initialized_ = false; }
        if (module_) { FreeLibrary(module_); module_ = nullptr; }
    }
};

class SmuTransport {
public:
    using IsOpenFn = BOOL(__stdcall*)();
    using ReadPhysicalFn = BOOL(__stdcall*)(void*, std::uint32_t*);
    using WritePhysicalFn = BOOL(__stdcall*)(void*, std::uint32_t);

    static SmuTransport Connect() {
        SmuTransport transport;
        const auto helper = ExtractInpOut();
        transport.library_ = LoadLibraryW(helper.c_str());
        if (!transport.library_) throw std::runtime_error("Could not load the embedded InpOut helper (Windows error " + std::to_string(GetLastError()) + ").");
        const auto open = Export<IsOpenFn>(transport.library_, "IsInpOutDriverOpen");
        transport.read_ = Export<ReadPhysicalFn>(transport.library_, "GetPhysLong");
        transport.write_ = Export<WritePhysicalFn>(transport.library_, "SetPhysLong");
        if (!open()) throw std::runtime_error("The direct-access helper did not start. Run as administrator and check whether Windows Core Isolation blocked its driver.");

        const auto location = FindAmdLocation();
        transport.config_ = FindEcamBase() + (static_cast<std::uint64_t>(location.bus) << 20) +
            (static_cast<std::uint64_t>(location.device) << 15) + (static_cast<std::uint64_t>(location.function) << 12);
        const auto vendor = transport.ReadPhysical(transport.config_) & 0xffff;
        if (vendor != 0x1002) throw std::runtime_error("The selected PCI location is not an AMD GPU (vendor 0x" + HexNarrow(vendor, 4) + ").");
        transport.mmio_ = transport.FindMmioBar();
        if (!transport.mmio_) throw std::runtime_error("No non-prefetchable AMD GPU MMIO BAR was found.");
        const bool pcie = transport.ProbePcieSmn(0xE0, 0xE4);
        transport.smnIndex_ = pcie ? 0xE0 : 0x38;
        transport.smnData_ = pcie ? 0xE4 : 0x3C;
        transport.backend_ = (pcie ? L"PCIe SMN" : L"MMIO SMN") + std::wstring(L" · PCI ") +
            Number(location.bus) + L":" + Number(location.device) + L"." + Number(location.function);
        return transport;
    }
    SmuTransport() = default;
    SmuTransport(const SmuTransport&) = delete;
    SmuTransport& operator=(const SmuTransport&) = delete;
    SmuTransport(SmuTransport&& other) noexcept { *this = std::move(other); }
    SmuTransport& operator=(SmuTransport&& other) noexcept {
        if (this != &other) {
            if (library_) FreeLibrary(library_);
            library_ = std::exchange(other.library_, nullptr); read_ = other.read_; write_ = other.write_;
            config_ = other.config_; mmio_ = other.mmio_; smnIndex_ = other.smnIndex_; smnData_ = other.smnData_; backend_ = std::move(other.backend_);
        }
        return *this;
    }
    ~SmuTransport() { if (library_) FreeLibrary(library_); }

    SmuSnapshot Readback() const {
        const std::uint32_t version = Send(kGetSmuVersion, 0);
        std::array<unsigned, 4> parts{ (version >> 24) & 0xff,(version >> 16) & 0xff,(version >> 8) & 0xff,version & 0xff };
        const auto ver = std::to_wstring(parts[0]) + L"." + std::to_wstring(parts[1]) + L"." + std::to_wstring(parts[2]) + L"." + std::to_wstring(parts[3]);
        return { L"SMU " + ver + L" · " + backend_,ReadLimits(ClockDomain::Gfx),ReadLimits(ClockDomain::Fclk),ReadLimits(ClockDomain::Soc) };
    }

    std::vector<std::wstring> Apply(const std::vector<ClockRequest>& requests) const {
        for (const auto& request : requests) {
            if (request.domain != ClockDomain::Gfx && request.domain != ClockDomain::Fclk && request.domain != ClockDomain::Soc)
            throw std::runtime_error("This GUI does not control the " + Narrow(DomainName(request.domain)) + " domain.");
        }
        const auto snapshot = Readback();
        std::vector<std::wstring> result;
        for (const auto& request : requests) {
            if (!request.minimum && !request.maximum && !request.releaseMaximum) continue;
            ClockLimits limits{};
            if (request.domain == ClockDomain::Gfx) limits = snapshot.gfx;
            else if (request.domain == ClockDomain::Fclk) limits = snapshot.fclk;
            else limits = snapshot.soc;
            ValidateClockRequest(request, limits);
            if (request.releaseMaximum) {
                Send(kSetSoftMaxByFreq, Pack(request.domain, 0xffff));
                result.push_back(DomainName(request.domain) + L" soft maximum automatic [SMU OK]");
            } else if (request.maximum) {
                const int wireMaximum = *request.maximum + (request.domain == ClockDomain::Fclk ? 1 : 0);
                Send(kSetSoftMaxByFreq, Pack(request.domain, wireMaximum));
                result.push_back(DomainName(request.domain) + L" max " + Number(*request.maximum) + L" MHz [SMU OK]");
            }
            if (request.minimum) {
                Send(kSetSoftMinByFreq, Pack(request.domain, *request.minimum));
                result.push_back(DomainName(request.domain) + L" min " + Number(*request.minimum) + L" MHz [SMU OK]");
            }
        }
        if (result.empty()) result.emplace_back(L"No enabled clock ranges to apply.");
        return result;
    }

    std::vector<std::wstring> RestoreAll() const {
        std::vector<std::wstring> result;
        for (auto domain : { ClockDomain::Gfx,ClockDomain::Fclk,ClockDomain::Soc }) {
            Send(kSetSoftMaxByFreq, Pack(domain, 0xffff));
            Send(kSetSoftMinByFreq, Pack(domain, 0));
            result.push_back(DomainName(domain) + L" automatic [SMU OK]");
        }
        return result;
    }

private:
    HMODULE library_{};
    ReadPhysicalFn read_{};
    WritePhysicalFn write_{};
    std::uint64_t config_{}, mmio_{};
    std::uint32_t smnIndex_{}, smnData_{};
    std::wstring backend_;

    static std::filesystem::path ExtractInpOut() {
        HMODULE exe = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(exe, MAKEINTRESOURCEW(kInpOutResource), RT_RCDATA);
        if (!resource) throw std::runtime_error("The embedded direct-access helper is missing from this build.");
        const DWORD size = SizeofResource(exe, resource);
        if (size == 0) throw std::runtime_error("The embedded direct-access helper is empty.");
        HGLOBAL loaded = LoadResource(exe, resource);
        if (!loaded) throw std::runtime_error("Could not load the embedded direct-access helper resource.");
        const void* data = LockResource(loaded);
        if (!data) throw std::runtime_error("Could not lock the embedded direct-access helper resource.");
        wchar_t temp[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, temp);
        if (length == 0 || length >= MAX_PATH) throw std::runtime_error("Could not locate the Windows temporary directory.");
        const auto directory = std::filesystem::path(temp) / L"RDNA4-Clock-Control";
        std::filesystem::create_directories(directory);
        const auto path = directory / L"inpoutx64.dll";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Could not extract the embedded direct-access helper.");
        file.write(static_cast<const char*>(data), size);
        if (!file) throw std::runtime_error("Could not write the extracted direct-access helper.");
        return path;
    }
    std::uint32_t ReadPhysical(std::uint64_t address) const {
        std::uint32_t value{};
        if (!read_(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), &value))
            throw std::runtime_error("Physical-memory read failed at 0x" + HexNarrow(address) + ".");
        return value;
    }
    void WritePhysical(std::uint64_t address, std::uint32_t value) const {
        if (!write_(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), value))
            throw std::runtime_error("Physical-memory write failed at 0x" + HexNarrow(address) + ".");
    }
    std::uint32_t SmnRead(std::uint32_t address) const {
        const auto base = smnIndex_ == 0xE0 ? config_ : mmio_;
        WritePhysical(base + smnIndex_, address);
        return ReadPhysical(base + smnData_);
    }
    void SmnWrite(std::uint32_t address, std::uint32_t value) const {
        const auto base = smnIndex_ == 0xE0 ? config_ : mmio_;
        WritePhysical(base + smnIndex_, address);
        WritePhysical(base + smnData_, value);
    }
    std::uint32_t Send(std::uint32_t message, std::uint32_t parameter) const {
        std::scoped_lock lock(g_mailboxMutex);
        SmnWrite(kResponseRegister, 0);
        SmnWrite(kParameterRegister, parameter);
        SmnWrite(kMessageRegister, message);
        const auto start = std::chrono::steady_clock::now();
        std::uint32_t response{};
        do {
            response = SmnRead(kResponseRegister);
            if (response != 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
        if (response == 0) throw std::runtime_error("SMU mailbox timed out for command 0x" + HexNarrow(message) + ".");
        if (response != kResponseOk) throw std::runtime_error("SMU rejected command 0x" + HexNarrow(message) + " (response 0x" + HexNarrow(response) + ").");
        return SmnRead(kParameterRegister);
    }
    ClockLimits ReadLimits(ClockDomain domain) const {
        const auto minimum = static_cast<int>(Send(kGetMinDpmFreq, static_cast<std::uint32_t>(domain) << 16));
        const auto maximum = static_cast<int>(Send(kGetMaxDpmFreq, static_cast<std::uint32_t>(domain) << 16));
        if (maximum <= 0 || minimum < 0 || minimum > maximum)
            throw std::runtime_error(Narrow(DomainName(domain) + L" returned an invalid DPM range " + Number(minimum) + L"–" + Number(maximum) + L" MHz."));
        return { minimum,maximum };
    }
    static std::uint32_t Pack(ClockDomain domain, int frequency) {
        return (static_cast<std::uint32_t>(domain) << 16) | (static_cast<std::uint32_t>(frequency) & 0xffff);
    }
    bool ProbePcieSmn(std::uint32_t index, std::uint32_t data) const {
        WritePhysical(config_ + index, 0);
        const auto zero = ReadPhysical(config_ + data);
        WritePhysical(config_ + index, 4);
        const auto four = ReadPhysical(config_ + data);
        return zero != 0 && zero != UINT32_MAX && four != zero && four != UINT32_MAX;
    }
    std::uint64_t FindMmioBar() const {
        std::vector<std::uint64_t> candidates;
        for (int offset = 0x10; offset <= 0x24;) {
            const auto low = ReadPhysical(config_ + offset);
            if (low & 1) { offset += 4; continue; }
            const bool is64 = ((low >> 1) & 3) == 2;
            const bool prefetch = (low & 8) != 0;
            std::uint64_t address = low & 0xfffffff0u;
            if (is64 && offset + 4 <= 0x24) {
                address |= static_cast<std::uint64_t>(ReadPhysical(config_ + offset + 4)) << 32;
                offset += 8;
            } else offset += 4;
            if (address && !prefetch) candidates.push_back(address);
        }
        return candidates.empty() ? 0 : candidates.back();
    }
};
} // namespace

std::wstring DomainName(ClockDomain domain) {
    switch (domain) {
    case ClockDomain::Gfx: return L"GFXCLK";
    case ClockDomain::Uclk: return L"UCLK";
    case ClockDomain::Fclk: return L"FCLK";
    case ClockDomain::Soc: return L"SOCCLK";
    default: return L"Unknown clock";
    }
}

void ValidateClockRequest(const ClockRequest& request, ClockLimits limits) {
    if (request.releaseMaximum && (request.domain != ClockDomain::Gfx || request.maximum))
        throw std::runtime_error("Only the linked GFX extension may release its soft maximum; it must not include a numeric maximum.");
    for (const auto value : { request.minimum,request.maximum }) {
        if (value && (*value < limits.minimum || *value > limits.maximum))
            throw std::runtime_error(Narrow(DomainName(request.domain) + L" " + Number(*value) + L" MHz is outside the live DPM range " +
                Number(limits.minimum) + L"–" + Number(limits.maximum) + L" MHz."));
    }
    if (request.minimum && request.maximum && *request.minimum > *request.maximum)
        throw std::runtime_error(Narrow(DomainName(request.domain) + L" minimum cannot exceed its maximum."));
}

ClockLimits ReadSmuLimits(ClockDomain domain) {
    auto transport = SmuTransport::Connect();
    const auto state = transport.Readback();
    if (domain == ClockDomain::Gfx) return state.gfx;
    if (domain == ClockDomain::Fclk) return state.fclk;
    if (domain == ClockDomain::Soc) return state.soc;
    throw std::runtime_error("This GUI does not control that clock domain.");
}
SmuSnapshot ReadSmu() { return SmuTransport::Connect().Readback(); }
std::vector<std::wstring> ApplySmu(const std::vector<ClockRequest>& requests) { return SmuTransport::Connect().Apply(requests); }
std::vector<std::wstring> RestoreSmuDefaults() { return SmuTransport::Connect().RestoreAll(); }
AdlxSnapshot ReadAdlx() { return AdlxSession().Read(); }
std::vector<std::wstring> ApplyAdlx(const AdlxRequest& request) { return AdlxSession().Apply(request); }
std::vector<std::wstring> ApplyFan(const FanRequest& request) {
    AdlxSession().ApplyFanRequest(request);
    std::vector<std::wstring> result;
    if (request.curve) result.emplace_back(L"Fan curve applied [ADLX OK]");
    if (request.minSpeed) result.emplace_back(L"Minimum fan speed applied [ADLX OK]");
    if (request.zeroRpm) result.emplace_back(std::wstring(L"Zero RPM ") + (*request.zeroRpm ? L"enabled" : L"disabled") + L" [ADLX OK]");
    if (result.empty()) result.emplace_back(L"No fan controls are staged.");
    return result;
}
std::vector<std::wstring> ResetFanToDefault() {
    AdlxSession().ResetFanDefaults();
    return { L"Fan settings restored to AMD driver defaults [ADLX OK]" };
}

void ValidateFanCurve(const std::vector<AdlxSnapshot::FanPoint>& curve, Range speed, Range temperature) {
    if (curve.size() < 2 || curve.size() > 10) throw std::runtime_error("A fan curve must contain 2 to 10 points.");
    const int speedStep = std::max(1, speed.step), tempStep = std::max(1, temperature.step);
    for (std::size_t i = 0; i < curve.size(); ++i) {
        const auto& point = curve[i];
        if (point.speed < speed.minimum || point.speed > speed.maximum ||
            (point.speed - speed.minimum) % speedStep != 0)
            throw std::runtime_error("Fan curve speed is outside the current ADLX range or step.");
        if (point.temperature < temperature.minimum || point.temperature > temperature.maximum ||
            (point.temperature - temperature.minimum) % tempStep != 0)
            throw std::runtime_error("Fan curve temperature is outside the current ADLX range or step.");
        if (i && point.temperature - curve[i - 1].temperature < tempStep)
            throw std::runtime_error("Fan curve temperatures must increase in point order.");
        if (i && point.speed < curve[i - 1].speed)
            throw std::runtime_error("Fan curve speed must not decrease as temperature rises.");
    }
}

ClockLimits RecoverGfxBaseLimits(ClockLimits current, int appliedOffset) {
    const auto maximum = static_cast<long long>(current.maximum) - appliedOffset;
    if (current.minimum > current.maximum || maximum < 0 || maximum > std::numeric_limits<int>::max())
        throw std::runtime_error("The live GFX DPM range is inconsistent with the applied ADLX offset.");
    const int baseMaximum = static_cast<int>(maximum);
    // A user-set soft minimum can be above the offset-adjusted base ceiling.
    // Keep the normalized interval valid so startup readback can still render.
    return { std::min(current.minimum,baseMaximum),baseMaximum };
}

GfxLinkedPlan PlanLinkedGfx(int target, int currentOffset, ClockLimits native, Range offsetRange) {
    const int step = std::max(1, offsetRange.step);
    if (offsetRange.minimum > 0 || offsetRange.maximum < 0 || (-offsetRange.minimum) % step != 0)
        throw std::runtime_error("The ADLX range must support a neutral offset.");
    if (native.minimum > native.maximum || target < native.minimum)
        throw std::runtime_error("GFX maximum is outside the linked range or ADLX offset step.");
    if (target <= native.maximum)
        return { target,target,false,currentOffset };

    const int maxOffset = offsetRange.minimum + ((offsetRange.maximum - offsetRange.minimum) / step) * step;
    if (maxOffset <= 0)
        throw std::runtime_error("The ADLX range has no positive step-aligned GFX extension.");
    const int requestedOffset = target - native.maximum;
    if (requestedOffset > offsetRange.maximum)
        throw std::runtime_error("GFX maximum exceeds the ADLX offset range.");
    const int roundedSteps = (requestedOffset + step / 2) / step;
    const int snappedOffset = std::min(maxOffset, roundedSteps * step);
    if (snappedOffset <= 0 || currentOffset > offsetRange.maximum || currentOffset < offsetRange.minimum)
        throw std::runtime_error("GFX maximum is outside the linked range or ADLX offset step.");
    const int effectiveTarget = native.maximum + snappedOffset;
    return { effectiveTarget,std::nullopt,true,std::max(currentOffset,snappedOffset) };
}

} // namespace rdna
