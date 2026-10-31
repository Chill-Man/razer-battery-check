#include "battery.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace pulse {
static bool ReplaceFile(const std::filesystem::path& from,const std::filesystem::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;std::filesystem::rename(from,to,error);return !error;
#endif
}
const ModelInfo* ModelForPid(uint16_t pid) {
    static const ModelInfo original{L"Basilisk V3 Pro",L"history.tsv",L"NominalHours",110,32};
    static const ModelInfo updated{L"Basilisk V3 Pro 35K",L"history-35k.tsv",L"NominalHours35K",140,28};
    switch(pid) {
    case 0x00AA:case 0x00AB:return &original;
    case 0x00CC:case 0x00CD:case 0x00D6:case 0x00D7:return &updated;
    default:return nullptr;
    }
}
int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
static bool ValidReading(const Reading& r) {
    return ModelForPid(r.pid)&&r.raw>=0&&r.raw<=255&&r.percent>=0&&r.percent<=100&&r.charging>=-1&&r.charging<=1&&r.time>0;
}
void TelemetryState::Restore(const Reading& cached) {
    if(!ValidReading(cached))return;
    reading=cached;reading.connected=false;failures=0;liveThisSession=false;
    knownCharging=cached.charging;chargingTime=cached.time;
}
void TelemetryState::Apply(const Reading& incoming) {
    if(incoming.connected&&ValidReading(incoming)){
        if(reading.pid!=incoming.pid){knownCharging=-1;chargingTime=0;}
        reading=incoming;failures=0;liveThisSession=true;
        if(incoming.charging>=0){knownCharging=incoming.charging;chargingTime=incoming.time;}
    } else {
        failures=std::min(failures+1,10000u);
        reading.connected=false;reading.error=incoming.error;
        // Keep the successful sample, its model and its original timestamp.
    }
}
bool TelemetryState::LooksLive(int64_t now) const {
    return ValidReading(reading)&&now>=reading.time&&now-reading.time<=600&&(reading.connected||(liveThisSession&&failures<3));
}
int TelemetryState::ChargingState(int64_t now) const {
    return now>=chargingTime&&now-chargingTime<=600?knownCharging:-1;
}
unsigned RetryDelay(unsigned failures,unsigned normalInterval) {
    static const unsigned seconds[]={2,5,15,30,60};
    if(!failures||failures>5)return normalInterval;
    return std::min(seconds[failures-1],normalInterval);
}
Reading LoadLastReading(const std::filesystem::path& path) {
    Reading r;unsigned version=0,pid=0;
    std::ifstream file{std::filesystem::path(path)};
    if(!(file>>version>>r.time>>pid>>r.raw>>r.charging)||version!=1||pid>65535)return Reading{};
    r.pid=static_cast<uint16_t>(pid);r.percent=static_cast<int>(std::lround(r.raw*100.0/255));
    if(!ValidReading(r)||r.time>Now()+60||r.time<Now()-30*86400)return Reading{};
    r.connected=false;return r;
}
bool SaveLastReading(const std::filesystem::path& path,const Reading& r) {
    if(!r.connected||!ValidReading(r))return false;
    auto temp=path;temp+=L".tmp";
    {std::ofstream file{std::filesystem::path(temp)};file<<1<<' '<<r.time<<' '<<r.pid<<' '<<r.raw<<' '<<r.charging<<'\n';if(!file)return false;}
    return ReplaceFile(temp,path);
}
bool ValidResponse(const unsigned char* b, unsigned char command) {
    unsigned char crc = 0; for (int i = 3; i <= 88; ++i) crc ^= b[i];
    return b[0] == 0 && b[1] == 2 && b[2] == 0x1f && b[3] == 0 && b[4] == 0 && b[5] == 0 && b[6] == 2 && b[7] == 7 && b[8] == command && b[89] == crc;
}
void History::Load(const std::filesystem::path& path) {
    std::ifstream file{std::filesystem::path(path)}; Sample s;
    while (file >> s.time >> s.raw >> s.charging) if (s.raw >= 0 && s.raw <= 255 && s.time > Now() - 30 * 86400 && s.time <= Now()) samples.push_back(s);
    std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) { return a.time < b.time; });
    if (samples.size() > 15000) samples.erase(samples.begin(), samples.end() - 15000);
}
void History::Save(const std::filesystem::path& path) const {
    auto temp=path;temp+=L".tmp";
    { std::ofstream file{std::filesystem::path(temp)}; for (const auto& s : samples) file << s.time << ' ' << s.raw << ' ' << s.charging << '\n'; if (!file) return; }
    ReplaceFile(temp,path);
}
void History::Add(const Reading& r) {
    if (!r.connected || r.raw < 0 || r.charging < 0) return;
    if (!samples.empty() && r.time <= samples.back().time) return;
    samples.push_back({r.time, r.raw, r.charging == 1});
    auto cutoff = r.time - 30 * 86400;
    samples.erase(std::remove_if(samples.begin(), samples.end(), [cutoff](const Sample& s) { return s.time < cutoff; }), samples.end());
    if (samples.size() > 15000) samples.erase(samples.begin(), samples.end() - 15000);
}
Estimate History::Predict(int percent, double nominalHours) const {
    Estimate e; e.hours = std::max(0, percent - 1) / 100.0 * nominalHours;
    // Use a continuous, recent discharging segment. Gaps (sleep, app closed,
    // failed queries) and any increase/charge break it; avoid misleading slopes.
    if (samples.size() < 4 || samples.back().charging) return e;
    size_t start = samples.size() - 1;
    while (start > 0) {
        const auto& a = samples[start - 1]; const auto& b = samples[start];
        if (a.charging || b.time - a.time > 600 || a.raw < b.raw || samples.back().time - a.time > 24 * 3600) break;
        --start;
    }
    const auto& first = samples[start]; const auto& last = samples.back();
    double span = (last.time - first.time) / 3600.0;
    if (span < 0.5 || first.raw - last.raw < 8 || samples.size() - start < 4) return e;
    // Regression over all samples tolerates the quantized battery gauge.
    double sx=0, sy=0, sxx=0, sxy=0, n=0;
    for (size_t i=start; i<samples.size(); ++i) { double x=(samples[i].time-first.time)/3600.0, y=samples[i].raw*100.0/255.0; sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;++n; }
    double denominator = n*sxx-sx*sx;
    if (denominator <= 0) return e;
    double drain = -(n*sxy-sx*sy)/denominator;
    if (drain < 0.05 || drain > 50) return e;
    e.learned = true; e.percentPerHour = drain; e.hours = std::max(0, percent - 1) / drain;
    return e;
}
}
