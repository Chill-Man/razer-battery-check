#include "battery.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <iostream>
namespace pulse {
std::wstring DataDirectory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) return L".";
    std::wstring dir = std::wstring(path) + L"\\BasiliskPulse"; CoTaskMemFree(path);
    CreateDirectoryW(dir.c_str(), nullptr); return dir;
}
static bool Query(HANDLE handle, unsigned char command, int& value) {
    unsigned char request[91] = {};
    request[2] = 0x1f; request[6] = 2; request[7] = 7; request[8] = command;
    for (int i = 3; i <= 88; ++i) request[89] ^= request[i];
    // These two commands only request telemetry. No configuration commands are sent.
    for (int attempt = 0; attempt < 4; ++attempt) {
        if(HidD_SetFeature(handle,request,sizeof(request))){
            Sleep(40+attempt*10);
            for(int receive=0;receive<3;++receive){
                unsigned char response[91]={};
                if(HidD_GetFeature(handle,response,sizeof(response))&&ValidResponse(response,command)){value=response[10];return true;}
                // A busy/late response can finish without resending the request.
                Sleep(35+receive*15);
            }
        }
        Sleep(60+attempt*40);
    }
    return false;
}
Reading ReadBattery(bool diagnostic) {
    Reading result; result.time = Now();
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) { result.error = L"Не удалось получить список HID"; return result; }
    SP_DEVICE_INTERFACE_DATA iface = {}; iface.cbSize = sizeof(iface);
    // Only query whitelisted Basilisk Pro models; do not send telemetry to other products.
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, i, &iface); ++i) {
        DWORD bytes = 0; SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &bytes, nullptr);
        std::vector<unsigned char> storage(bytes);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data()); detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, bytes, nullptr, nullptr)) continue;
        std::wstring path = detail->DevicePath;
        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (path.find(L"vid_1532&pid_") == std::wstring::npos) continue;
        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes={};attributes.Size=sizeof(attributes);
        if(!HidD_GetAttributes(h,&attributes)||attributes.VendorID!=0x1532||!ModelForPid(attributes.ProductID)){CloseHandle(h);continue;}
        PHIDP_PREPARSED_DATA pp = nullptr; HIDP_CAPS caps = {};
        if (HidD_GetPreparsedData(h, &pp)) { HidP_GetCaps(pp, &caps); HidD_FreePreparsedData(pp); }
        if (diagnostic) std::wcout << path << L" usage=" << caps.UsagePage << L":" << caps.Usage << L" feature=" << caps.FeatureReportByteLength << L"\n";
        if (caps.UsagePage == 1 && caps.Usage == 2) {
            int raw = -1;
            if (Query(h, 0x80, raw)) {
                result.connected = true; result.raw = raw; result.percent = (int)std::lround(raw * 100.0 / 255.0);
                result.pid = attributes.ProductID;
                int charging = -1;
                if (Query(h, 0x84, charging) && charging <= 1) result.charging = charging;
                CloseHandle(h); break;
            }
            if (diagnostic) std::wcout << L"  Telemetry query failed, Win32=" << GetLastError() << L"\n";
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (!result.connected) result.error = L"Мышь спит, отключена или HID занят";
    return result;
}
}
