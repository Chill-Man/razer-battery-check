#include "battery.h"
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
using Microsoft::WRL::ComPtr;
using namespace pulse;
namespace {
constexpr UINT TrayMessage = WM_APP + 1, ReadingMessage = WM_APP + 2;
constexpr int Width = 320, Height = 344;
const wchar_t* ClassName = L"BasiliskPulse.Window";
const wchar_t* RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* SettingsKey = L"Software\\BasiliskPulse";
HWND window = nullptr;
UINT taskbarMessage;
NOTIFYICONDATAW tray = {};
HICON trayIcon = nullptr;
ComPtr<ID2D1Factory> d2d;
ComPtr<IDWriteFactory> dwrite;
ComPtr<ID2D1HwndRenderTarget> target;
ComPtr<ID2D1SolidColorBrush> brush;
ComPtr<ID2D1RadialGradientBrush> mouseGlow;
ComPtr<ID2D1RadialGradientBrush> mouseSoftGlow;
ComPtr<ID2D1Bitmap> mouseBitmap;
ComPtr<IWICBitmapSource> mousePixels;
ComPtr<ID2D1Bitmap> chargeBitmap;
std::wstring chargeBitmapText;
UINT chargeBitmapColor=0;
float chargeBitmapScale=0;
std::thread worker;
std::mutex workerMutex;
std::condition_variable wake;
bool stopping = false, refreshRequested = true;
std::atomic<int> interval{120};
TelemetryState telemetry;
Reading& current=telemetry.reading;
int64_t lastSuccess = 0;
Estimate estimate;
bool polling = true, settingsPage = false, inspectMode = false, alignmentGuides = false;
bool ambienceRunning=false;
ULONGLONG ambienceStart=0;
bool lowNotified = false;
float dpiScale = 1;
float reveal = 1;
ULONGLONG animationStart = 0, lastManualRefresh = 0;
int hovered = 0, focused = 0;
bool trackingMouse = false, animations = true;
double nominal = 110;
enum Action { Refresh = 1, Settings, Synapse, Exit, Startup, Motion, Interval, Less, More, Back, HoursFast, Hours60, HoursDefault };
struct ToggleTransition { float from=0,to=0;ULONGLONG started=0;bool initialized=false,active=false; };
ToggleTransition toggleTransitions[3];
float TogglePosition(ToggleTransition& state) {
    if(!state.active)return state.to;
    float t=std::min(1.0f,(GetTickCount64()-state.started)/240.0f);
    if(t>=1){state.active=false;return state.to;}
    return state.from+(state.to-state.from)*(1-std::pow(1-t,4.0f));
}
void FinishToggleAnimations() {
    KillTimer(window,3);
    for(auto& state:toggleTransitions){state.from=state.to;state.active=false;}
}
void AnimateToggle(int id,bool enabled,bool allowed) {
    auto& state=toggleTransitions[id-Startup];
    float position=state.initialized?TogglePosition(state):(enabled?0.0f:1.0f);
    state.initialized=true;state.from=position;state.to=enabled?1.0f:0.0f;
    state.started=GetTickCount64();state.active=allowed&&std::abs(state.to-state.from)>0.001f;
    if(state.active)SetTimer(window,3,15,nullptr);
}
UINT BlendColor(UINT from,UINT to,float t) {
    UINT result=0;for(int shift:{0,8,16}){
        float a=static_cast<float>((from>>shift)&255),b=static_cast<float>((to>>shift)&255);
        result|=static_cast<UINT>(std::lround(a+(b-a)*t))<<shift;
    }return result;
}
struct Hit { int id; D2D1_RECT_F rect; std::wstring label; };
std::vector<Hit> hits;
struct Update { Reading reading; Estimate prediction; };
const ModelInfo& CurrentModel() {
    auto model=ModelForPid(current.pid);return *(model?model:ModelForPid(0x00AA));
}

DWORD ReadSetting(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, bytes = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, SettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
    return value;
}
void WriteSetting(const wchar_t* name, DWORD value) {
    HKEY key; if (RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); RegCloseKey(key);
    }
}
std::wstring Executable() { wchar_t path[32768]; GetModuleFileNameW(nullptr, path, 32768); return path; }
bool Autostart() {
    wchar_t value[32768]; DWORD bytes=sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, L"BasiliskPulse", RRF_RT_REG_SZ, nullptr, value, &bytes)==ERROR_SUCCESS;
}
void ToggleAutostart() {
    HKEY key; if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (Autostart()) RegDeleteValueW(key, L"BasiliskPulse");
    else { auto command=L"\""+Executable()+L"\""; RegSetValueExW(key,L"BasiliskPulse",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))); }
    RegCloseKey(key);
}
bool MotionAllowed() {
    BOOL enabled=TRUE; HIGHCONTRASTW hc={sizeof(hc)};
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);
    SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);
    return animations && enabled && !(hc.dwFlags&HCF_HIGHCONTRASTON);
}
RECT AmbienceRect() {
    return {static_cast<LONG>(std::floor(210*dpiScale)),static_cast<LONG>(std::floor(44*dpiScale)),static_cast<LONG>(std::ceil(314*dpiScale)),static_cast<LONG>(std::ceil(181*dpiScale))};
}
void SyncAmbienceTimer() {
    bool desired=IsWindowVisible(window)&&!settingsPage&&current.percent>=0&&MotionAllowed();
    if(desired&&!ambienceRunning){ambienceStart=GetTickCount64();ambienceRunning=SetTimer(window,2,33,nullptr)!=0;}
    else if(!desired&&ambienceRunning){KillTimer(window,2);ambienceRunning=false;}
}
void RequestRefresh() {
    { std::lock_guard<std::mutex> lock(workerMutex); refreshRequested = true; }
    wake.notify_one();
}
void RunWorker(Reading initial) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    History history;auto dir=DataDirectory();std::wstring path;const ModelInfo* historyModel=nullptr;
    TelemetryState state;state.Restore(initial);
    if(state.reading.percent>=0){historyModel=ModelForPid(state.reading.pid);path=dir+L"\\"+historyModel->historyFile;history.Load(path);}
    unsigned nextDelay=0,retries=0;
    auto inputStamp=[](){LASTINPUTINFO info={sizeof(info)};GetLastInputInfo(&info);return info.dwTime;};
    DWORD lastInput=inputStamp();
    while (true) {
        {
            std::unique_lock<std::mutex> lock(workerMutex);
            ULONGLONG deadline=GetTickCount64()+nextDelay*1000ull;
            while(!stopping&&!refreshRequested&&GetTickCount64()<deadline){
                auto now=GetTickCount64();if(now>=deadline)break;
                auto remaining=deadline-now;
                wake.wait_for(lock,std::chrono::milliseconds(retries?std::min<ULONGLONG>(remaining,5000):remaining),[]{return stopping||refreshRequested;});
                // During recovery, new mouse/keyboard activity triggers a read within
                // five seconds, without raw-input hooks or a permanent UI timer.
                if(retries&&inputStamp()!=lastInput)break;
            }
            if (stopping) break;
            refreshRequested = false;
        }
        auto reading=ReadBattery();
        state.Apply(reading);
        if (reading.connected) {
            auto model=ModelForPid(reading.pid);
            if(model!=historyModel){history.samples.clear();path=dir+L"\\"+model->historyFile;history.Load(path);historyModel=model;}
            history.Add(reading);history.Save(path);
            SaveLastReading(dir+L"\\last-reading.tsv",reading);
        }
        auto prediction=history.Predict(state.reading.percent);
        retries=reading.connected&&reading.charging>=0?0:std::min(retries+1,10000u);
        nextDelay=RetryDelay(retries,interval.load());lastInput=inputStamp();
        {
            std::ofstream status{std::filesystem::path(dir+L"\\status.json.tmp")};
            status << "{\n  \"connected\": " << (reading.connected?"true":"false") << ",\n  \"percent\": " << state.reading.percent << ",\n  \"raw\": " << state.reading.raw << ",\n  \"pid\": " << state.reading.pid << ",\n  \"charging\": " << state.reading.charging << ",\n  \"unixTime\": " << state.reading.time << ",\n  \"attemptUnixTime\": " << reading.time << ",\n  \"consecutiveFailures\": " << state.failures << ",\n  \"retrySeconds\": " << nextDelay << ",\n  \"learned\": " << (prediction.learned?"true":"false") << "\n}\n";
        }
        MoveFileExW((dir+L"\\status.json.tmp").c_str(),(dir+L"\\status.json").c_str(),MOVEFILE_REPLACE_EXISTING);
        auto update = new Update{reading,prediction};
        if (!PostMessageW(window,ReadingMessage,0,reinterpret_cast<LPARAM>(update))) delete update;
    }
    CoUninitialize();
}
std::wstring RuntimeText() {
    if (current.percent < 0) return L"Нет данных";
    bool live=telemetry.LooksLive(Now());
    int charging=telemetry.ChargingState(Now());
    if(live&&charging==1)return L"Идёт зарядка";
    if (current.percent <= 1) return L"Пора зарядить";
    double hours=estimate.learned?estimate.hours:std::max(0,current.percent-1)*nominal/100.0;
    if(!estimate.learned)return L"≈ "+std::to_wstring(static_cast<int>(std::ceil(hours)))+L" ч";
    int minutes=std::max(5,static_cast<int>(std::round(hours*60/5))*5);
    auto h=minutes/60, m=minutes%60;
    std::wstring value=L"≈ "+std::to_wstring(h)+L" ч";
    if (m) value+=L" "+std::to_wstring(m)+L" мин";
    return value;
}
D2D1_COLOR_F Color(UINT rgb, float alpha=1) { return D2D1::ColorF(rgb,alpha); }
void Fill(D2D1_RECT_F rect, UINT color, float radius=0, float alpha=1) {
    brush->SetColor(Color(color,alpha));
    if (radius) target->FillRoundedRectangle(D2D1::RoundedRect(rect,radius,radius),brush.Get()); else target->FillRectangle(rect,brush.Get());
}
void Line(float x1,float y1,float x2,float y2,UINT color,float thickness=1) {
    brush->SetColor(Color(color)); target->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),brush.Get(),thickness);
}
void Text(const std::wstring& text,float x,float y,float width,float height,float size,UINT color=0xEFF2F0,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT alignment=DWRITE_TEXT_ALIGNMENT_LEADING) {
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite->CreateTextFormat(L"Segoe UI Variable",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ru-RU",&format))) return;
    format->SetTextAlignment(alignment);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    brush->SetColor(Color(color));
    target->DrawTextW(text.c_str(),static_cast<UINT32>(text.size()),format.Get(),D2D1::RectF(x,y,x+width,y+height),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Button(int id,D2D1_RECT_F rect,const std::wstring& label,bool accent=false) {
    hits.push_back({id,rect,label});
    bool hot=hovered==id||focused==id;
    Fill(rect,accent?(hot?0xB9FF94:0xA5F57A):(hot?0x303632:0x232824),12);
    if (focused==id) {brush->SetColor(Color(0xA5F57A));target->DrawRoundedRectangle(D2D1::RoundedRect(rect,12,12),brush.Get(),1);}
    Text(label,rect.left,rect.top+(rect.bottom-rect.top-18)/2,rect.right-rect.left,24,12,accent?0x10230C:0xDAE0DC,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_CENTER);
}
void Toggle(int id,float y,const std::wstring& label,bool enabled) {
    auto& state=toggleTransitions[id-Startup];
    if(!state.initialized||(!state.active&&state.to!=(enabled?1.0f:0.0f))){state.to=enabled?1.0f:0.0f;state.from=state.to;state.initialized=true;}
    float position=TogglePosition(state);
    D2D1_RECT_F row=D2D1::RectF(12,y,308,y+40);hits.push_back({id,row,label});
    if (hovered==id||focused==id) Fill(row,0x242A25,12);
    Text(label,20,y+10,235,22,13,0xF0F3F1,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Fill(D2D1::RectF(266,y+9,300,y+31),BlendColor(0x3A443D,0xA5F57A,position),11);
    brush->SetColor(Color(BlendColor(0xABB4AD,0x152611,position)));target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(277+12*position,y+20),8,8),brush.Get());
}
void LoadMouseBitmap() {
    // The small transparent PNG is embedded; decode once per visible panel.
    HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(2),RT_RCDATA);
    if(!resource)return;
    DWORD size=SizeofResource(nullptr,resource);
    auto bytes=static_cast<BYTE*>(LockResource(LoadResource(nullptr,resource)));
    if(!bytes||!size)return;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateStream(&stream))||
       FAILED(stream->InitializeFromMemory(bytes,size))||
       FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||
       FAILED(decoder->GetFrame(0,&frame))||
       FAILED(factory->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return;
    mousePixels=converter;
}
ComPtr<IWICBitmapSource> CropVisiblePixels(IWICImagingFactory* factory,IWICBitmapSource* source,UINT& width,UINT& height) {
    ComPtr<IWICBitmapSource> result;UINT sourceWidth=0,sourceHeight=0;
    if(FAILED(source->GetSize(&sourceWidth,&sourceHeight))||!sourceWidth||!sourceHeight)return result;
    UINT stride=sourceWidth*4;std::vector<BYTE> pixels(static_cast<size_t>(stride)*sourceHeight);
    if(FAILED(source->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data())))return result;
    int left=static_cast<int>(sourceWidth),top=static_cast<int>(sourceHeight),right=-1,bottom=-1;
    for(UINT y=0;y<sourceHeight;++y)for(UINT x=0;x<sourceWidth;++x)if(pixels[static_cast<size_t>(y)*stride+x*4+3]>8){
        left=std::min(left,static_cast<int>(x));right=std::max(right,static_cast<int>(x));
        top=std::min(top,static_cast<int>(y));bottom=std::max(bottom,static_cast<int>(y));
    }
    if(right<left)return result;
    width=static_cast<UINT>(right-left+1);height=static_cast<UINT>(bottom-top+1);
    WICRect rect={left,top,static_cast<INT>(width),static_cast<INT>(height)};ComPtr<IWICBitmapClipper> clipper;
    if(SUCCEEDED(factory->CreateBitmapClipper(&clipper))&&SUCCEEDED(clipper->Initialize(source,&rect)))result=clipper;
    return result;
}
void PrepareChargeBitmaps(const std::wstring& number,UINT color) {
    if(chargeBitmap&&number==chargeBitmapText&&color==chargeBitmapColor&&chargeBitmapScale==dpiScale)return;
    chargeBitmap.Reset();mouseBitmap.Reset();
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> canvas;
    ComPtr<ID2D1RenderTarget> raster;ComPtr<ID2D1SolidColorBrush> ink;ComPtr<IDWriteTextFormat> format;
    UINT width=static_cast<UINT>(std::ceil(190*dpiScale)),height=static_cast<UINT>(std::ceil(112*dpiScale));
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96*dpiScale,96*dpiScale);
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&canvas))||
       FAILED(d2d->CreateWicBitmapRenderTarget(canvas.Get(),properties,&raster))||
       FAILED(raster->CreateSolidColorBrush(Color(color),&ink))||
       FAILED(dwrite->CreateTextFormat(L"Segoe UI Variable",nullptr,DWRITE_FONT_WEIGHT_LIGHT,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,84,L"ru-RU",&format)))return;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    raster->BeginDraw();raster->Clear(D2D1::ColorF(0,0));raster->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    raster->DrawTextW(number.c_str(),static_cast<UINT32>(number.size()),format.Get(),D2D1::RectF(0,0,190,112),ink.Get());
    if(FAILED(raster->EndDraw()))return;
    UINT inkWidth=0,inkHeight=0;auto croppedNumber=CropVisiblePixels(factory.Get(),canvas.Get(),inkWidth,inkHeight);
    auto bitmapProperties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    if(!croppedNumber||FAILED(target->CreateBitmapFromWicBitmap(croppedNumber.Get(),&bitmapProperties,&chargeBitmap)))return;
    if(mousePixels){
        UINT sourceWidth=0,sourceHeight=0;mousePixels->GetSize(&sourceWidth,&sourceHeight);
        UINT mouseWidth=std::max(1u,static_cast<UINT>(std::lround(inkHeight*sourceWidth/static_cast<double>(sourceHeight))));
        ComPtr<IWICBitmapScaler> scaler;
        if(SUCCEEDED(factory->CreateBitmapScaler(&scaler))&&SUCCEEDED(scaler->Initialize(mousePixels.Get(),mouseWidth,inkHeight,WICBitmapInterpolationModeFant))){
            UINT croppedWidth=0,croppedHeight=0;auto croppedMouse=CropVisiblePixels(factory.Get(),scaler.Get(),croppedWidth,croppedHeight);
            if(croppedMouse)target->CreateBitmapFromWicBitmap(croppedMouse.Get(),&bitmapProperties,&mouseBitmap);
        }
    }
    chargeBitmapText=number;chargeBitmapColor=color;chargeBitmapScale=dpiScale;
}
void DrawMouseAmbience(float centerX,float top,float height) {
    if(!mouseBitmap||height<=0)return;
    if(!mouseGlow){
        D2D1_GRADIENT_STOP stops[]={
            {0,Color(0xE5F0EC,0.24f)},{0.25f,Color(0xD8E9DF,0.15f)},
            {0.55f,Color(0xBAD4C6,0.06f)},{0.85f,Color(0xA5C4B1,0.01f)},
            {1,Color(0xA5C4B1,0)}
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        if(SUCCEEDED(target->CreateGradientStopCollection(stops,5,D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,&collection)))
            target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(centerX,top+height/2),D2D1::Point2F(0,0),34,44),collection.Get(),&mouseGlow);
    }
    if(!mouseSoftGlow){
        D2D1_GRADIENT_STOP stops[]={
            {0,Color(0xB9DFC5,0.14f)},{0.3f,Color(0xA7D6B4,0.07f)},
            {0.65f,Color(0x8FBE9F,0.015f)},{1,Color(0x8FBE9F,0)}
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        if(SUCCEEDED(target->CreateGradientStopCollection(stops,4,D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,&collection)))
            target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(centerX,top+height/2),D2D1::Point2F(0,0),30,38),collection.Get(),&mouseSoftGlow);
    }
    float seconds=ambienceRunning?(GetTickCount64()-ambienceStart)/1000.0f:0;
    float breath=0.5f-0.5f*std::cos(seconds*0.698132f);
    float driftX=std::sin(seconds*0.31f),driftY=std::sin(seconds*0.23f);
    float centerY=top+height/2;
    float glowStrength=telemetry.LooksLive(Now())?1.0f:0.65f;
    if(mouseGlow){
        auto point=D2D1::Point2F(centerX-3+4*driftX,centerY-6+3*driftY);
        mouseGlow->SetCenter(point);
        mouseGlow->SetRadiusX(32+3*breath);mouseGlow->SetRadiusY(42+3*breath);
        mouseGlow->SetOpacity((0.72f+0.28f*breath)*glowStrength);
        target->FillEllipse(D2D1::Ellipse(point,40,53),mouseGlow.Get());
    }
    if(mouseSoftGlow){
        auto point=D2D1::Point2F(centerX+7-4*driftX,centerY+9+2*driftY);
        mouseSoftGlow->SetCenter(point);
        mouseSoftGlow->SetOpacity((0.58f-0.18f*breath)*glowStrength);
        target->FillEllipse(D2D1::Ellipse(point,39,49),mouseSoftGlow.Get());
    }
}
void DrawMouse(float centerX,float top,float height) {
    if(!mouseBitmap)return;
    DrawMouseAmbience(centerX,top,height);
    auto size=mouseBitmap->GetPixelSize();float width=std::round(height*size.width/size.height*dpiScale)/dpiScale;
    float left=std::round((centerX-width/2)*dpiScale)/dpiScale;
    target->DrawBitmap(mouseBitmap.Get(),D2D1::RectF(left,top,left+width,top+height),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
D2D1_RECT_F DrawChargeNumber(const std::wstring& number,UINT color) {
    PrepareChargeBitmaps(number,color);
    float left=std::round(20*dpiScale)/dpiScale,top=std::round(80*dpiScale)/dpiScale;
    if(!chargeBitmap)return D2D1::RectF(left,top,left,top);
    auto pixels=chargeBitmap->GetPixelSize();auto bounds=D2D1::RectF(left,top,left+pixels.width/dpiScale,top+pixels.height/dpiScale);
    target->DrawBitmap(chargeBitmap.Get(),bounds,1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    return bounds;
}
void Overview() {
    bool online=telemetry.LooksLive(Now());
    Text(CurrentModel().name,20,17,280,32,21,0xEFF3EF,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto number=current.percent<0?L"—":std::to_wstring(current.percent);
    auto numberInk=DrawChargeNumber(number,online?0xF5F8F5:0x88938B);
    if(current.percent>=0)Text(L"%",numberInk.right+10,numberInk.bottom-35,36,40,25,0xA0ACA3,DWRITE_FONT_WEIGHT_LIGHT);
    Text(!online?L"Последний заряд":telemetry.ChargingState(Now())==1?L"Заряжается":L"Заряд аккумулятора",20,155,188,21,11,0x98A59C);
    if(current.percent>=0)DrawMouse(261,numberInk.top,numberInk.bottom-numberInk.top);
    if(alignmentGuides&&current.percent>=0){
        Line(16,numberInk.top,302,numberInk.top,0xFF6878,0.75f);
        Line(16,numberInk.bottom,302,numberInk.bottom,0xFF6878,0.75f);
    }
    UINT chargeColor=current.percent<=15?0xFFB074:0xA5F57A;
    for(int i=0;i<40;++i) {float x=20+i*7.05f;bool lit=current.percent>=0&&i<static_cast<int>(current.percent*0.4f*reveal);Fill(D2D1::RectF(x,190,x+4,202),lit?(online?chargeColor:0x657467):0x303932,1.5f);}
    Fill(D2D1::RectF(12,216,308,266),0x202722,12);
    Text(L"ДО 1% ЗАРЯДА",24,221,272,16,9,0x96A59A,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(RuntimeText(),23,237,274,28,19,0xECF5E9,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Button(Refresh,D2D1::RectF(20,278,198,310),polling?L"Обновляется…":L"↻   Обновить",true);
    Button(Settings,D2D1::RectF(208,278,300,310),L"Настройки");
    hits.push_back({Synapse,D2D1::RectF(20,316,180,340),L"Открыть Razer Synapse"});
    hits.push_back({Exit,D2D1::RectF(240,316,300,340),L"Выйти"});
    Text(L"Открыть Synapse ↗",20,322,170,18,10,hovered==Synapse||focused==Synapse?0xA5F57A:0x87958C);
    Text(L"Выйти",240,322,60,18,10,hovered==Exit||focused==Exit?0xFFB074:0x87958C,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_TRAILING);
}
void SettingsView() {
    Button(Back,D2D1::RectF(16,16,78,48),L"‹ Назад");
    Text(L"Настройки",92,18,208,32,20,0xF1F5F2,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Toggle(Startup,70,L"Запуск с Windows",Autostart());
    Toggle(Motion,114,L"Плавные анимации",animations);
    Toggle(Interval,158,L"Экономный опрос",interval==300);
    Line(20,214,300,214,0x303932);
    Text(L"Ресурс полного заряда",20,228,280,24,14,0xECF3ED,DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Button(Less,D2D1::RectF(20,258,64,292),L"−");
    Text(std::to_wstring(static_cast<int>(nominal))+L" часов",72,260,176,34,21,0xA5F57A,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_CENTER);
    Button(More,D2D1::RectF(256,258,300,292),L"+");
    Button(HoursFast,D2D1::RectF(20,304,105,334),std::to_wstring(CurrentModel().fastPollingHours)+L" ч");
    Button(Hours60,D2D1::RectF(117,304,203,334),L"60 ч");
    Button(HoursDefault,D2D1::RectF(215,304,300,334),std::to_wstring(CurrentModel().nominalHours)+L" ч");
}
void DiscardGraphics() {mouseSoftGlow.Reset();mouseGlow.Reset();chargeBitmap.Reset();chargeBitmapText.clear();mouseBitmap.Reset();mousePixels.Reset();brush.Reset();target.Reset();}
void Paint() {
    PAINTSTRUCT ps;BeginPaint(window,&ps);
    if(!d2d)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf());
    if(!dwrite)DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    if(!d2d||!dwrite){EndPaint(window,&ps);return;}
    if(!target) {
        RECT rect;GetClientRect(window,&rect);
        HRESULT hr=d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(rect.right,rect.bottom),D2D1_PRESENT_OPTIONS_NONE),&target);
        if(SUCCEEDED(hr)){ target->SetDpi(96*dpiScale,96*dpiScale);target->CreateSolidColorBrush(Color(0xFFFFFF),&brush);LoadMouseBitmap(); }
    }
    if(target&&brush) {
        auto region=AmbienceRect();
        bool ambienceOnly=!settingsPage&&reveal>=1&&chargeBitmap&&current.percent>=0&&ps.rcPaint.left>=region.left&&ps.rcPaint.top>=region.top&&ps.rcPaint.right<=region.right&&ps.rcPaint.bottom<=region.bottom;
        target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());
        if(ambienceOnly)target->PushAxisAlignedClip(D2D1::RectF(region.left/dpiScale,region.top/dpiScale,region.right/dpiScale,region.bottom/dpiScale),D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(Color(0x161C18));
        target->SetTransform(D2D1::Matrix3x2F::Translation(0,(1-reveal)*14));
        for(int i=7;i>0;--i) {brush->SetColor(Color(0x76A862,0.006f));target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(276,100),i*22.0f,i*24.0f),brush.Get());}
        if(ambienceOnly){auto size=chargeBitmap->GetPixelSize();DrawMouse(261,std::round(80*dpiScale)/dpiScale,size.height/dpiScale);target->PopAxisAlignedClip();}
        else {hits.clear();if(settingsPage)SettingsView();else Overview();}
        if(target->EndDraw()==D2DERR_RECREATE_TARGET)DiscardGraphics();
    }
    EndPaint(window,&ps);
}
HICON CreateTrayIcon() {
    const int size=32;
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto old=SelectObject(dc,bitmap);memset(pixels,0,size*size*4);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,telemetry.LooksLive(Now())?(current.percent<=15?RGB(255,176,116):RGB(165,245,122)):RGB(160,174,163));
    HFONT font=CreateFontW(current.percent==100?-21:-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto oldFont=SelectObject(dc,font);RECT r={0,0,32,32};auto label=current.percent<0?L"—":std::to_wstring(current.percent);DrawTextW(dc,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    auto bytes=static_cast<unsigned char*>(pixels);for(int i=0;i<size*size;++i){auto p=bytes+i*4; p[3]=std::max(p[0],std::max(p[1],p[2]));}
    HBITMAP mask=CreateBitmap(size,size,1,1,nullptr);ICONINFO icon={TRUE,0,0,mask,bitmap};HICON result=CreateIconIndirect(&icon);
    SelectObject(dc,oldFont);SelectObject(dc,old);DeleteObject(font);DeleteObject(bitmap);DeleteObject(mask);DeleteDC(dc);return result;
}
void UpdateTray(bool add=false) {
    HICON next=CreateTrayIcon();tray.hIcon=next;
    auto tip=L"Basilisk Pulse · "+(current.percent<0?L"ожидание мыши":std::to_wstring(current.percent)+L"%")+L"\n"+RuntimeText();
    if(!current.connected&&current.percent>=0)tip+=L"\nПоследнее чтение: "+std::to_wstring(std::max<int64_t>(0,(Now()-current.time)/60))+L" мин назад · повтор";
    else if(current.connected&&current.charging<0)tip+=L"\nСтатус зарядки уточняется";
    wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
    Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&tray);
    if(add){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}
    if(trayIcon)DestroyIcon(trayIcon);trayIcon=next;
}
void HidePanel() {FinishToggleAnimations();KillTimer(window,1);KillTimer(window,2);ambienceRunning=false;ShowWindow(window,SW_HIDE);DiscardGraphics();dwrite.Reset();d2d.Reset();focused=0;hovered=0;}
void StartAnimation() {
    if(MotionAllowed()){animationStart=GetTickCount64();reveal=0;SetTimer(window,1,15,nullptr);}
    else reveal=1;
    SyncAmbienceTimer();
    InvalidateRect(window,nullptr,FALSE);
}
void ShowPanel() {
    if(IsWindowVisible(window)){HidePanel();return;}
    settingsPage=false;focused=0;hovered=0;
    NOTIFYICONIDENTIFIER id={sizeof(id),window,1,{}};RECT trayRect;
    POINT point;GetCursorPos(&point);
    if(SUCCEEDED(Shell_NotifyIconGetRect(&id,&trayRect))){point.x=(trayRect.left+trayRect.right)/2;point.y=trayRect.top;}
    HMONITOR monitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(monitor,&mi);
    SetWindowPos(window,HWND_TOPMOST,mi.rcWork.right-Width,mi.rcWork.bottom-Height,Width,Height,SWP_NOACTIVATE);
    dpiScale=GetDpiForWindow(window)/96.0f;
    int w=static_cast<int>(Width*dpiScale),h=static_cast<int>(Height*dpiScale),margin=static_cast<int>(12*dpiScale);
    int x=std::clamp(point.x-w/2,mi.rcWork.left+margin,std::max(mi.rcWork.left+margin,mi.rcWork.right-w-margin));
    int y=std::max(mi.rcWork.top+margin,mi.rcWork.bottom-h-margin);
    SetWindowPos(window,HWND_TOPMOST,x,y,w,h,SWP_SHOWWINDOW);SetForegroundWindow(window);StartAnimation();
    if(Now()-lastSuccess>5){polling=true;RequestRefresh();}
}
void DoAction(int id) {
    bool motionBefore=MotionAllowed();
    switch(id) {
    case Refresh:
        if(GetTickCount64()-lastManualRefresh>=5000){lastManualRefresh=GetTickCount64();polling=true;RequestRefresh();}break;
    case Settings:FinishToggleAnimations();settingsPage=true;focused=0;hovered=0;StartAnimation();break;
    case Back:FinishToggleAnimations();settingsPage=false;focused=0;hovered=0;StartAnimation();break;
    case Synapse: {
        auto outcome=ShellExecuteW(window,L"open",L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Razer\\Razer Synapse.lnk",nullptr,nullptr,SW_SHOWNORMAL);
        if(reinterpret_cast<INT_PTR>(outcome)<=32)MessageBoxW(window,L"Не удалось открыть ярлык Razer Synapse.",L"Basilisk Pulse",MB_OK|MB_ICONINFORMATION);
        break; }
    case Exit:DestroyWindow(window);return;
    case Startup:ToggleAutostart();AnimateToggle(id,Autostart(),motionBefore);break;
    case Motion:animations=!animations;WriteSetting(L"Animations",animations);AnimateToggle(id,animations,motionBefore||MotionAllowed());SyncAmbienceTimer();break;
    case Interval:interval=interval==120?300:120;WriteSetting(L"Interval",interval);AnimateToggle(id,interval==300,motionBefore);RequestRefresh();break;
    case Less:nominal=std::max(5.0,nominal-1);WriteSetting(CurrentModel().nominalSetting,static_cast<DWORD>(nominal));UpdateTray();break;
    case More:nominal=std::min(300.0,nominal+1);WriteSetting(CurrentModel().nominalSetting,static_cast<DWORD>(nominal));UpdateTray();break;
    case HoursFast:case Hours60:case HoursDefault:nominal=id==HoursFast?CurrentModel().fastPollingHours:id==Hours60?60:CurrentModel().nominalHours;WriteSetting(CurrentModel().nominalSetting,static_cast<DWORD>(nominal));UpdateTray();break;
    }
    InvalidateRect(window,nullptr,FALSE);
}
LRESULT CALLBACK WndProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==taskbarMessage&&taskbarMessage){UpdateTray(true);return 0;}
    switch(message) {
    case WM_PAINT:Paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:if(target)target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));return 0;
    case WM_DPICHANGED:dpiScale=HIWORD(wp)/96.0f;DiscardGraphics();return 0;
    case WM_ACTIVATE:if(!inspectMode&&LOWORD(wp)==WA_INACTIVE&&IsWindowVisible(hwnd))HidePanel();return 0;
    case WM_CLOSE:HidePanel();return 0;
    case WM_SETTINGCHANGE:if(!MotionAllowed()){FinishToggleAnimations();KillTimer(hwnd,1);reveal=1;InvalidateRect(hwnd,nullptr,FALSE);}SyncAmbienceTimer();return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND)RequestRefresh();return TRUE;
    case WM_DEVICECHANGE:RequestRefresh();return TRUE;
    case TrayMessage:
        if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT)ShowPanel();return 0;
    case WM_TIMER:
        if(wp==1){float t=std::min(1.0f,(GetTickCount64()-animationStart)/320.0f);reveal=1-std::pow(1-t,5.0f);if(t>=1)KillTimer(hwnd,1);InvalidateRect(hwnd,nullptr,FALSE);}
        else if(wp==2){if(!IsWindowVisible(hwnd)||settingsPage||!animations||current.percent<0)SyncAmbienceTimer();else {auto rect=AmbienceRect();InvalidateRect(hwnd,&rect,FALSE);}}
        else if(wp==3){
            bool active=false;for(auto& state:toggleTransitions){TogglePosition(state);active=active||state.active;}
            if(!settingsPage||!IsWindowVisible(hwnd))FinishToggleAnimations();
            else {InvalidateRect(hwnd,nullptr,FALSE);if(!active)KillTimer(hwnd,3);}
        }return 0;
    case WM_MOUSEMOVE:{
        if(!trackingMouse){TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);trackingMouse=true;}
        float x=GET_X_LPARAM(lp)/dpiScale,y=GET_Y_LPARAM(lp)/dpiScale;int hit=0;
        for(const auto& a:hits)if(x>=a.rect.left&&x<=a.rect.right&&y>=a.rect.top&&y<=a.rect.bottom)hit=a.id;
        if(hit!=hovered){hovered=hit;InvalidateRect(hwnd,nullptr,FALSE);}SetCursor(LoadCursorW(nullptr,hit?IDC_HAND:IDC_ARROW));return 0;}
    case WM_MOUSELEAVE:trackingMouse=false;hovered=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_LBUTTONUP:{float x=GET_X_LPARAM(lp)/dpiScale,y=GET_Y_LPARAM(lp)/dpiScale;for(const auto& a:hits)if(x>=a.rect.left&&x<=a.rect.right&&y>=a.rect.top&&y<=a.rect.bottom){DoAction(a.id);break;}return 0;}
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){if(settingsPage)DoAction(Back);else HidePanel();}
        else if(wp==VK_RETURN||wp==VK_SPACE){if(focused)DoAction(focused);}
        else if(wp==VK_F5)DoAction(Refresh);
        else if(wp==VK_TAB&&!hits.empty()){
            int index=-1;for(size_t i=0;i<hits.size();++i)if(hits[i].id==focused)index=static_cast<int>(i);
            bool back=(GetKeyState(VK_SHIFT)&0x8000)!=0;index=(index+(back?-1:1)+static_cast<int>(hits.size()))%static_cast<int>(hits.size());focused=hits[index].id;InvalidateRect(hwnd,nullptr,FALSE);
        }return 0;
    case ReadingMessage:{
        auto update=reinterpret_cast<Update*>(lp);polling=false;
        if(update->reading.connected){
            auto oldModel=ModelForPid(current.pid);auto newModel=ModelForPid(update->reading.pid);
            if(newModel!=oldModel)nominal=std::clamp<double>(ReadSetting(newModel->nominalSetting,newModel->nominalHours),5,300);
            telemetry.Apply(update->reading);lastSuccess=current.time;estimate=update->prediction;
        }
        else telemetry.Apply(update->reading);
        delete update;UpdateTray();SyncAmbienceTimer();
        if(current.connected&&current.charging==0&&current.percent<=15&&!lowNotified){
            tray.uFlags=NIF_INFO;wcscpy_s(tray.szInfoTitle,L"Basilisk: пора зарядить");auto info=L"Осталось "+std::to_wstring(current.percent)+L"% · "+RuntimeText();wcsncpy_s(tray.szInfo,info.c_str(),_TRUNCATE);tray.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&tray);lowNotified=true;
        }
        if(current.connected&&(current.charging==1||current.percent>20))lowNotified=false;
        if(IsWindowVisible(hwnd))InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_DESTROY:
        {std::lock_guard<std::mutex> lock(workerMutex);stopping=true;}wake.notify_one();
        FinishToggleAnimations();KillTimer(hwnd,1);KillTimer(hwnd,2);ambienceRunning=false;Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE,PWSTR command,int) {
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\BasiliskPulse.SingleInstance");
    if(GetLastError()==ERROR_ALREADY_EXISTS){auto existing=FindWindowW(ClassName,L"Basilisk Pulse");if(existing)PostMessageW(existing,TrayMessage,0,MAKELPARAM(NIN_SELECT,1));if(mutex)CloseHandle(mutex);return 0;}
    inspectMode=wcsstr(command,L"--inspect")!=nullptr;
    alignmentGuides=wcsstr(command,L"--alignment-guides")!=nullptr;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    animations=ReadSetting(L"Animations",1)!=0;interval=ReadSetting(L"Interval",120)==300?300:120;nominal=std::clamp<double>(ReadSetting(L"NominalHours",110),5,300);
    WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=WndProc;wc.hInstance=hInstance;wc.lpszClassName=ClassName;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(hInstance,MAKEINTRESOURCEW(1));RegisterClassExW(&wc);
    window=CreateWindowExW((inspectMode?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST,ClassName,L"Basilisk Pulse",WS_POPUP,0,0,Width,Height,nullptr,nullptr,hInstance,nullptr);
    if(!window)return 1;
    DWORD corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    MARGINS margins={1,1,1,1};DwmExtendFrameIntoClientArea(window,&margins);
    taskbarMessage=RegisterWindowMessageW(L"TaskbarCreated");tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uCallbackMessage=TrayMessage;UpdateTray(true);
    auto cached=LoadLastReading(DataDirectory()+L"\\last-reading.tsv");
    if(cached.percent<0){
        // Migrate existing installations from their genuine measurement history.
        for(uint16_t pid:{uint16_t(0x00AB),uint16_t(0x00CD)}){
            auto model=ModelForPid(pid);History previous;previous.Load(DataDirectory()+L"\\"+model->historyFile);
            if(!previous.samples.empty()&&previous.samples.back().time>cached.time){
                const auto& sample=previous.samples.back();cached.pid=pid;cached.raw=sample.raw;cached.time=sample.time;
                cached.percent=static_cast<int>(std::lround(sample.raw*100.0/255));cached.charging=sample.charging?1:0;
            }
        }
    }
    telemetry.Restore(cached);
    if(current.percent>=0){
        lastSuccess=current.time;auto& model=CurrentModel();nominal=std::clamp<double>(ReadSetting(model.nominalSetting,model.nominalHours),5,300);
        History previous;previous.Load(DataDirectory()+L"\\"+model.historyFile);estimate=previous.Predict(current.percent,nominal);
        UpdateTray();
    }
    worker=std::thread(RunWorker,current);
    if(wcsstr(command,L"--show")||inspectMode)ShowPanel();
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(worker.joinable())worker.join();
    while(PeekMessageW(&msg,nullptr,ReadingMessage,ReadingMessage,PM_REMOVE))delete reinterpret_cast<Update*>(msg.lParam);
    DiscardGraphics();dwrite.Reset();d2d.Reset();if(trayIcon)DestroyIcon(trayIcon);CoUninitialize();if(mutex)CloseHandle(mutex);return 0;
}
