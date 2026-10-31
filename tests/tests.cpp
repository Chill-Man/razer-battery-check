#include "../src/battery.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <codecvt>
#include <locale>
static bool DeleteFileW(const wchar_t* path) { return std::filesystem::remove(std::filesystem::u8path(std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(path))); }
#endif
#include <fstream>
#include <filesystem>
static int failures = 0;
void Check(bool ok, const char* message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }
int main(int argc, char**) {
    if (argc > 1) { auto r = pulse::ReadBattery(true); std::cout << "connected=" << r.connected << " raw=" << r.raw << " percent=" << r.percent << " charging=" << r.charging << '\n'; return r.connected ? 0 : 1; }
    auto original=pulse::ModelForPid(0x00AA);auto updated=pulse::ModelForPid(0x00CC);
    Check(original&&original==pulse::ModelForPid(0x00AB),"original wired and receiver share one profile");
    Check(updated&&updated==pulse::ModelForPid(0x00CD)&&updated==pulse::ModelForPid(0x00D6)&&updated==pulse::ModelForPid(0x00D7),"35K wired, receiver and Phantom variants supported");
    Check(original&&updated&&original!=updated&&std::wstring(original->historyFile)!=updated->historyFile&&std::wstring(original->nominalSetting)!=updated->nominalSetting,"do not mix model histories or runtime preferences");
    Check(!pulse::ModelForPid(0x00CB)&&!pulse::ModelForPid(0x0555)&&!pulse::ModelForPid(0),"reject wired non-Pro Basilisk, headset and unknown device");
    Check(updated&&updated->nominalHours==140&&updated->fastPollingHours==28,"35K runtime profile uses 35K specifications");
    pulse::TelemetryState state;
    pulse::Reading good;good.connected=true;good.pid=0x00AB;good.time=pulse::Now();good.raw=243;good.percent=95;good.charging=0;
    state.Apply(good);
    pulse::Reading lost;lost.time=good.time+1;lost.error=L"temporary HID busy";
    state.Apply(lost);
    Check(state.reading.percent==95&&state.reading.pid==0x00AB&&state.reading.time==good.time,"one failed query preserves the last percent, model and timestamp");
    Check(!state.reading.connected&&state.LooksLive(good.time+2),"brief failure does not flicker the display or claim a successful query");
    state.Apply(lost);state.Apply(lost);
    Check(!state.LooksLive(good.time+10)&&state.reading.percent==95,"persistent failure marks the preserved reading as cached");
    auto recovered=good;recovered.raw=240;recovered.percent=94;recovered.time+=20;
    state.Apply(recovered);
    Check(state.failures==0&&state.reading.percent==94&&state.LooksLive(good.time+21),"new response recovers automatically and resets failures");
    auto partial=recovered;partial.charging=-1;partial.time+=5;partial.raw=239;
    state.Apply(partial);
    Check(state.reading.raw==239&&state.ChargingState(good.time+26)==0,"partial status failure preserves fresh battery data and recent charging state");
    Check(state.ChargingState(good.time+1000)==-1&&!state.LooksLive(good.time+1000),"stale charging state and stale connectivity expire");
    auto different=partial;different.pid=0x00CD;different.time+=1;
    state.Apply(different);
    Check(state.ChargingState(different.time)==-1,"do not reuse charging state from another model");
    Check(pulse::RetryDelay(1,300)==2&&pulse::RetryDelay(2,300)==5&&pulse::RetryDelay(3,300)==15&&pulse::RetryDelay(6,300)==300,"recovery retries quickly before backing off to economy interval");
    Check(pulse::RetryDelay(0,120)==120&&pulse::RetryDelay(6,120)==120,"healthy and prolonged idle polling honor configured interval");
    auto cachePath=pulse::DataDirectory()+L"\\test-last-reading-cache.tmp";
    Check(pulse::SaveLastReading(cachePath,good),"save a successful sample atomically");
    Check(!pulse::SaveLastReading(cachePath,lost),"a failed query cannot overwrite the persistent cache");
    auto cached=pulse::LoadLastReading(cachePath);pulse::TelemetryState restarted;restarted.Restore(cached);
    Check(restarted.reading.percent==95&&!restarted.LooksLive(good.time+5),"restart restores the percent without claiming a live connection");
    restarted.Apply(lost);Check(restarted.reading.percent==95&&!restarted.LooksLive(good.time+5),"offline restart retains the cached battery through repeated failures");
    auto empty=good;empty.raw=0;empty.percent=0;
    Check(pulse::SaveLastReading(cachePath,empty)&&pulse::LoadLastReading(cachePath).percent==0,"zero percent is a valid cached reading");
    {std::ofstream file{std::filesystem::path(cachePath)};file<<"broken cache";}
    Check(pulse::LoadLastReading(cachePath).percent==-1,"reject corrupted persistent data");
    DeleteFileW(cachePath.c_str());
    pulse::History h;
    Check(std::abs(h.Predict(51).hours - 55) < 0.01, "nominal estimate to 1 percent");
    Check(h.Predict(1).hours == 0, "1 percent threshold");
    Check(h.Predict(0).hours == 0, "no negative remaining time");
    for (int i=0;i<=60;++i) h.samples.push_back({100000+i*60, 230-i/5, false});
    auto e=h.Predict(85);
    Check(e.learned && e.hours > 15 && e.hours < 22, "learned quantized discharge regression");
    h.samples.back().charging=true;
    Check(!h.Predict(85).learned, "charging invalidates current segment");
    h.samples.back().charging=false;
    h.samples.push_back({110000,215,false});
    Check(!h.Predict(84).learned, "sleep gap does not train forecast");
    h.samples.push_back({110060,240,false});
    Check(!h.Predict(94).learned, "battery increase resets training");
    pulse::History flat;
    for(int i=0;i<100;++i) flat.samples.push_back({100000+i*60,200,false});
    Check(!flat.Predict(78).learned, "flat battery never divides by zero");
    pulse::Reading r; r.connected=true;r.raw=100;r.charging=-1;r.time=pulse::Now();
    flat.Add(r); Check(flat.samples.size()==100, "unknown charging status excluded");
    unsigned char b[91]={};b[1]=2;b[2]=0x1f;b[6]=2;b[7]=7;b[8]=0x80;b[10]=190;
    for(int i=3;i<=88;++i)b[89]^=b[i];
    Check(pulse::ValidResponse(b,0x80), "valid battery report");
    Check(!pulse::ValidResponse(b,0x84), "reject mismatched command");
    b[10]^=1; Check(!pulse::ValidResponse(b,0x80), "reject corrupt checksum");
    b[10]^=1;b[1]=1; Check(!pulse::ValidResponse(b,0x80), "reject busy report");
    auto path=pulse::DataDirectory()+L"\\test-history.tmp";
    h.Save(path); pulse::History loaded;loaded.Load(path);
    Check(loaded.samples.empty(), "discard ancient samples on load"); DeleteFileW(path.c_str());
    pulse::History recent;auto now=pulse::Now();
    recent.samples={{now-300,220,false},{now-120,219,false},{now,220,true}};
    recent.Save(path);loaded.samples.clear();loaded.Load(path);
    Check(loaded.samples.size()==3 && loaded.samples.back().charging && loaded.samples.front().raw==220,"persist recent readings and charging state");
    DeleteFileW(path.c_str());
    std::cout << (failures ? "FAILED" : "All tests passed") << '\n'; return failures ? 1:0;
}
