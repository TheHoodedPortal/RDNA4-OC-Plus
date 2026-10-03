// Native C++ port of RDNA4 OC+: approved Adrenalin charcoal, white and restrained red.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objbase.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <commctrl.h>
#include "hardware.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cwchar>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {
constexpr wchar_t kClassName[] = L"RDNA4OCPlus.NativeWindow";
constexpr wchar_t kWindowTitle[] = L"RDNA4 OC+";
constexpr D2D1_COLOR_F kCanvas{ 16.0F / 255, 16.0F / 255, 16.0F / 255, 1 };
constexpr D2D1_COLOR_F kHeader{ 38.0F / 255, 38.0F / 255, 38.0F / 255, 1 };
constexpr D2D1_COLOR_F kSurface{ 38.0F / 255, 38.0F / 255, 38.0F / 255, 1 };
constexpr D2D1_COLOR_F kInset{ 31.0F / 255, 31.0F / 255, 31.0F / 255, 1 };
constexpr D2D1_COLOR_F kRule{ 25.0F / 255, 25.0F / 255, 25.0F / 255, 1 };
constexpr D2D1_COLOR_F kTrack{ 68.0F / 255, 68.0F / 255, 68.0F / 255, 1 };
constexpr D2D1_COLOR_F kInk{ 245.0F / 255, 245.0F / 255, 245.0F / 255, 1 };
constexpr D2D1_COLOR_F kMuted{ 172.0F / 255, 172.0F / 255, 172.0F / 255, 1 };
constexpr D2D1_COLOR_F kDisabled{ 154.0F / 255, 154.0F / 255, 154.0F / 255, 1 };
constexpr D2D1_COLOR_F kAccent{ 230.0F / 255, 0, 58.0F / 255, 1 };
constexpr D2D1_COLOR_F kChromeHover{ 58.0F / 255, 58.0F / 255, 58.0F / 255, 1 };
constexpr D2D1_COLOR_F kFieldHover{ 92.0F / 255, 92.0F / 255, 92.0F / 255, 1 };

struct Graphics {
    ID2D1Factory* factory{};
    IDWriteFactory* writeFactory{};
    ID2D1HwndRenderTarget* target{};
    ID2D1SolidColorBrush* brush{};
    IDWriteTextFormat* body{};
    IDWriteTextFormat* smallText{};
    IDWriteTextFormat* bold{};
    IDWriteTextFormat* cardTitle{};
    IDWriteTextFormat* brand{};
    IDWriteTextFormat* mono{};
    float scale{ 1.0F };
    bool trackingMouse{};

    ~Graphics() { DropTarget(); Release(body); Release(smallText); Release(bold); Release(cardTitle); Release(brand); Release(mono); Release(writeFactory); Release(factory); }
    template<class T> static void Release(T*& value) { if (value) { value->Release(); value = nullptr; } }

