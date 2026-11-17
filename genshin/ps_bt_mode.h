#pragma once
#import <IOKit/hid/IOHIDManager.h>
#import <QuartzCore/QuartzCore.h>
#include "ps_bt_report.h"
#include "ps_usb_report.h"

namespace PSBTMode {
// All calls belong to PSAudio's serial device queue. The shared HID handle
// does not seize the pad, replace input callbacks, or select a system device.
static IOHIDManagerRef manager;
static IOHIDDeviceRef device;
static uint8_t counter;
static double retryAt;
static std::atomic<bool> accepted{false};
static std::atomic<uint64_t> writes{0},failures{0};
static IOReturn previousStatus=kIOReturnNotOpen;

static void Close(bool sendSilence=true) {
    if(device) {
        if(sendSilence&&accepted.load()) {
            auto zero=PSBluetooth::MakeReport({},counter++);
            IOHIDDeviceSetReport(device,kIOHIDReportTypeOutput,0x32,zero.data(),zero.size());
        }
        IOHIDDeviceClose(device,kIOHIDOptionsTypeNone);CFRelease(device);device=nullptr;
    }
    if(manager) {IOHIDManagerClose(manager,kIOHIDOptionsTypeNone);CFRelease(manager);manager=nullptr;}
    accepted=false;counter=0;
}
static uint32_t Number(IOHIDDeviceRef d,CFStringRef name) {
    CFTypeRef value=IOHIDDeviceGetProperty(d,name);int32_t number=0;
    if(value&&CFGetTypeID(value)==CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)value,kCFNumberSInt32Type,&number);
    return uint32_t(number);
}
static bool Identity(IOHIDDeviceRef d) {
    uint32_t product=Number(d,CFSTR(kIOHIDProductIDKey));
    CFTypeRef transport=IOHIDDeviceGetProperty(d,CFSTR(kIOHIDTransportKey));
    return Number(d,CFSTR(kIOHIDVendorIDKey))==0x054c&&(product==0x0ce6||product==0x0df2)&&
        Number(d,CFSTR(kIOHIDPrimaryUsagePageKey))==1&&Number(d,CFSTR(kIOHIDPrimaryUsageKey))==5&&
        transport&&CFGetTypeID(transport)==CFStringGetTypeID()&&CFEqual(transport,CFSTR("Bluetooth"));
}
static bool Open(void) {
    if(device)return true;
    if(CACurrentMediaTime()<retryAt)return false;
    Close();retryAt=CACurrentMediaTime()+3;
    manager=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
    if(!manager)return false;
    NSMutableArray *matches=[NSMutableArray array];
    for(unsigned product:{0x0ce6u,0x0df2u})[matches addObject:@{
        (__bridge NSString *)CFSTR(kIOHIDVendorIDKey):@0x054c,
        (__bridge NSString *)CFSTR(kIOHIDProductIDKey):@(product),
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsagePageKey):@1,
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsageKey):@5,
        (__bridge NSString *)CFSTR(kIOHIDTransportKey):@"Bluetooth"}];
    IOHIDManagerSetDeviceMatchingMultiple(manager,(__bridge CFArrayRef)matches);
    IOReturn status=IOHIDManagerOpen(manager,kIOHIDOptionsTypeNone);
    if(status) {Close(false);return false;}
    CFSetRef found=IOHIDManagerCopyDevices(manager);
    CFIndex count=found?CFSetGetCount(found):0;
    if(count!=1) {if(found)CFRelease(found);Close(false);return false;}
    IOHIDDeviceRef candidate=nullptr;CFSetGetValues(found,(const void **)&candidate);
    CFTypeRef descriptor=IOHIDDeviceGetProperty(candidate,CFSTR(kIOHIDReportDescriptorKey));
    size_t bytes=descriptor&&CFGetTypeID(descriptor)==CFDataGetTypeID()?
        PSUSBOutputReportSize(CFDataGetBytePtr((CFDataRef)descriptor),CFDataGetLength((CFDataRef)descriptor),0x32):0;
    if(!Identity(candidate)||bytes!=PSBluetooth::reportBytes) {
        Log(@"ps_bt descriptor_ready=0 report_bytes=%lu expected=%lu physical_output_unverified=1",bytes,PSBluetooth::reportBytes);
        CFRelease(found);Close(false);return false;
    }
    status=IOHIDDeviceOpen(candidate,kIOHIDOptionsTypeNone);
    if(!status) {device=candidate;CFRetain(device);retryAt=0;}
    CFRelease(found);
    Log(@"ps_bt shared_open status=0x%x report_bytes=%lu exclusive=0 input_callbacks_unchanged=1",status,bytes);
    if(status)Close(false);
    return status==kIOReturnSuccess;
}
static bool Send(const PSBluetooth::Samples &pcm) {
    if(!device)return false;
    auto report=PSBluetooth::MakeReport(pcm,counter++);
    IOReturn status=IOHIDDeviceSetReport(device,kIOHIDReportTypeOutput,0x32,report.data(),report.size());
    if(!status)++writes;else ++failures;
    accepted=status==kIOReturnSuccess;
    if(status!=previousStatus||writes.load()==1||failures.load()==1)
        Log(@"ps_bt packet status=0x%x api_accepted=%d writes=%llu failures=%llu rate=3000 channels=2 signed_bits=8 physical_feel_unverified=1",
            status,accepted.load(),writes.load(),failures.load());
    previousStatus=status;
    if(status) {Close(false);retryAt=CACurrentMediaTime()+3;}
    return status==kIOReturnSuccess;
}
}
