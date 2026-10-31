#pragma once
#define NOMINMAX
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace pulse {
struct ModelInfo {
    const wchar_t* name;
    const wchar_t* historyFile;
    const wchar_t* nominalSetting;
    int nominalHours;
    int fastPollingHours;
};
const ModelInfo* ModelForPid(uint16_t pid);
struct Reading {
    bool connected = false;
    int percent = -1;
    int raw = -1;
    int charging = -1; // Unknown is distinct from discharging.
    uint16_t pid = 0;
    int64_t time = 0;
    std::wstring error;
};
class TelemetryState {
public:
    Reading reading;
    unsigned failures=0;
    bool liveThisSession=false;
    void Restore(const Reading& cached);
    void Apply(const Reading& incoming);
    bool LooksLive(int64_t now) const;
    int ChargingState(int64_t now) const;
private:
    int knownCharging=-1;
    int64_t chargingTime=0;
};
unsigned RetryDelay(unsigned failures,unsigned normalInterval);
Reading LoadLastReading(const std::filesystem::path& path);
bool SaveLastReading(const std::filesystem::path& path,const Reading& reading);
int64_t Now();
std::wstring DataDirectory();
Reading ReadBattery(bool diagnostic = false);
bool ValidResponse(const unsigned char* data, unsigned char command);
struct Sample { int64_t time; int raw; bool charging; };
struct Estimate { double hours = 0; bool learned = false; double percentPerHour = 0; };
class History {
public:
    std::vector<Sample> samples;
    void Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;
    void Add(const Reading& reading);
    Estimate Predict(int percent, double nominalHours = 110) const;
};
}