    HRESULT Initialize(HWND hwnd) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
        if (FAILED(hr)) return hr;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory));
        if (FAILED(hr)) return hr;
        scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0F;
        return CreateFormats();
    }

    HRESULT CreateFormats() {
        Release(body); Release(smallText); Release(bold); Release(cardTitle); Release(brand); Release(mono);
        HRESULT hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.67F * scale, L"en-us", &body);
        if (FAILED(hr)) return hr;
        hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.33F * scale, L"en-us", &smallText);
        if (FAILED(hr)) return hr;
        hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.67F * scale, L"en-us", &bold);
        if (FAILED(hr)) return hr;
        hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.33F * scale, L"en-us", &cardTitle);
        if (FAILED(hr)) return hr;
        hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.67F * scale, L"en-us", &brand);
        if (FAILED(hr)) return hr;
        return writeFactory->CreateTextFormat(L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 10.67F * scale, L"en-us", &mono);
    }

    void DropTarget() { Release(brush); Release(target); }
    HRESULT EnsureTarget(HWND hwnd) {
        if (target) return S_OK;
        RECT rc{}; GetClientRect(hwnd, &rc);
        const auto size = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rc.right)), static_cast<UINT32>(std::max<LONG>(1, rc.bottom)));
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(),96.0F,96.0F);
        HRESULT hr = factory->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(hwnd, size), &target);
        if (SUCCEEDED(hr)) hr = target->CreateSolidColorBrush(kInk, &brush);
        if (FAILED(hr)) DropTarget();
        return hr;
    }
    float P(float dip) const { return dip * scale; }
    void Fill(D2D1_RECT_F r, D2D1_COLOR_F c) { brush->SetColor(c); target->FillRectangle(r, brush); }
    void FillRound(D2D1_ROUNDED_RECT r, D2D1_COLOR_F c) { brush->SetColor(c); target->FillRoundedRectangle(r, brush); }
    void FillEllipse(D2D1_ELLIPSE e, D2D1_COLOR_F c) { brush->SetColor(c); target->FillEllipse(e, brush); }
    void Stroke(D2D1_RECT_F r, D2D1_COLOR_F c, float width = 1.0F) { brush->SetColor(c); target->DrawRectangle(r, brush, P(width)); }
    void StrokeRound(D2D1_ROUNDED_RECT r, D2D1_COLOR_F c, float width = 1.0F) { brush->SetColor(c); target->DrawRoundedRectangle(r, brush, P(width)); }
    void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float width = 1.0F) {
        brush->SetColor(c); target->DrawLine(D2D1::Point2F(P(x1), P(y1)), D2D1::Point2F(P(x2), P(y2)), brush, P(width));
    }
    void Text(const std::wstring& value, float x, float y, float w, float h, D2D1_COLOR_F color,
        IDWriteTextFormat* format = nullptr, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        brush->SetColor(color);
        IDWriteTextFormat* use = format ? format : body;
        use->SetTextAlignment(align); use->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        target->DrawText(value.c_str(), static_cast<UINT32>(value.size()), use,
            D2D1::RectF(P(x), P(y), P(x + w), P(y + h)), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void TextTop(const std::wstring& value, float x, float y, float w, float h, D2D1_COLOR_F color,
        IDWriteTextFormat* format = nullptr) {
        brush->SetColor(color);
        IDWriteTextFormat* use = format ? format : body;
        use->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); use->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        target->DrawText(value.c_str(), static_cast<UINT32>(value.size()), use,
            D2D1::RectF(P(x), P(y), P(x + w), P(y + h)), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
};

struct SliderState {
    std::wstring id, title, description, unit;
    int minimum{}, maximum{}, value{}, baseline{}, automaticValue{}, step{ 1 };
    bool available{}, direct{}, automatic{ true }, baselineAutomatic{ true }, invalid{};
    int scaleMinimum{}, scaleMaximum{};
    bool Staged() const { return available && (direct ? !automatic && (baselineAutomatic || value != baseline) : value != baseline); }
    std::optional<int> Request() const { return direct && automatic ? std::nullopt : std::optional<int>(value); }
};
using FanPoint = rdna::AdlxSnapshot::FanPoint;
enum class UiPage { Tuning, Fan };

struct ClockGroup {
    rdna::ClockDomain domain{};
    std::wstring title, description;
    SliderState minimum, maximum;
    rdna::ClockLimits dpm{};
};

struct HitRegion { D2D1_RECT_F rect{}; SliderState* slider{}; bool field{}; };
struct ButtonRegion { D2D1_RECT_F rect{}; int action{}; };
struct FanPointHit { D2D1_RECT_F rect{}; std::size_t index{}; };
enum class ResultKind { Smu, Adlx, Apply, Reset, FanReset };
struct BackendResult {
    ResultKind kind{};
    std::thread::id workerId{};
    std::optional<rdna::SmuSnapshot> smu;
    std::optional<rdna::AdlxSnapshot> adlx;
    std::vector<std::wstring> diagnostics;
    std::wstring error;
    bool smuApplied{}, adlxApplied{};
};
struct EditBinding { struct App* app{}; int index{}; WNDPROC original{}; };

struct App {
    Graphics g;
    HWND hwnd{};
    HFONT editFont{};
    HBRUSH editBrush{};
    std::array<ClockGroup, 3> clocks;
    std::array<SliderState, 4> driver;
    bool timingAvailable{}, timingValue{}, timingBaseline{}, timingStaged{};
    bool maximized{}, closeHover{}, maxHover{}, minHover{}, detailsHover{};
    int hoverAction{};
    int chromePressed{};
    SliderState* hoveredSlider{};
    bool hoveredField{};
    bool trackingMouse{}, busy{}, readCompleted{}, smuRead{}, adlxRead{}, smuDone{}, adlxDone{}, detailsOpen{};
    bool gfxAvailable{}, gfxLinked{}, gfxDraftLinked{};
    rdna::ClockLimits gfxNative{};
    int gfxAppliedOffset{};
    std::optional<rdna::Range> gfxOffsetRange;
    std::wstring gpuName{ L"GPU not read" }, status{ L"Reading hardware on startup. No settings are applied automatically." };
    D2D1_COLOR_F statusColor{ 172.0F / 255,172.0F / 255,172.0F / 255,1 };
    std::vector<std::wstring> diagnostics;
    std::vector<HitRegion> sliderHits;
    std::vector<ButtonRegion> buttonHits;
    SliderState* dragging{};
    SliderState* editing{};
    std::array<HWND, 11> editControls{};
    std::array<EditBinding, 11> editBindings{};
    rdna::AdlxSnapshot::Fan fan{};
    SliderState minFanSpeed{L"FanMinSpeed",L"Minimum fan speed",L"Lowest driver-set speed.",L"RPM"};
    std::vector<FanPoint> fanCurve, fanCurveBaseline;
    std::vector<FanPointHit> fanPointHits;
    D2D1_RECT_F fanPlot{};
    UiPage page{UiPage::Tuning};
    int draggingFanPoint{-1};
    bool zeroRpm{}, zeroRpmBaseline{};
    std::mutex resultMutex;
    std::deque<BackendResult> results;
    bool alive{ true };
    std::vector<std::thread> workers;

    App() {
        const std::array<std::tuple<rdna::ClockDomain,const wchar_t*,const wchar_t*>,3> defs{{
            {rdna::ClockDomain::Gfx,L"GFXCLK",L"GPU core"},
            {rdna::ClockDomain::Fclk,L"FCLK",L"Fabric"},
            {rdna::ClockDomain::Soc,L"SOCCLK",L"SoC"}}};
        for (std::size_t i=0;i<clocks.size();++i) {
            clocks[i].domain=std::get<0>(defs[i]); clocks[i].title=std::get<1>(defs[i]); clocks[i].description=std::get<2>(defs[i]);
            clocks[i].minimum={clocks[i].title+L"Min",L"Min",L"",L"MHz",0,0,0,0,0,1,false,true};
            clocks[i].maximum={clocks[i].title+L"Max",L"Max",L"",L"MHz",0,0,0,0,0,1,false,true};
        }
        driver={{{L"GfxMax",L"GPU maximum",L"Offset from the max default frequency.",L"MHz"},
            {L"Voltage",L"GPU voltage",L"Driver voltage target.",L"mV"},
            {L"VramMax",L"Memory maximum",L"VRAM clock limit under load.",L"MHz"},
            {L"Power",L"Power limit",L"Offset from stock board power.",L"%"}}};
        minFanSpeed.direct=false;
    }
    ~App() {
        { std::scoped_lock lock(resultMutex); alive=false; }
        for (auto& worker:workers) if(worker.joinable()) worker.join();
        if(editFont) DeleteObject(editFont);if(editBrush)DeleteObject(editBrush);
    }
};

constexpr D2D1_COLOR_F kAccentLight{ 255.0F / 255,112.0F / 255,146.0F / 255,1 };
constexpr D2D1_COLOR_F kError{ 255.0F / 255,155.0F / 255,155.0F / 255,1 };
constexpr UINT kResultMessage = WM_APP + 41;
constexpr wchar_t kEditClass[] = L"RDNA4OCPlus.ValueEdit";
constexpr int kEditBase = 1000;

D2D1_RECT_F Rect(float x,float y,float w,float h) { return D2D1::RectF(x,y,x+w,y+h); }
bool Contains(D2D1_RECT_F r,float x,float y) { return x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom; }
std::wstring GroupNumber(int value) {
    const bool negative=value<0; std::wstring digits=std::to_wstring(negative?-static_cast<long long>(value):value);
    for (int i=static_cast<int>(digits.size())-3;i>0;i-=3) digits.insert(static_cast<std::size_t>(i),L",");
    return negative?L"-"+digits:digits;
}
std::wstring ValueText(const SliderState& slider) {
    if (!slider.available) return L"—";
    if (slider.unit==L"%" && slider.value>0) return L"+"+std::to_wstring(slider.value);
    return std::to_wstring(slider.value);
}
void SetStatus(App& app,std::wstring value,D2D1_COLOR_F color=kMuted) { app.status=std::move(value); app.statusColor=color; }
std::size_t PendingCount(const App& app) {
    std::size_t count=app.timingStaged?1:0;
    for(const auto& group:app.clocks) count+=group.minimum.Staged()+group.maximum.Staged();
    for(const auto& slider:app.driver) count+=slider.Staged();
    count+=app.minFanSpeed.Staged()+(app.fanCurve!=app.fanCurveBaseline)+(app.zeroRpm!=app.zeroRpmBaseline);
    return count;
}
std::wstring Join(const std::vector<std::wstring>& lines) {
    std::wstring result;
    for(const auto& line:lines) { if(!result.empty()) result+=L"\r\n"; result+=line; }
    return result;
}

std::wstring Utf8(const std::string& value) {
    if (value.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0)return L"Hardware operation failed.";
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
std::wstring ErrorText(const std::exception& ex) { return Utf8(ex.what()); }
std::array<SliderState*,11> AllSliders(App& app) {
    return {&app.clocks[0].minimum,&app.clocks[0].maximum,&app.clocks[1].minimum,&app.clocks[1].maximum,
        &app.clocks[2].minimum,&app.clocks[2].maximum,&app.driver[0],&app.driver[1],&app.driver[2],&app.driver[3],&app.minFanSpeed};
}
void ConfigureSlider(SliderState& slider,int minimum,int maximum,int value,int step,bool preserve) {
    const bool keep=preserve&&slider.Staged();
    slider.minimum=minimum;slider.maximum=maximum;slider.step=std::max(1,step);slider.available=minimum<=maximum;
    slider.scaleMinimum=minimum;slider.scaleMaximum=maximum;
    if(!slider.available){slider.invalid=false;return;}
    if(!keep){slider.value=std::clamp(value,minimum,maximum);slider.baseline=slider.value;slider.automatic=true;slider.baselineAutomatic=true;}
    slider.invalid=slider.value<minimum||slider.value>maximum;
}
void ConfigureClockGroup(App& app,ClockGroup& group,rdna::ClockLimits limits,bool preserve) {
    group.dpm=limits;
    const int gfxUpper=group.domain==rdna::ClockDomain::Gfx&&app.gfxLinked&&app.gfxOffsetRange
        ?limits.maximum+std::max(0,app.gfxOffsetRange->maximum):limits.maximum;
    ConfigureSlider(group.minimum,limits.minimum,limits.maximum,limits.minimum,1,preserve);
    group.minimum.scaleMaximum=gfxUpper;
    ConfigureSlider(group.maximum,limits.minimum,gfxUpper,
        group.domain==rdna::ClockDomain::Gfx&&app.driver[0].available?limits.maximum+std::max(0,app.driver[0].value):limits.maximum,1,preserve);
    group.minimum.direct=group.maximum.direct=true;
}
void ConfigureGfxLink(App& app,bool preserve) {
    if(!app.gfxAvailable){app.gfxLinked=false;return;}
    const auto& offset=app.driver[0];
    app.gfxLinked=offset.available&&offset.minimum<0&&offset.maximum>0&&(-offset.minimum)%std::max(1,offset.step)==0;
    auto& group=app.clocks[0];
    app.gfxNative=rdna::RecoverGfxBaseLimits(group.dpm,app.adlxRead?app.gfxAppliedOffset:0);
    const int step=std::max(1,offset.step);
    const int maximumAlignedOffset=offset.minimum+((offset.maximum-offset.minimum)/step)*step;
    const int upper=app.gfxLinked?app.gfxNative.maximum+std::max(0,maximumAlignedOffset):app.gfxNative.maximum;
    ConfigureSlider(group.minimum,group.dpm.minimum,group.dpm.maximum,group.dpm.minimum,1,preserve);
    group.minimum.scaleMaximum=upper;
    const int draft=app.gfxNative.maximum+(app.gfxLinked?offset.value:0);
    ConfigureSlider(group.maximum,group.dpm.minimum,upper,draft,1,preserve);
    group.minimum.direct=group.maximum.direct=true;
}
void ApplySmuReadback(App& app,const rdna::SmuSnapshot& snapshot,bool preserve) {
    app.smuRead=true;app.gfxAvailable=true;app.clocks[0].dpm=snapshot.gfx;
    ConfigureClockGroup(app,app.clocks[1],snapshot.fclk,preserve);
    ConfigureClockGroup(app,app.clocks[2],snapshot.soc,preserve);
    ConfigureGfxLink(app,preserve);
}
void ApplyFanReadback(App& app,const rdna::AdlxSnapshot::Fan& snapshot,bool preserve) {
    const bool keepCurve=preserve&&app.fanCurve!=app.fanCurveBaseline;
    const bool keepZero=preserve&&app.zeroRpm!=app.zeroRpmBaseline;
    app.fan=snapshot;
    app.fanCurveBaseline=snapshot.curve;
    if(!keepCurve)app.fanCurve=snapshot.curve;
    app.zeroRpmBaseline=snapshot.zeroRpm;
    if(!keepZero)app.zeroRpm=snapshot.zeroRpm;
    if(snapshot.minSpeed.available) {
        const auto& v=snapshot.minSpeed;
        ConfigureSlider(app.minFanSpeed,v.range.minimum,v.range.maximum,v.value,v.range.step,preserve);
    } else app.minFanSpeed.available=false;
    app.minFanSpeed.direct=false;
}
void ApplyAdlxReadback(App& app,const rdna::AdlxSnapshot& snapshot,bool preserve,bool preserveFan=true) {
    app.adlxRead=true;app.gpuName=snapshot.gpuName;app.diagnostics.push_back(L"ADLX "+snapshot.version+L" · "+snapshot.gpuName);
    app.gfxAppliedOffset=snapshot.gfxMax.available?snapshot.gfxMax.value:0;
    app.gfxOffsetRange=snapshot.gfxMax.available?std::optional<rdna::Range>(snapshot.gfxMax.range):std::nullopt;
    auto configure=[&](SliderState& s,const rdna::TuningValue& v){
        if(!v.available){s.available=false;s.invalid=false;return;}
        ConfigureSlider(s,v.range.minimum,v.range.maximum,v.value,v.range.step,preserve);
        s.direct=false;
    };
    app.driver[0].title=snapshot.gfxMax.range.minimum<0?L"Max frequency offset":L"GPU maximum";
    app.driver[0].description=snapshot.gfxMax.range.minimum<0?L"Offset from the max default frequency.":L"Highest driver clock target.";
    app.driver[1].title=snapshot.voltage.range.minimum<0?L"Voltage offset":L"GPU voltage";
    app.driver[1].description=snapshot.voltage.range.minimum<0?L"Shifts entire V/F curve down by offset amount.":L"Driver voltage target.";
    configure(app.driver[0],snapshot.gfxMax);configure(app.driver[1],snapshot.voltage);
    configure(app.driver[2],snapshot.vramMax);configure(app.driver[3],snapshot.powerLimit);
    app.timingAvailable=snapshot.memoryTiming.available;
    if(app.timingAvailable){app.timingValue=snapshot.memoryTiming.value!=0;if(!preserve||!app.timingStaged)app.timingBaseline=app.timingValue;}
    else {app.timingAvailable=false;app.timingStaged=false;}
    ConfigureGfxLink(app,preserve);
    ApplyFanReadback(app,snapshot.fan,preserveFan);
}

template<class Work> void Launch(App& app,ResultKind kind,Work work) {
    App* state=&app;
    app.workers.emplace_back([state,kind,work=std::move(work)]() mutable {
        BackendResult result;result.kind=kind;result.workerId=std::this_thread::get_id();
        try { work(result); }
        catch(const std::exception& ex){result.error=ErrorText(ex);}
        catch(...){result.error=L"Unknown hardware operation failure.";}
        HWND hwnd{};bool alive{};
        {std::scoped_lock lock(state->resultMutex);state->results.push_back(std::move(result));alive=state->alive;hwnd=state->hwnd;}
        if(alive&&hwnd)PostMessageW(hwnd,kResultMessage,0,0);
    });
}
void StartInitialReads(App& app) {
    app.busy=true;SetStatus(app,L"Reading hardware…");
    Launch(app,ResultKind::Smu,[](BackendResult& r){r.smu=rdna::ReadSmu();r.diagnostics.push_back(r.smu->backend);});
    Launch(app,ResultKind::Adlx,[](BackendResult& r){r.adlx=rdna::ReadAdlx();});
}

void AddButton(App& app,float x,float y,float w,float h,int action) {
    app.buttonHits.push_back({Rect(x,y,w,h),action});
}

void DrawChrome(App& app, float width) {
    auto& g = app.g;
    g.Fill(D2D1::RectF(0, 0, g.P(width), g.P(44)), kHeader);
    g.Fill(D2D1::RectF(0, g.P(44), g.P(width), g.P(76)), kInset);
    g.Fill(D2D1::RectF(0, 0, g.P(44), g.P(44)), kAccent);
    g.Text(L"OC+", 0, 0, 44, 44, kInk, g.bold, DWRITE_TEXT_ALIGNMENT_CENTER);
    g.Text(L"RDNA4 OC+", 60, 4, 220, 36, kInk, g.brand);
    g.Text(app.gpuName, 320, 4, width - 320 - 156, 36, kMuted, g.body, DWRITE_TEXT_ALIGNMENT_TRAILING);
    const float buttonX=width-132;
    const std::array<bool,3> hovered{app.minHover,app.maxHover,app.closeHover};
    for(int i=0;i<3;++i)if(hovered[static_cast<std::size_t>(i)])
        g.Fill(D2D1::RectF(g.P(buttonX+i*44),0,g.P(buttonX+(i+1)*44),g.P(44)),i==2?kAccent:kChromeHover);
    constexpr float iconStroke=1.1F;
    g.Line(buttonX+17,22,buttonX+27,22,kInk,iconStroke);
    if(app.maximized){
        g.Stroke(D2D1::RectF(g.P(buttonX+61),g.P(16),g.P(buttonX+69),g.P(24)),kInk,iconStroke);
        g.Stroke(D2D1::RectF(g.P(buttonX+63),g.P(18),g.P(buttonX+71),g.P(26)),kInk,iconStroke);
    }else g.Stroke(D2D1::RectF(g.P(buttonX+61),g.P(17),g.P(buttonX+71),g.P(27)),kInk,iconStroke);
    g.Line(buttonX+105,17,buttonX+115,27,kInk,iconStroke);
    g.Line(buttonX+115,17,buttonX+105,27,kInk,iconStroke);
    g.Line(0,g.P(44),g.P(width),g.P(44),kRule); g.Line(0,g.P(75),g.P(width),g.P(75),kRule);
    g.Text(L"Tuning",70,44,78,32,app.page==UiPage::Tuning?kInk:kMuted,g.bold,DWRITE_TEXT_ALIGNMENT_CENTER);
    g.Text(L"Fan",158,44,66,32,app.page==UiPage::Fan?kInk:kMuted,g.bold,DWRITE_TEXT_ALIGNMENT_CENTER);
    if(app.page==UiPage::Tuning)g.Fill(D2D1::RectF(g.P(87),g.P(73),g.P(131),g.P(76)),kAccent);
    else g.Fill(D2D1::RectF(g.P(169),g.P(73),g.P(213),g.P(76)),kAccent);
    AddButton(app,70,44,78,32,7);AddButton(app,158,44,66,32,9);
}

void DrawSlider(App& app,SliderState& slider,float x,float y,float width,float height,bool compact) {
    auto& g=app.g;
    const float rowTop=compact?y:std::max(y,y+(height-76)/2),rowHeight=compact?height:76;
    const float top=compact?rowTop+std::max(4.0F,(rowHeight-28)/2):rowTop+18;
    const float trackY=compact?rowTop+rowHeight/2:rowTop+30;
    const float fieldWidth=94,fieldLeft=x+width-106,trackLeft=compact?x+54:x+std::max(214.0F,width*0.44F),trackRight=x+width-126;
    const float trackHeight=3.0F;
    const bool enabled=slider.available&&!app.busy;
    const auto ink=enabled?kInk:kDisabled;
    if(compact) g.Text(slider.title,x+20,top,36,28,enabled?kInk:kDisabled,g.body);
    else {
        g.Text(slider.title,x+20,rowTop+12,trackLeft-x-32,24,ink,g.bold);
        g.Text(slider.description,x+20,rowTop+35,trackLeft-x-32,26,kMuted,g.smallText);
    }
#ifdef RDNA_ENABLE_HOVER_EXPERIMENT
    const bool sliderHovered=app.hoveredSlider==&slider;
    const bool fieldHovered=sliderHovered&&app.hoveredField;
#endif
    g.Fill(Rect(trackLeft,trackY-trackHeight/2,trackRight-trackLeft,trackHeight),kTrack);
    if(enabled) {
        const float ratio=slider.scaleMaximum==slider.scaleMinimum?0.0F:static_cast<float>(slider.value-slider.scaleMinimum)/(slider.scaleMaximum-slider.scaleMinimum);
        const float knobX=trackLeft+(trackRight-trackLeft)*std::clamp(ratio,0.0F,1.0F);
        g.Fill(Rect(trackLeft,trackY-trackHeight/2,knobX-trackLeft,trackHeight),kAccent);
        float knobWidth=10.0F,knobHeight=20.0F;
#ifdef RDNA_ENABLE_HOVER_EXPERIMENT
        if(sliderHovered&&!fieldHovered){knobWidth=11.0F;knobHeight=22.0F;}
#endif
        const auto knob=D2D1::RoundedRect(Rect(knobX-knobWidth/2,trackY-knobHeight/2,knobWidth,knobHeight),g.P(1),g.P(1));
        g.FillRound(knob,kInk);
        if(!compact) {
            g.Text(GroupNumber(slider.minimum),trackLeft,trackY+14,64,20,kMuted,g.mono);
            g.Text(GroupNumber(slider.maximum),trackRight-64,trackY+14,64,20,kMuted,g.mono,DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    } else if(!compact) g.Text(L"—",trackLeft,trackY+14,trackRight-trackLeft,20,kDisabled,g.mono,DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto field=D2D1::RoundedRect(Rect(fieldLeft,top,fieldWidth,28),g.P(2),g.P(2));
    g.FillRound(field,kInset);
    auto fieldBorder=kTrack;
#ifdef RDNA_ENABLE_HOVER_EXPERIMENT
    if(fieldHovered&&enabled)fieldBorder=kFieldHover;
    if(app.editing==&slider)fieldBorder=kAccent;
#endif
    g.StrokeRound(field,fieldBorder);
    const auto fieldColor=slider.invalid?kError:slider.Staged()?kAccentLight:enabled?kInk:kDisabled;
    g.Text(ValueText(slider),fieldLeft+7,top,fieldWidth-43,28,fieldColor,g.mono,DWRITE_TEXT_ALIGNMENT_TRAILING);
    g.Text(slider.unit,fieldLeft+fieldWidth-30,top,25,28,enabled?kMuted:kDisabled,g.smallText,DWRITE_TEXT_ALIGNMENT_TRAILING);
    app.sliderHits.push_back({Rect(trackLeft-10,trackY-14,trackRight-trackLeft+20,28),&slider,false});
    app.sliderHits.push_back({Rect(fieldLeft,top,fieldWidth,28),&slider,true});
}

void DrawClockCard(App& app, float x, float y, float w, float h) {
    auto& g=app.g;
    const auto card=D2D1::RoundedRect(D2D1::RectF(g.P(x),g.P(y),g.P(x+w),g.P(y+h)),g.P(3),g.P(3));
    g.FillRound(card,kSurface);g.StrokeRound(card,kRule);
    g.Text(L"SMU soft limits",x+20,y,w-40,44,kInk,g.cardTitle);
    g.Line(x+1,y+44,x+w-1,y+44,kRule);
    const float groupTop=y+44, groupH=std::max(108.0F,(h-64)/3.0F);
    for(size_t i=0;i<app.clocks.size();i++) {
        auto& clock=app.clocks[i];
        const float gy=groupTop+i*groupH;
        const float rowTop=gy+30,available=std::max(76.0F,groupH-34),row=(available-4)/2;
        g.Text(clock.title,x+20,gy+6,110,28,kInk,g.bold);
        g.Text(clock.description,x+130,gy+6,100,28,kMuted,g.smallText);
        const std::wstring range=clock.minimum.available?GroupNumber(clock.minimum.minimum)+L"–"+GroupNumber(clock.maximum.maximum)+L" MHz":L"Range not read";
        g.Text(range,x+232,gy+6,w-252,28,kMuted,g.smallText,DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawSlider(app,clock.minimum,x+9,rowTop,w-18,row,true);
        DrawSlider(app,clock.maximum,x+9,rowTop+row+4,w-18,available-row-4,true);
        g.Line(x+20,gy+groupH-1,x+w-20,gy+groupH-1,kRule);
    }
}

void DrawDriverCard(App& app, float x, float y, float w, float h) {
    auto& g=app.g;
    const auto card=D2D1::RoundedRect(D2D1::RectF(g.P(x),g.P(y),g.P(x+w),g.P(y+h)),g.P(3),g.P(3));
    g.FillRound(card,kSurface);g.StrokeRound(card,kRule);
    g.Text(L"ADLX controls",x+20,y,w-40,44,kInk,g.cardTitle);
    g.Line(x+1,y+44,x+w-1,y+44,kRule);
    const float rowH=std::max(76.0F,(h-44)/5.0F);
    for(size_t i=0;i<app.driver.size();i++) DrawSlider(app,app.driver[i],x+1,y+44+i*rowH,w-2,rowH,false);
    const float timingY=y+44+4*rowH,toggleTop=timingY+std::max(0.0F,(h-(timingY-y)-72)/2);
    const bool timingEnabled=app.timingAvailable&&!app.busy;
    g.Line(x+20,timingY+rowH-1,x+w-20,timingY+rowH-1,kRule);
    g.Text(L"Fast timings",x+21,toggleTop+12,w-144,24,timingEnabled?kInk:kDisabled,g.bold);
    g.Text(app.timingAvailable?L"Use the driver’s faster memory timings.":L"Read hardware first",x+21,toggleTop+35,w-144,25,kMuted,g.smallText);
    g.Text(!app.timingAvailable?L"—":app.timingValue?L"On":L"Off",x+w-102,toggleTop+24,30,23,kMuted,g.smallText,DWRITE_TEXT_ALIGNMENT_TRAILING);
    const auto pill=D2D1::RoundedRect(D2D1::RectF(g.P(x+w-64),g.P(toggleTop+24),g.P(x+w-22),g.P(toggleTop+47)),g.P(11),g.P(11));
    g.FillRound(pill,timingEnabled&&app.timingValue?kAccent:kTrack);
    g.FillEllipse(D2D1::Ellipse(D2D1::Point2F(g.P(x+w-64+(timingEnabled&&app.timingValue?30.5F:11.5F)),g.P(toggleTop+35.5F)),g.P(8.5F),g.P(8.5F)),timingEnabled&&app.timingValue?kInk:kDisabled);
    AddButton(app,x+1,timingY,w-2,rowH,5);
}

void DrawFanCard(App& app,float x,float y,float w,float h) {
    auto& g=app.g;
    const auto card=D2D1::RoundedRect(D2D1::RectF(g.P(x),g.P(y),g.P(x+w),g.P(y+h)),g.P(3),g.P(3));
    g.FillRound(card,kSurface);g.StrokeRound(card,kRule);
    g.Text(L"Fan controls",x+20,y,w-40,44,kInk,g.cardTitle);
    g.Line(x+1,y+44,x+w-1,y+44,kRule);
    if(!app.fan.available) {
        const std::wstring message=app.adlxDone?L"Manual fan tuning isn’t available from this AMD driver/card.":L"Reading fan controls from the AMD driver…";
        g.Text(message,x+28,y+58,w-56,64,app.adlxDone?kMuted:kDisabled);
        return;
    }

    // Keep the available driver controls in one compact header row; omit the
    // minimum-speed control entirely when this driver does not expose it.
    const bool canToggle=app.fan.zeroRpmSupported&&!app.busy;
    g.Text(L"Zero RPM",x+20,y+55,180,24,kInk,g.bold);
    if(app.fan.zeroRpmSupported)g.Text(L"Stop the fans at light load.",x+20,y+79,360,24,kMuted,g.smallText);
    g.Text(app.fan.zeroRpmSupported?(app.zeroRpm?L"On":L"Off"):L"—",x+w-102,y+65,38,23,kMuted,g.smallText,DWRITE_TEXT_ALIGNMENT_TRAILING);
    const auto pill=D2D1::RoundedRect(D2D1::RectF(g.P(x+w-56),g.P(y+64),g.P(x+w-14),g.P(y+87)),g.P(11),g.P(11));
    g.FillRound(pill,canToggle&&app.zeroRpm?kAccent:kTrack);
    g.FillEllipse(D2D1::Ellipse(D2D1::Point2F(g.P(x+w-56+(canToggle&&app.zeroRpm?30.5F:11.5F)),g.P(y+75.5F)),g.P(8.5F),g.P(8.5F)),canToggle?kInk:kDisabled);
    AddButton(app,x+1,y+50,w-2,48,8);
    g.Line(x+20,y+105,x+w-20,y+105,kRule);

    float plotTop=y+132;
    if(app.fan.minSpeedSupported) {
        app.minFanSpeed.title=L"Fan speed floor";
        app.minFanSpeed.description=L"Lowest driver-set speed.";
        DrawSlider(app,app.minFanSpeed,x+1,y+108,w-2,70,false);
        plotTop=y+184;
    }
    const float plotLeft=x+76,plotW=w-104,plotH=std::max(96.0F,y+h-plotTop-54);
    app.fanPlot=Rect(plotLeft,plotTop,plotW,plotH);
    g.Fill(app.fanPlot,kInset);
    for(int i=0;i<=4;++i) {
        const float yy=plotTop+plotH*i/4;
        const float xx=plotLeft+plotW*i/4;
        g.Line(plotLeft,yy,plotLeft+plotW,yy,kTrack);
        g.Line(xx,plotTop,xx,plotTop+plotH,kTrack);
        const int speed=app.fan.speedRange.maximum-(app.fan.speedRange.maximum-app.fan.speedRange.minimum)*i/4;
        const int temp=app.fan.temperatureRange.minimum+(app.fan.temperatureRange.maximum-app.fan.temperatureRange.minimum)*i/4;
        g.Text(std::to_wstring(speed)+L"%",x+20,yy-9,42,18,kMuted,g.mono,DWRITE_TEXT_ALIGNMENT_TRAILING);
        g.Text(std::to_wstring(temp)+L"°",xx-20,plotTop+plotH+8,42,18,kMuted,g.mono,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    g.Text(L"FAN SPEED",x+20,plotTop-26,100,20,kMuted,g.smallText);
    g.Text(L"TEMPERATURE (°C)",plotLeft+plotW-142,plotTop+plotH+27,142,18,kMuted,g.smallText,DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto position=[&](const FanPoint& p) {
        const float xr=static_cast<float>(p.temperature-app.fan.temperatureRange.minimum)/std::max(1,app.fan.temperatureRange.maximum-app.fan.temperatureRange.minimum);
        const float yr=static_cast<float>(p.speed-app.fan.speedRange.minimum)/std::max(1,app.fan.speedRange.maximum-app.fan.speedRange.minimum);
        return D2D1::Point2F(g.P(plotLeft+std::clamp(xr,0.0F,1.0F)*plotW),g.P(plotTop+(1.0F-std::clamp(yr,0.0F,1.0F))*plotH));
    };
    for(std::size_t i=1;i<app.fanCurve.size();++i) {
        const auto a=position(app.fanCurve[i-1]),b=position(app.fanCurve[i]);
        g.Line(a.x/g.scale,a.y/g.scale,b.x/g.scale,b.y/g.scale,kAccent,2.0F);
    }
    app.fanPointHits.clear();
    for(std::size_t i=0;i<app.fanCurve.size();++i) {
        const auto p=position(app.fanCurve[i]);const float px=p.x/g.scale,py=p.y/g.scale;
        g.FillEllipse(D2D1::Ellipse(p,g.P(7),g.P(7)),kInk);
        g.FillEllipse(D2D1::Ellipse(p,g.P(3),g.P(3)),kAccent);
        app.fanPointHits.push_back({Rect(px-13,py-13,26,26),i});
    }
}

void DrawActionBar(App& app, float width, float y) {
    auto& g=app.g;
    const auto strip=D2D1::RoundedRect(D2D1::RectF(g.P(24),g.P(y),g.P(width-24),g.P(y+48)),g.P(3),g.P(3));
    g.FillRound(strip,kSurface);g.StrokeRound(strip,kRule);
    const std::array<std::pair<const wchar_t*,float>,3> actions{{{L"Apply",76.0F},{L"Reset to default",142.0F},{L"Undo",72.0F}}};
    const auto pending=PendingCount(app);
    const bool applyEnabled=pending>0&&!app.busy;
    const bool resetEnabled=(app.page==UiPage::Fan?app.adlxRead:(app.smuRead&&app.adlxRead))&&!app.busy;
    const bool undoEnabled=pending>0&&!app.busy;
    float right=width-36;
    for(std::size_t i=0;i<actions.size();++i) {
        const auto [label,dip]=actions[i];
        right-=dip;
        const auto r=D2D1::RoundedRect(D2D1::RectF(g.P(right),g.P(y+6),g.P(right+dip),g.P(y+42)),g.P(2),g.P(2));
        const bool enabled=i==0?applyEnabled:i==1?resetEnabled:undoEnabled;
        auto fill=i==0&&enabled?kAccent:kSurface;
        if(i!=0&&enabled&&app.hoverAction==static_cast<int>(i+1))fill=D2D1::ColorF(0x303030);
        g.FillRound(r,fill);
        if(i!=0)g.StrokeRound(r,kTrack);
        g.Text(label,right,y+6,dip,36,enabled?(i==0?kInk:kMuted):kDisabled,g.bold,DWRITE_TEXT_ALIGNMENT_CENTER);
        AddButton(app,right,y+6,dip,36,static_cast<int>(i+1));
        right-=8;
    }
    const std::wstring state=pending?std::to_wstring(pending)+L" pending change"+(pending==1?L"":L"s"):app.readCompleted?L"No pending changes":L"Waiting for startup read.";
    g.Text(state,40,y,std::max(1.0F,right-44),48,pending?kInk:kMuted);
}

void DrawDetails(App& app,float width,float height) {
    auto& g=app.g;
    g.Fill(Rect(0,0,width,height),D2D1::ColorF(0x000000,0.68F));
    const float w=std::min(660.0F,width-64),h=std::min(420.0F,height-120),x=(width-w)/2,y=(height-h)/2;
    const auto box=D2D1::RoundedRect(Rect(g.P(x),g.P(y),g.P(w),g.P(h)),g.P(4),g.P(4));
    g.FillRound(box,kSurface); g.StrokeRound(box,kRule);
    g.Text(L"Operation details",x+20,y,w-72,44,kInk,g.cardTitle);
    const auto close=D2D1::RoundedRect(Rect(g.P(x+w-48),g.P(y+8),g.P(36),g.P(28)),g.P(2),g.P(2));
    g.FillRound(close,app.detailsHover?kAccent:kSurface);
    g.Line(x+w-37,y+16,x+w-23,y+30,kInk); g.Line(x+w-23,y+16,x+w-37,y+30,kInk);
    g.Line(x+1,y+44,x+w-1,y+44,kRule);
    std::wstring details=Join(app.diagnostics);
    if(details.empty()) details=L"No diagnostic details are available yet.";
    g.TextTop(details,x+20,y+60,w-40,h-80,kMuted,g.smallText);
    AddButton(app,x+w-48,y+8,36,28,6);
}

void Render(App& app) {
    if (FAILED(app.g.EnsureTarget(app.hwnd))) return;
    auto& g=app.g; RECT rc{}; GetClientRect(app.hwnd,&rc);
    const float width=static_cast<float>(rc.right),height=static_cast<float>(rc.bottom), s=g.scale;
    const float dipW=width/s,dipH=height/s,margin=24,gap=24;
    const float bodyTop=100,actionY=dipH-72,cardH=std::max(180.0F,actionY-bodyTop-24),colW=(dipW-2*margin-gap)/2;
    app.sliderHits.clear();app.buttonHits.clear();app.fanPointHits.clear();
    g.target->BeginDraw();g.target->Clear(kCanvas);
    DrawChrome(app,dipW);
    if(app.page==UiPage::Tuning) {
        DrawClockCard(app,margin,bodyTop,colW,cardH);
        DrawDriverCard(app,margin+colW+gap,bodyTop,colW,cardH);
    } else DrawFanCard(app,margin,bodyTop,dipW-2*margin,cardH);
    DrawActionBar(app,dipW,actionY);
    auto detailsColor=app.diagnostics.empty()?kDisabled:kMuted;
    g.Text(L"Details",dipW-112,44,88,32,detailsColor,g.bold,DWRITE_TEXT_ALIGNMENT_TRAILING);
    if(!app.diagnostics.empty()) AddButton(app,dipW-112,44,88,32,4);
    if(app.detailsOpen) DrawDetails(app,dipW,dipH);
    HRESULT hr=g.target->EndDraw();
    if(hr==D2DERR_RECREATE_TARGET)g.DropTarget();
    if(app.editing){
        auto sliders=AllSliders(app);for(std::size_t i=0;i<sliders.size();++i)if(sliders[i]==app.editing){
            const auto hit=std::find_if(app.sliderHits.begin(),app.sliderHits.end(),[&](const HitRegion& h){return h.slider==sliders[i]&&h.field;});
            if(hit!=app.sliderHits.end()){
                const RECT bounds{static_cast<LONG>((hit->rect.left+6)*s),static_cast<LONG>((hit->rect.top+4)*s),static_cast<LONG>((hit->rect.right-39)*s),static_cast<LONG>((hit->rect.bottom-4)*s)};
                SetWindowPos(app.editControls[i],nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
            }break;
        }
    }
}

void StageSlider(App& app,SliderState& slider,int value) {
    value=std::clamp(value,slider.minimum,slider.maximum);
    value=slider.minimum+((value-slider.minimum)/std::max(1,slider.step))*std::max(1,slider.step);
    slider.value=value;slider.automatic=false;slider.invalid=false;
    if(&slider==&app.clocks[0].maximum&&app.gfxLinked&&app.gfxOffsetRange&&value>app.gfxNative.maximum){
        const auto plan=rdna::PlanLinkedGfx(value,app.driver[0].value,app.gfxNative,*app.gfxOffsetRange);
        slider.value=plan.target;
        if(plan.offset>app.driver[0].value){StageSlider(app,app.driver[0],plan.offset);app.gfxDraftLinked=true;}
    }
}
void StageAt(App& app,SliderState& slider,float x) {
    const auto found=std::find_if(app.sliderHits.begin(),app.sliderHits.end(),[&](const HitRegion& h){return h.slider==&slider&&!h.field;});
    if(found==app.sliderHits.end())return;
    const float left=found->rect.left+10,right=found->rect.right-10;
    const float ratio=std::clamp((x-left)/std::max(1.0F,right-left),0.0F,1.0F);
    StageSlider(app,slider,slider.scaleMinimum+static_cast<int>(std::lround(ratio*(slider.scaleMaximum-slider.scaleMinimum))));
}
void StageFanPoint(App& app,std::size_t index,float x,float y) {
    if(index>=app.fanCurve.size()||!app.fan.available)return;
    const auto& tr=app.fan.temperatureRange;const auto& sr=app.fan.speedRange;
    const float xr=std::clamp((x-app.fanPlot.left)/std::max(1.0F,app.fanPlot.right-app.fanPlot.left),0.0F,1.0F);
    const float yr=1.0F-std::clamp((y-app.fanPlot.top)/std::max(1.0F,app.fanPlot.bottom-app.fanPlot.top),0.0F,1.0F);
    const int ts=std::max(1,tr.step),ss=std::max(1,sr.step);
    auto snap=[](int value,int minimum,int step){return minimum+static_cast<int>(std::lround(static_cast<double>(value-minimum)/step))*step;};
    int t=snap(tr.minimum+static_cast<int>(std::lround(xr*(tr.maximum-tr.minimum))),tr.minimum,ts);
    int slo=sr.minimum,shi=sr.maximum,tlo=tr.minimum,thi=tr.maximum;
    if(index){tlo=app.fanCurve[index-1].temperature+ts;slo=app.fanCurve[index-1].speed;}
    if(index+1<app.fanCurve.size()){thi=app.fanCurve[index+1].temperature-ts;shi=app.fanCurve[index+1].speed;}
    if(tlo>thi)return;
    t=std::clamp(t,tlo,thi);
    int s=snap(sr.minimum+static_cast<int>(std::lround(yr*(sr.maximum-sr.minimum))),sr.minimum,ss);
    if(slo>shi)return;
    s=std::clamp(s,slo,shi);
    app.fanCurve[index]={t,s};
    SetStatus(app,L"Fan curve staged. Apply to send it to the AMD driver.");
}
bool ParseEditor(App& app,std::size_t index,bool accept) {
    if(!app.editing)return true;
    SliderState* slider=app.editing;
    if(accept){
        wchar_t text[64]{};GetWindowTextW(app.editControls[index],text,64);
        wchar_t* end{};const long value=std::wcstol(text,&end,10);
        if(end==text||*end!=L'\0'||value<slider->minimum||value>slider->maximum||
            ((value-slider->minimum)%std::max(1,slider->step))!=0){
            slider->invalid=true;SetStatus(app,L"Value is outside the supported range or step.",kError);InvalidateRect(app.hwnd,nullptr,FALSE);return false;
        }
        StageSlider(app,*slider,static_cast<int>(value));
    }else slider->invalid=false;
    app.editing=nullptr;ShowWindow(app.editControls[index],SW_HIDE);InvalidateRect(app.hwnd,nullptr,FALSE);return true;
}
LRESULT CALLBACK EditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* binding=reinterpret_cast<EditBinding*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(binding&&binding->app){
        if(msg==WM_KEYDOWN&&wp==VK_RETURN){ParseEditor(*binding->app,static_cast<std::size_t>(binding->index),true);return 0;}
        if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){ParseEditor(*binding->app,static_cast<std::size_t>(binding->index),false);return 0;}
        if(msg==WM_KILLFOCUS)ParseEditor(*binding->app,static_cast<std::size_t>(binding->index),true);
        if(binding->original)return CallWindowProcW(binding->original,hwnd,msg,wp,lp);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void CreateEditors(App& app,HWND hwnd){
    app.editBrush=CreateSolidBrush(RGB(31,31,31));
    app.editFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Cascadia Mono");
    auto sliders=AllSliders(app);
    for(std::size_t i=0;i<sliders.size();++i){
        HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_RIGHT|ES_AUTOHSCROLL,0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditBase+i)),GetModuleHandleW(nullptr),nullptr);
        app.editControls[i]=edit;app.editBindings[i]={&app,static_cast<int>(i),reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(EditProc)))};
        SetWindowLongPtrW(edit,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app.editBindings[i]));SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(app.editFont),TRUE);ShowWindow(edit,SW_HIDE);
    }
}
void BeginEditor(App& app,SliderState& slider){
    auto sliders=AllSliders(app);for(std::size_t i=0;i<sliders.size();++i)if(sliders[i]==&slider){
        if(app.editing){auto old=std::find(sliders.begin(),sliders.end(),app.editing);if(old!=sliders.end())ParseEditor(app,static_cast<std::size_t>(old-sliders.begin()),true);}
        app.editing=&slider;SetWindowTextW(app.editControls[i],ValueText(slider).c_str());InvalidateRect(app.hwnd,nullptr,FALSE);Render(app);
        ShowWindow(app.editControls[i],SW_SHOW);SetFocus(app.editControls[i]);SendMessageW(app.editControls[i],EM_SETSEL,0,-1);return;
    }
}
void Undo(App& app){
    if(app.editing){auto all=AllSliders(app);auto old=std::find(all.begin(),all.end(),app.editing);if(old!=all.end())ParseEditor(app,static_cast<std::size_t>(old-all.begin()),false);}
    for(auto* slider:AllSliders(app)){slider->value=slider->baseline;slider->automatic=slider->baselineAutomatic;slider->invalid=false;}
    app.timingValue=app.timingBaseline;app.timingStaged=false;app.gfxDraftLinked=false;SetStatus(app,L"Pending changes undone.");InvalidateRect(app.hwnd,nullptr,FALSE);
    app.fanCurve=app.fanCurveBaseline;app.zeroRpm=app.zeroRpmBaseline;
}
bool BuildRequests(App& app,std::vector<rdna::ClockRequest>& clocks,rdna::AdlxRequest& driver,rdna::FanRequest& fan){
    for(const auto& group:app.clocks){
        const auto& lo=group.minimum;const auto& hi=group.maximum;
        if(!lo.Staged()&&!hi.Staged())continue;
        if(!lo.automatic&&!hi.automatic&&lo.value>hi.value){SetStatus(app,group.title+L" minimum must not exceed maximum.",kError);return false;}
        if(group.domain==rdna::ClockDomain::Gfx&&hi.Staged()&&app.gfxLinked){
            const auto plan=rdna::PlanLinkedGfx(hi.value,app.driver[0].value,app.gfxNative,*app.gfxOffsetRange);
            if(plan.releaseMaximum&&app.driver[0].value<plan.offset){SetStatus(app,L"The ADLX offset is below the staged GFX extension.",kError);return false;}
            clocks.push_back({group.domain,lo.Staged()?lo.Request():std::nullopt,plan.softMaximum,plan.releaseMaximum});
        }else clocks.push_back({group.domain,lo.Staged()?lo.Request():std::nullopt,hi.Staged()?hi.Request():std::nullopt});
    }
    auto value=[](SliderState& s)->std::optional<int>{return s.Staged()?std::optional<int>(s.value):std::nullopt;};
    driver={value(app.driver[0]),value(app.driver[1]),value(app.driver[2]),app.timingStaged?std::optional<int>(app.timingValue?1:0):std::nullopt,value(app.driver[3])};
    if(app.fanCurve!=app.fanCurveBaseline) {
        try { rdna::ValidateFanCurve(app.fanCurve,app.fan.speedRange,app.fan.temperatureRange); }
        catch(const std::exception& ex){SetStatus(app,ErrorText(ex),kError);return false;}
        fan.curve=app.fanCurve;
    }
    if(app.zeroRpm!=app.zeroRpmBaseline)fan.zeroRpm=app.zeroRpm;
    fan.minSpeed=value(app.minFanSpeed);
    for(auto* s:AllSliders(app))if(s->invalid){SetStatus(app,L"Correct the highlighted value before applying.",kError);return false;}
    return true;
}
void StartApply(App& app){
    std::vector<rdna::ClockRequest> clocks;rdna::AdlxRequest driver{};rdna::FanRequest fan{};if(!BuildRequests(app,clocks,driver,fan))return;
    if(clocks.empty()&&!driver.gfxMax&&!driver.voltage&&!driver.vramMax&&!driver.memoryTiming&&!driver.powerLimit&&!fan.curve&&!fan.minSpeed&&!fan.zeroRpm)return;
    app.busy=true;SetStatus(app,L"Applying staged controls…");InvalidateRect(app.hwnd,nullptr,FALSE);
    Launch(app,ResultKind::Apply,[clocks=std::move(clocks),driver,fan](BackendResult& r){
        if(driver.gfxMax||driver.voltage||driver.vramMax||driver.memoryTiming||driver.powerLimit){auto lines=rdna::ApplyAdlx(driver);r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());r.adlxApplied=true;}
        if(!clocks.empty()){auto lines=rdna::ApplySmu(clocks);r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());r.smuApplied=true;}
        if(fan.curve||fan.minSpeed||fan.zeroRpm){auto lines=rdna::ApplyFan(fan);r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());}
        try{r.adlx=rdna::ReadAdlx();}catch(const std::exception& ex){r.diagnostics.push_back(L"ADLX readback: "+ErrorText(ex));}
        try{r.smu=rdna::ReadSmu();}catch(const std::exception& ex){r.diagnostics.push_back(L"SMU readback: "+ErrorText(ex));}
    });
}
void StartFanReset(App& app) {
    if(MessageBoxW(app.hwnd,L"Restore the AMD driver’s default fan curve, minimum speed, and Zero RPM setting?",kWindowTitle,MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return;
    app.busy=true;SetStatus(app,L"Restoring default fan settings…");InvalidateRect(app.hwnd,nullptr,FALSE);
    Launch(app,ResultKind::FanReset,[](BackendResult& r){
        auto lines=rdna::ResetFanToDefault();r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());
        try{r.adlx=rdna::ReadAdlx();}catch(const std::exception& ex){r.diagnostics.push_back(L"ADLX readback: "+ErrorText(ex));}
    });
}
void StartReset(App& app){
    if(MessageBoxW(app.hwnd,L"Reset GPU tuning to stock defaults? This immediately restores automatic SMU clock limits and supported neutral ADLX tuning values.",kWindowTitle,MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return;
    app.busy=true;SetStatus(app,L"Resetting tuning to stock defaults…");InvalidateRect(app.hwnd,nullptr,FALSE);
    Launch(app,ResultKind::Reset,[](BackendResult& r){
        const auto current=rdna::ReadAdlx();
        auto neutral=[](const rdna::TuningValue& v)->std::optional<int>{const int step=std::max(1,v.range.step);return v.available&&v.range.minimum<=0&&v.range.maximum>=0&&(-v.range.minimum)%step==0?std::optional<int>(0):std::nullopt;};
        rdna::AdlxRequest req{neutral(current.gfxMax),neutral(current.voltage),current.vramMax.available?std::optional<int>(current.vramMax.range.minimum):std::nullopt,
            current.memoryTiming.available?std::optional<int>(0):std::nullopt,neutral(current.powerLimit)};
        auto lines=rdna::ApplyAdlx(req);r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());r.adlxApplied=true;
        lines=rdna::RestoreSmuDefaults();r.diagnostics.insert(r.diagnostics.end(),lines.begin(),lines.end());r.smuApplied=true;
        try{r.adlx=rdna::ReadAdlx();}catch(const std::exception& ex){r.diagnostics.push_back(L"ADLX readback: "+ErrorText(ex));}
        try{r.smu=rdna::ReadSmu();}catch(const std::exception& ex){r.diagnostics.push_back(L"SMU readback: "+ErrorText(ex));}
    });
}
void DrainResults(App& app){
    std::deque<BackendResult> results;{std::scoped_lock lock(app.resultMutex);results.swap(app.results);}
    for(auto& result:results){
        auto worker=std::find_if(app.workers.begin(),app.workers.end(),[&](const std::thread& thread){return thread.get_id()==result.workerId;});
        if(worker!=app.workers.end()){
            worker->join();
            app.workers.erase(worker);
        }
        app.diagnostics.insert(app.diagnostics.end(),result.diagnostics.begin(),result.diagnostics.end());
        if(!result.error.empty()){app.diagnostics.push_back(result.error);SetStatus(app,L"Hardware operation failed. See Details.",kError);}
        if(result.kind==ResultKind::Smu){app.smuDone=true;if(result.smu){ApplySmuReadback(app,*result.smu,true);SetStatus(app,L"SMU ranges loaded · finishing driver read…");}}
        if(result.kind==ResultKind::Adlx){app.adlxDone=true;if(result.adlx){ApplyAdlxReadback(app,*result.adlx,true);SetStatus(app,L"Driver controls loaded · finishing startup read…");}}
        if(result.kind==ResultKind::Apply||result.kind==ResultKind::Reset||result.kind==ResultKind::FanReset){
            if(result.smu)ApplySmuReadback(app,*result.smu,result.kind==ResultKind::Apply);
            if(result.adlx)ApplyAdlxReadback(app,*result.adlx,result.kind!=ResultKind::Reset,result.kind!=ResultKind::FanReset);
            if(!result.error.size()&&result.kind==ResultKind::Apply){
                for(auto* s:AllSliders(app))if(s->Staged()){s->baseline=s->value;s->baselineAutomatic=s->automatic;}
                app.timingBaseline=app.timingValue;app.timingStaged=false;
            }
            if(!result.error.size()&&result.kind==ResultKind::Reset){
                for(auto* s:AllSliders(app))if(s!=&app.minFanSpeed){s->value=s->baseline;s->automatic=s->baselineAutomatic;s->invalid=false;}
                app.timingValue=app.timingBaseline;app.timingStaged=false;
            }
            if(!result.error.size()&&result.kind==ResultKind::FanReset){
                app.fanCurveBaseline=app.fanCurve;app.zeroRpmBaseline=app.zeroRpm;
                if(app.minFanSpeed.available)app.minFanSpeed.baseline=app.minFanSpeed.value;
            }
            app.gfxDraftLinked=false;app.busy=false;
            if(result.error.empty())SetStatus(app,result.kind==ResultKind::Reset?L"Tuning returned to stock defaults.":result.kind==ResultKind::FanReset?L"Fan settings returned to AMD defaults.":L"Changes applied and hardware readback refreshed.");
        }
        app.readCompleted=app.smuRead||app.adlxRead;
        if((result.kind==ResultKind::Smu||result.kind==ResultKind::Adlx)&&app.smuDone&&app.adlxDone){app.busy=false;app.readCompleted=app.smuRead||app.adlxRead;SetStatus(app,app.readCompleted?L"Readback updated.":L"Hardware read failed. Open Details for diagnostics.",app.readCompleted?kMuted:kError);}
    }
    InvalidateRect(app.hwnd,nullptr,FALSE);
}

LRESULT HitTest(HWND hwnd,LPARAM lp){
    if(IsZoomed(hwnd))return HTCLIENT;
    POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&point);
    RECT rc{};GetClientRect(hwnd,&rc);const UINT dpi=GetDpiForWindow(hwnd);
    const int buttonWidth=std::max(1,44*static_cast<int>(dpi)/96),buttonStart=rc.right-3*buttonWidth;
    // These regions are custom-drawn client buttons; keep them out of the resize bands.
    if(point.y>=0&&point.y<44*static_cast<int>(dpi)/96&&point.x>=buttonStart)return HTCLIENT;
    const int frame=std::max(5,static_cast<int>(GetSystemMetricsForDpi(SM_CXSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi)));
    const bool left=point.x<frame,right=point.x>=rc.right-frame,top=point.y<frame,bottom=point.y>=rc.bottom-frame;
    if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
    if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
    if(point.y<44*static_cast<int>(dpi)/96&&point.x<buttonStart)return HTCAPTION;
    return HTCLIENT;
}
int ChromeButtonAt(HWND hwnd,int x,int y){
    RECT rc{};GetClientRect(hwnd,&rc);const int width=std::max(1,44*static_cast<int>(GetDpiForWindow(hwnd))/96),start=rc.right-3*width;
    if(y<0||y>=44*static_cast<int>(GetDpiForWindow(hwnd))/96||x<start)return 0;
    if(x<start+width)return 1;if(x<start+2*width)return 2;if(x<start+3*width)return 3;return 0;
}
void InvokeChromeButton(HWND hwnd,int button){
    if(button==1)ShowWindow(hwnd,SW_MINIMIZE);
    else if(button==2){if(IsZoomed(hwnd))ShowWindow(hwnd,SW_RESTORE);else ShowWindow(hwnd,SW_MAXIMIZE);}
    else if(button==3)PostMessageW(hwnd,WM_CLOSE,0,0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(msg) {
    case WM_NCCREATE: {
        auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);app=static_cast<App*>(cs->lpCreateParams);app->hwnd=hwnd;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));return TRUE;
    }
    case WM_CREATE: {
        const HRESULT hr=app->g.Initialize(hwnd);if(FAILED(hr))return -1;
        const int disabled=1;DwmSetWindowAttribute(hwnd,DWMWA_NCRENDERING_POLICY,&disabled,sizeof(disabled));
        const int noBorder=static_cast<int>(0xFFFFFFFE);DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&noBorder,sizeof(noBorder));
        const int dark=1;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
        CreateEditors(*app,hwnd);
        StartInitialReads(*app);
        return 0;
    }
    case WM_NCCALCSIZE:return 0;
    case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:{
        const int disabled=1;DwmSetWindowAttribute(hwnd,DWMWA_NCRENDERING_POLICY,&disabled,sizeof(disabled));
        const int noBorder=static_cast<int>(0xFFFFFFFE);DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&noBorder,sizeof(noBorder));
        InvalidateRect(hwnd,nullptr,FALSE);return TRUE;
    }
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_STYLECHANGED:{
        const int disabled=1;DwmSetWindowAttribute(hwnd,DWMWA_NCRENDERING_POLICY,&disabled,sizeof(disabled));
        const int noBorder=static_cast<int>(0xFFFFFFFE);DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&noBorder,sizeof(noBorder));
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    case WM_NCHITTEST:return HitTest(hwnd,lp);
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLOREDIT:if(app){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,RGB(245,245,245));SetBkColor(dc,RGB(31,31,31));return reinterpret_cast<LRESULT>(app->editBrush);}return DefWindowProcW(hwnd,msg,wp,lp);
    case WM_SIZE:
        if(app&&app->g.target){app->g.target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));InvalidateRect(hwnd,nullptr,FALSE);}
        if(app)app->maximized=wp==SIZE_MAXIMIZED;return 0;
    case WM_DPICHANGED: {
        if(app){app->g.scale=static_cast<float>(HIWORD(wp))/96.0F;if(app->g.writeFactory)app->g.CreateFormats();}
        auto* suggested=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if(!app)return 0;
        if(!app->g.trackingMouse){TRACKMOUSEEVENT tme{sizeof(tme),TME_LEAVE,hwnd,0};TrackMouseEvent(&tme);app->g.trackingMouse=true;}
        const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        RECT rc{};GetClientRect(hwnd,&rc);const int dpi=static_cast<int>(GetDpiForWindow(hwnd)),buttonWidth=std::max(1,44*dpi/96),start=rc.right-3*buttonWidth;
        const bool oldMinHover=app->minHover,oldMaxHover=app->maxHover,oldCloseHover=app->closeHover;
        app->minHover=y>=0&&y<44*dpi/96&&x>=start&&x<start+buttonWidth;
        app->maxHover=y>=0&&y<44*dpi/96&&x>=start+buttonWidth&&x<start+2*buttonWidth;
        app->closeHover=y>=0&&y<44*dpi/96&&x>=start+2*buttonWidth&&x<start+3*buttonWidth;
        const float dipX=static_cast<float>(x)*96.0F/GetDpiForWindow(hwnd),dipY=static_cast<float>(y)*96.0F/GetDpiForWindow(hwnd);
        bool changed=oldMinHover!=app->minHover||oldMaxHover!=app->maxHover||oldCloseHover!=app->closeHover;
        if(app->dragging){StageAt(*app,*app->dragging,dipX);changed=true;}
        if(app->draggingFanPoint>=0){StageFanPoint(*app,static_cast<std::size_t>(app->draggingFanPoint),dipX,dipY);changed=true;}
        int action=0;for(const auto& b:app->buttonHits)if(Contains(b.rect,dipX,dipY)){action=b.action;break;}
        if(action!=app->hoverAction){app->hoverAction=action;changed=true;}
        bool detailsHover=false;for(const auto& b:app->buttonHits)if(b.action==6&&Contains(b.rect,dipX,dipY))detailsHover=true;
        if(detailsHover!=app->detailsHover){app->detailsHover=detailsHover;changed=true;}
#ifdef RDNA_ENABLE_HOVER_EXPERIMENT
        SliderState* hovered=nullptr;bool fieldHovered=false;
        if(!app->detailsOpen){
            for(const auto& hit:app->sliderHits)if(hit.field&&Contains(hit.rect,dipX,dipY)){hovered=hit.slider;fieldHovered=true;break;}
            if(!hovered)for(const auto& hit:app->sliderHits)if(!hit.field&&Contains(hit.rect,dipX,dipY)){hovered=hit.slider;break;}
        }
        if(hovered!=app->hoveredSlider||fieldHovered!=app->hoveredField){app->hoveredSlider=hovered;app->hoveredField=fieldHovered;changed=true;}
#endif
        if(changed)InvalidateRect(hwnd,nullptr,FALSE);return 0;
    }
    case WM_MOUSELEAVE:if(app){app->g.trackingMouse=false;app->minHover=app->maxHover=app->closeHover=false;app->hoverAction=0;app->detailsHover=false;
#ifdef RDNA_ENABLE_HOVER_EXPERIMENT
        app->hoveredSlider=nullptr;app->hoveredField=false;
#endif
        InvalidateRect(hwnd,nullptr,FALSE);}return 0;
    case WM_LBUTTONDOWN: {
        if(!app)return 0;
        const int px=GET_X_LPARAM(lp),py=GET_Y_LPARAM(lp);
        if(app->detailsOpen){
            const float x=static_cast<float>(px)*96.0F/GetDpiForWindow(hwnd),y=static_cast<float>(py)*96.0F/GetDpiForWindow(hwnd);
            const auto close=std::find_if(app->buttonHits.begin(),app->buttonHits.end(),[](const ButtonRegion& b){return b.action==6;});
            if(close!=app->buttonHits.end()&&Contains(close->rect,x,y))app->detailsOpen=false;
            else {
                RECT client{};GetClientRect(hwnd,&client);const float scale=static_cast<float>(GetDpiForWindow(hwnd))/96.0F;
                const float width=static_cast<float>(client.right)/scale,height=static_cast<float>(client.bottom)/scale;
                const float w=std::min(660.0F,width-64),h=std::min(420.0F,height-120),left=(width-w)/2,top=(height-h)/2;
                if(!Contains(Rect(left,top,w,h),x,y))app->detailsOpen=false;
            }
            InvalidateRect(hwnd,nullptr,FALSE);return 0;
        }
        const int chrome=ChromeButtonAt(hwnd,px,py);
        if(chrome){app->chromePressed=chrome;SetCapture(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        const float x=static_cast<float>(GET_X_LPARAM(lp))*96.0F/GetDpiForWindow(hwnd),y=static_cast<float>(GET_Y_LPARAM(lp))*96.0F/GetDpiForWindow(hwnd);
        if(app->editing){auto all=AllSliders(*app);auto old=std::find(all.begin(),all.end(),app->editing);if(old!=all.end()&&!ParseEditor(*app,static_cast<std::size_t>(old-all.begin()),true))return 0;}
        if(app->page==UiPage::Fan&&app->fan.available&&!app->busy){
            for(const auto& hit:app->fanPointHits)if(Contains(hit.rect,x,y)){
                app->draggingFanPoint=static_cast<int>(hit.index);SetCapture(hwnd);StageFanPoint(*app,hit.index,x,y);InvalidateRect(hwnd,nullptr,FALSE);return 0;
            }
        }
        for(const auto& hit:app->sliderHits){if(!Contains(hit.rect,x,y))continue;
            if(hit.field){BeginEditor(*app,*hit.slider);return 0;}
            if(hit.slider->available&&!app->busy){app->dragging=hit.slider;SetCapture(hwnd);StageAt(*app,*hit.slider,x);InvalidateRect(hwnd,nullptr,FALSE);}return 0;
        }
        for(const auto& b:app->buttonHits)if(Contains(b.rect,x,y)){
            if(b.action>=1&&b.action<=3&&!app->busy){if(b.action==1)StartApply(*app);else if(b.action==2){if(app->page==UiPage::Fan)StartFanReset(*app);else StartReset(*app);}else Undo(*app);}
            else if(b.action==4){app->detailsOpen=true;InvalidateRect(hwnd,nullptr,FALSE);}
            else if(b.action==5&&app->timingAvailable&&!app->busy){app->timingValue=!app->timingValue;app->timingStaged=app->timingValue!=app->timingBaseline;InvalidateRect(hwnd,nullptr,FALSE);}
            else if(b.action==6){app->detailsOpen=false;InvalidateRect(hwnd,nullptr,FALSE);}
            else if(b.action==7){app->page=UiPage::Tuning;InvalidateRect(hwnd,nullptr,FALSE);}
            else if(b.action==9){app->page=UiPage::Fan;InvalidateRect(hwnd,nullptr,FALSE);}
            else if(b.action==8&&app->fan.zeroRpmSupported&&!app->busy){app->zeroRpm=!app->zeroRpm;SetStatus(*app,L"Zero RPM setting staged. Apply to send it to the AMD driver.");InvalidateRect(hwnd,nullptr,FALSE);}
            return 0;
        }
        if(app->detailsOpen){app->detailsOpen=false;InvalidateRect(hwnd,nullptr,FALSE);}
        return 0;
    }
    case WM_LBUTTONUP: {
        if(!app)return 0;
        if(app->chromePressed){const int pressed=app->chromePressed;app->chromePressed=0;const bool same=ChromeButtonAt(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp))==pressed;if(GetCapture()==hwnd)ReleaseCapture();if(same)InvokeChromeButton(hwnd,pressed);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(app->dragging){app->dragging=nullptr;ReleaseCapture();InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(app->draggingFanPoint>=0){app->draggingFanPoint=-1;ReleaseCapture();InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        return 0;
    }
    case WM_CAPTURECHANGED:if(app){app->chromePressed=0;app->dragging=nullptr;app->draggingFanPoint=-1;InvalidateRect(hwnd,nullptr,FALSE);}return 0;
    case WM_CANCELMODE:if(app){app->chromePressed=0;app->dragging=nullptr;app->draggingFanPoint=-1;}if(GetCapture()==hwnd)ReleaseCapture();return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi=reinterpret_cast<MINMAXINFO*>(lp);const UINT dpi=GetDpiForWindow(hwnd);
        mmi->ptMinTrackSize.x=static_cast<LONG>(1000*dpi/96.0F);mmi->ptMinTrackSize.y=static_cast<LONG>(640*dpi/96.0F);return 0;
    }
    case WM_PAINT: {PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);if(app)Render(*app);EndPaint(hwnd,&ps);return 0;}
    case kResultMessage:if(app)DrainResults(*app);return 0;
    case WM_DESTROY:if(app){std::scoped_lock lock(app->resultMutex);app->alive=false;}PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,wp,lp);
    }
}
}

