#pragma once
#import <IOKit/hid/IOHIDManager.h>
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include "ps_usb_report.h"

namespace PSUSBMode {
// Called only on PSAudio's serial device queue, never a render/input thread.
static IOHIDManagerRef manager;
static IOHIDDeviceRef device;
static PSUSBAudioModeReport report;
static std::atomic<bool> accepted{false};
static std::atomic<uint64_t> writes{0},failures{0};
static IOReturn previousStatus=kIOReturnNotOpen;
static double retryAt;
static bool powerInitialized;
static void Close(void) {
    accepted=false;report={};powerInitialized=false;
    if(device) {IOHIDDeviceClose(device,kIOHIDOptionsTypeNone);CFRelease(device);device=nullptr;}
    if(manager) {IOHIDManagerClose(manager,kIOHIDOptionsTypeNone);CFRelease(manager);manager=nullptr;}
}
static uint32_t Number(IOHIDDeviceRef d,CFStringRef name) {
    CFTypeRef value=IOHIDDeviceGetProperty(d,name);int32_t number=0;
    if(value&&CFGetTypeID(value)==CFNumberGetTypeID())CFNumberGetValue((CFNumberRef)value,kCFNumberSInt32Type,&number);
    return (uint32_t)number;
}
static IOReturn Open(void) {
    if(device)return kIOReturnSuccess;
    Close();
    manager=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
    if(!manager)return kIOReturnNoMemory;
    NSMutableArray *matches=[NSMutableArray array];
    for(unsigned product:{0x0ce6u,0x0df2u})[matches addObject:@{
        (__bridge NSString *)CFSTR(kIOHIDVendorIDKey):@0x054c,
        (__bridge NSString *)CFSTR(kIOHIDProductIDKey):@(product),
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsagePageKey):@1,
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsageKey):@5,
        (__bridge NSString *)CFSTR(kIOHIDTransportKey):@"USB"}];
    IOHIDManagerSetDeviceMatchingMultiple(manager,(__bridge CFArrayRef)matches);
    IOReturn status=IOHIDManagerOpen(manager,kIOHIDOptionsTypeNone);
    if(status)return status;
    CFSetRef found=IOHIDManagerCopyDevices(manager);
    CFIndex count=found?CFSetGetCount(found):0;
    if(count!=1) {if(found)CFRelease(found);return kIOReturnNotFound;}
    IOHIDDeviceRef candidate=nullptr;CFSetGetValues(found,(const void **)&candidate);
    // Repeat the identity checks on the selected handle before any write.
    uint32_t vendor=Number(candidate,CFSTR(kIOHIDVendorIDKey)),product=Number(candidate,CFSTR(kIOHIDProductIDKey));
    CFTypeRef transport=IOHIDDeviceGetProperty(candidate,CFSTR(kIOHIDTransportKey));
    bool identity=vendor==0x054c&&(product==0x0ce6||product==0x0df2)&&transport&&
        CFGetTypeID(transport)==CFStringGetTypeID()&&CFEqual(transport,CFSTR("USB"));
    CFTypeRef descriptor=IOHIDDeviceGetProperty(candidate,CFSTR(kIOHIDReportDescriptorKey));
    size_t size=descriptor&&CFGetTypeID(descriptor)==CFDataGetTypeID()?
        PSUSBOutputReportSize(CFDataGetBytePtr((CFDataRef)descriptor),CFDataGetLength((CFDataRef)descriptor),2):0;
    if(!identity||!report.build(size)) {
        Log(@"ps_audio usb_mode_descriptor accepted=0 vendor=0x%x product=0x%x report_bytes=%lu",vendor,product,size);
        CFRelease(found);return kIOReturnUnsupported;
    }
    status=IOHIDDeviceOpen(candidate,kIOHIDOptionsTypeNone);
    if(!status) {device=candidate;CFRetain(device);retryAt=0;}
    CFRelease(found);
    Log(@"ps_audio usb_mode_open status=0x%x product=0x%x report_bytes=%lu exclusive=0 input_handlers_unchanged=1",status,product,size);
    return status;
}
static bool ResumeAudio(void) {
    // A poll during backoff is not an attempt. Keep the first retry deadline
    // even when the audio device is polled more often than the retry interval.
    if(CACurrentMediaTime()<retryAt)return false;
    IOReturn status=Open();
    bool initialize=!powerInitialized;
    if(!status&&initialize) {
        // USB input status[1] is byte 54 (Linux hid-playstation); bit 2 is
        // the firmware's current microphone-mute state. Never unmute an
        // unknown microphone merely to enable grip feedback.
        std::array<uint8_t,64> input{};input[0]=1;CFIndex size=input.size();
        IOReturn read=IOHIDDeviceGetReport(device,kIOHIDReportTypeInput,1,input.data(),&size);
        bool known=!read&&size>=55&&input[0]==1;
        bool muted=!known||(input[54]&4)!=0;
        report.build(report.length,true,muted);
        Log(@"ps_audio usb_haptic_unmute initialization=1 mic_state_known=%d microphone_muted=%d input_status=0x%x recording_started=0",known,muted,read);
    } else if(!status)report.build(report.length,false);
    if(!status)status=IOHIDDeviceSetReport(device,kIOHIDReportTypeOutput,2,report.bytes.data(),report.length);
    if(!status)++writes;else ++failures;
    if(!status&&initialize)powerInitialized=true;
    accepted=status==kIOReturnSuccess;
    if(status!=previousStatus||(!status&&writes.load()==1)||(status&&failures.load()==1))
        Log(@"ps_audio usb_audio_mode status=0x%x api_accepted=%d writes=%llu failures=%llu rumble_select=0 triggers_leds_volume_untouched=1 physical_feel_unverified=1",
            status,accepted.load(),writes.load(),failures.load());
    previousStatus=status;
    if(status) {Close();retryAt=CACurrentMediaTime()+3;}
    return status==kIOReturnSuccess;
}
}
