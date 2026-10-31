#include "battery.h"
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <codecvt>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <locale>
#include <thread>
namespace pulse {
std::wstring DataDirectory() {
    const char* xdg=std::getenv("XDG_DATA_HOME");const char* home=std::getenv("HOME");
    auto dir=std::filesystem::path(xdg&&*xdg?xdg:(home?std::string(home)+"/.local/share":"."))/"BasiliskPulse";
    std::error_code error;std::filesystem::create_directories(dir,error);
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(dir.u8string());
}
static bool Query(int fd,unsigned char command,int& value) {
    unsigned char request[91]={};request[2]=0x1f;request[6]=2;request[7]=7;request[8]=command;
    for(int i=3;i<=88;++i)request[89]^=request[i];
    for(int attempt=0;attempt<3;++attempt){
        if(ioctl(fd,HIDIOCSFEATURE(sizeof(request)),request)>=0){
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            for(int receive=0;receive<3;++receive){
                unsigned char response[91]={};int count=ioctl(fd,HIDIOCGFEATURE(sizeof(response)),response);
                // Unnumbered hidraw replies may omit the report-ID prefix.
                if(count==90){for(int i=90;i>0;--i)response[i]=response[i-1];response[0]=0;}
                if(count>=90&&ValidResponse(response,command)){value=response[10];return true;}
                std::this_thread::sleep_for(std::chrono::milliseconds(45));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(70+attempt*40));
    }
    return false;
}
Reading ReadBattery(bool diagnostic) {
    Reading r;r.time=Now();bool denied=false;
    std::error_code error;
    std::filesystem::directory_iterator end;
    for(std::filesystem::directory_iterator iterator("/sys/class/hidraw",error);!error&&iterator!=end;iterator.increment(error)){
        const auto entry=*iterator;
        auto device=std::filesystem::canonical(entry.path()/"device",error);if(error){error.clear();continue;}
        std::ifstream interfaceFile(device.parent_path()/"bInterfaceNumber");std::string interfaceNumber;interfaceFile>>interfaceNumber;
        if(interfaceNumber!="00")continue;
        std::ifstream identity(device/"uevent");std::string line;unsigned bus=0,vendor=0,product=0;
        while(std::getline(identity,line))if(line.rfind("HID_ID=",0)==0)std::sscanf(line.c_str(),"HID_ID=%x:%x:%x",&bus,&vendor,&product);
        if(bus!=BUS_USB||vendor!=0x1532||!ModelForPid(static_cast<uint16_t>(product)))continue;
        auto node=std::filesystem::path("/dev")/entry.path().filename();int fd=open(node.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);
        if(fd<0){if(errno==EACCES||errno==EPERM)denied=true;if(diagnostic)std::cerr<<node<<": open failed ("<<errno<<")\n";continue;}
        hidraw_devinfo info={};
        if(ioctl(fd,HIDIOCGRAWINFO,&info)<0||info.vendor!=0x1532||!ModelForPid(static_cast<uint16_t>(info.product))){close(fd);continue;}
        if(diagnostic)std::cout<<node<<" pid="<<std::hex<<info.product<<std::dec<<" interface=00\n";
        int raw=-1;if(Query(fd,0x80,raw)){
            r.connected=true;r.pid=static_cast<uint16_t>(info.product);r.raw=raw;r.percent=static_cast<int>(std::lround(raw*100.0/255));
            int charging=-1;if(Query(fd,0x84,charging)&&charging<=1)r.charging=charging;
            close(fd);break;
        }
        close(fd);
    }
    if(!r.connected)r.error=denied?L"HID permission denied: install the bundled udev rule and reconnect the receiver.":L"Mouse asleep, disconnected, or HID interface busy.";
    return r;
}
}
