#import <Foundation/Foundation.h>
#include <cassert>
#include <vector>
#include <random>
static void Log(NSString *,...) {}
#include "../genshin/ps_usb_mode.h"
int main() {@autoreleasepool {
    // Sony's public USB report shape: ID 2 has 47 output bytes; the input
    // and feature items in the same descriptor must not change that length.
    const uint8_t descriptor[]={0x85,1,0x75,8,0x95,63,0x81,2,
        0x85,2,0x75,8,0x95,47,0x91,2,0x95,63,0xb1,2};
    assert(PSUSBOutputReportSize(descriptor,sizeof(descriptor),2)==48);
    const uint8_t stacked[]={0x85,2,0x75,8,0x95,47,0xa4,0x85,3,0x95,63,0x91,2,0xb4,0x91,2};
    assert(PSUSBOutputReportSize(stacked,sizeof(stacked),2)==48);
    assert(PSUSBOutputReportSize(stacked,sizeof(stacked),3)==64);
    const uint8_t longItem[]={0xfe,2,7,0xaa,0xbb,0x85,2,0x75,8,0x95,47,0x91,2};
    assert(PSUSBOutputReportSize(longItem,sizeof(longItem),2)==48);
    const uint8_t overflow[]={0x85,2,0x77,0xff,0xff,0xff,0xff,0x97,0xff,0xff,0xff,0xff,0x91,2};
    assert(!PSUSBOutputReportSize(overflow,sizeof(overflow),2));
    const uint8_t pop[]={0xb4};assert(!PSUSBOutputReportSize(pop,sizeof(pop),2));
    for(size_t length=0;length<sizeof(descriptor);++length)PSUSBOutputReportSize(descriptor,length,2);
    std::mt19937 random(42);std::array<uint8_t,256> fuzz;
    for(unsigned i=0;i<10000;++i) {
        for(auto &b:fuzz)b=(uint8_t)random();
        PSUSBOutputReportSize(fuzz.data(),random()%fuzz.size(),2);
    }
    PSUSBAudioModeReport report;
    for(unsigned size:{48u,64u}) {
        assert(report.build(size)&&report.length==size&&report.bytes[0]==2);
        for(unsigned i=1;i<report.bytes.size();++i)assert(report.bytes[i]==0);
        assert((report.bytes[1]&3)==0&&report.bytes[2]==0);
    }
    for(unsigned size:{0u,47u,49u,63u,65u,78u})assert(!report.build(size)&&!report.length);
    assert(report.build(48,true,true)&&report.bytes[2]==2&&report.bytes[10]==0x10);
    assert(report.build(48,true,false)&&report.bytes[2]==2&&report.bytes[10]==0);
    for(unsigned i=1;i<48;++i)if(i!=2)assert(report.bytes[i]==0);
    assert(report.build(48,false)&&report.bytes[2]==0&&report.bytes[10]==0);
    IOReturn status=PSUSBMode::Open();
    assert(PSUSBMode::writes==0); // Enumeration/open only, never a test rumble.
    if(status) {
        PSUSBMode::retryAt=0;assert(!PSUSBMode::ResumeAudio());assert(PSUSBMode::writes==0&&!PSUSBMode::accepted);
    }
    PSUSBMode::Close();assert(!PSUSBMode::device&&!PSUSBMode::manager&&!PSUSBMode::accepted);
    printf("USB mode: descriptor sizes, Push/Pop, malformed/fuzzed reports, audio-mode-only packet, untouched other controls and real no-write HID enumeration passed; open_status=0x%x no physical playback\n",status);
}}