namespace {
bool IsProcessElevated(bool& elevated) {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return false;
    TOKEN_ELEVATION info{};
    DWORD returned{};
    const BOOL queried=GetTokenInformation(token,TokenElevation,&info,sizeof(info),&returned);
    CloseHandle(token);
    if (!queried) return false;
    elevated=info.TokenIsElevated!=0;
    return true;
}

bool RelaunchElevated(PWSTR parameters) {
    wchar_t executable[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,executable,MAX_PATH);
    if(length==0||length>=MAX_PATH)return false;
    SHELLEXECUTEINFOW launch{};
    launch.cbSize=sizeof(launch);
    launch.fMask=SEE_MASK_NOASYNC;
    launch.lpVerb=L"runas";
    launch.lpFile=executable;
    launch.lpParameters=parameters&&*parameters?parameters:nullptr;
    launch.nShow=SW_SHOWNORMAL;
    return ShellExecuteExW(&launch)!=FALSE;
}
}

int WINAPI wWinMain(_In_ HINSTANCE instance,_In_opt_ HINSTANCE,_In_ PWSTR commandLine,_In_ int show) {
    bool elevated{};
    if(!IsProcessElevated(elevated))return 1;
    if(!elevated){
        if(!RelaunchElevated(commandLine)&&GetLastError()!=ERROR_CANCELLED)
            MessageBoxW(nullptr,L"RDNA4 OC+ could not request administrator access.",L"RDNA4 OC+",MB_OK|MB_ICONERROR);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com)&&com!=RPC_E_CHANGED_MODE)return 1;
    const bool ownsCom=SUCCEEDED(com);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WindowProc;wc.hInstance=instance;
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));wc.hIconSm=wc.hIcon;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kClassName;
    if(!RegisterClassExW(&wc)){if(ownsCom)CoUninitialize();return 2;}
    const DWORD style=WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU;
    int result=3;
    {
        App app;
        HWND hwnd=CreateWindowExW(0,kClassName,kWindowTitle,style,CW_USEDEFAULT,CW_USEDEFAULT,1200,760,nullptr,nullptr,instance,&app);
        if(hwnd){
            const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
            SetWindowPos(hwnd,nullptr,0,0,MulDiv(1200,dpi,96),MulDiv(760,dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(hwnd,show);UpdateWindow(hwnd);
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
            result=static_cast<int>(msg.wParam);
        }
    }
    UnregisterClassW(kClassName,instance);if(ownsCom)CoUninitialize();return result;
}
