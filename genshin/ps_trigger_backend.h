#pragma once
#include "ps_trigger_report.h"
#include "ps_system_trigger.h"
namespace PSTriggerBackend {
static dispatch_queue_t queue;
static IOHIDManagerRef manager;static IOHIDDeviceRef device;
static bool bluetooth,owned[2]{};static size_t reportLength;
static NativeTrigger::Pair sent{};
static std::atomic<bool> capable{false},allowed{false},systemBackend{false};
static std::atomic<uint64_t> generation{0},selection{0},writes{0},failures{0};
static GCController *__strong selected;
static double retryAt,nextSelectionAt;
static uint32_t Number(IOHIDDeviceRef d,CFStringRef key) {
    CFTypeRef value=IOHIDDeviceGetProperty(d,key);int32_t n=0;
    if(value&&CFGetTypeID(value)==CFNumberGetTypeID())CFNumberGetValue((CFNumberRef)value,kCFNumberSInt32Type,&n);
    return uint32_t(n);
}
static bool Write(unsigned mask,const NativeTrigger::Pair &effects) {
    if(!device||!mask)return false;
    auto p=PSTriggerReport::Make(bluetooth,reportLength,mask,effects);
    IOReturn status=p.length?IOHIDDeviceSetReport(device,kIOHIDReportTypeOutput,p.bytes[0],p.bytes.data(),p.length):kIOReturnBadArgument;
    if(status)++failures;else ++writes;
    if((!status&&writes.load()<=2)||(status&&failures.load()<=2))Log(@"native_trigger output status=0x%x transport=%s writes=%llu failures=%llu physical_feedback_verified=0",status,bluetooth?"bluetooth":"usb",writes.load(),failures.load());
    return status==kIOReturnSuccess;
}
static void Release() {
    unsigned mask=(owned[0]?1:0)|(owned[1]?2:0);
    if(mask)Write(mask,{});owned[0]=owned[1]=false;sent={};
}
static void Close() {
    if(!systemBackend.load())capable=false;
    Release();
    if(device){IOHIDDeviceClose(device,kIOHIDOptionsTypeNone);CFRelease(device);device=nullptr;}
    if(manager){IOHIDManagerClose(manager,kIOHIDOptionsTypeNone);CFRelease(manager);manager=nullptr;}
}
static void Open() {
    if(device||CACurrentMediaTime()<retryAt)return;
    retryAt=CACurrentMediaTime()+3;
    manager=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);if(!manager)return;
    NSMutableArray *matches=[NSMutableArray array];
    for(unsigned product:{0x0ce6u,0x0df2u})[matches addObject:@{
        (__bridge NSString *)CFSTR(kIOHIDVendorIDKey):@0x054c,
        (__bridge NSString *)CFSTR(kIOHIDProductIDKey):@(product),
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsagePageKey):@1,
        (__bridge NSString *)CFSTR(kIOHIDPrimaryUsageKey):@5}];
    IOHIDManagerSetDeviceMatchingMultiple(manager,(__bridge CFArrayRef)matches);
    IOReturn status=IOHIDManagerOpen(manager,kIOHIDOptionsTypeNone);
    if(status){Close();return;}
    CFSetRef found=IOHIDManagerCopyDevices(manager);
    CFIndex count=found?CFSetGetCount(found):0;
    // A selected GC controller plus exactly one Sony HID endpoint prevents
    // sending effects to another controller, including a USB/BT duplicate.
    if(count!=1){if(found)CFRelease(found);Close();return;}
    IOHIDDeviceRef candidate=nullptr;CFSetGetValues(found,(const void **)&candidate);
    CFTypeRef transport=IOHIDDeviceGetProperty(candidate,CFSTR(kIOHIDTransportKey));
    bool usb=transport&&CFGetTypeID(transport)==CFStringGetTypeID()&&CFEqual(transport,CFSTR("USB"));
    bool bt=transport&&CFGetTypeID(transport)==CFStringGetTypeID()&&CFEqual(transport,CFSTR("Bluetooth"));
    CFTypeRef d=IOHIDDeviceGetProperty(candidate,CFSTR(kIOHIDReportDescriptorKey));
    size_t size=(usb||bt)&&d&&CFGetTypeID(d)==CFDataGetTypeID()?
        PSUSBOutputReportSize(CFDataGetBytePtr((CFDataRef)d),CFDataGetLength((CFDataRef)d),bt?0x31:2):0;
    uint32_t product=Number(candidate,CFSTR(kIOHIDProductIDKey));
    bool identity=Number(candidate,CFSTR(kIOHIDVendorIDKey))==0x054c&&(product==0x0ce6||product==0x0df2)&&
        Number(candidate,CFSTR(kIOHIDPrimaryUsagePageKey))==1&&Number(candidate,CFSTR(kIOHIDPrimaryUsageKey))==5;
    bool valid=identity&&((bt&&size==78)||(usb&&(size==48||size==64)));
    if(valid)status=IOHIDDeviceOpen(candidate,kIOHIDOptionsTypeNone);else status=kIOReturnUnsupported;
    if(!status){device=candidate;CFRetain(device);bluetooth=bt;reportLength=size;if(!systemBackend.load())capable=true;retryAt=0;}
    CFRelease(found);
    Log(@"native_trigger endpoint status=0x%x report_bytes=%lu shared=1 input_callbacks_unchanged=1 physical_feedback_verified=0",status,size);
    if(status)Close();
}
static void ReleaseIfSuspended(uint64_t epoch) {
    if(!allowed||epoch==generation.load())Release();
}
static void Suspend() {
    allowed=false;uint64_t epoch=++generation;
    // A concurrent resume may enqueue a new effect before this block. Never
    // release that newer effect with an older suspension request.
    if(systemBackend.load()) {
        dispatch_async(dispatch_get_main_queue(),^{
            if(!allowed||epoch==generation.load())PSSystemTrigger::Release();
        });
    } else if(queue)dispatch_async(queue,^{ReleaseIfSuspended(epoch);});
}
static void SetAllowed(bool value) {
    if(allowed.exchange(value)==value)return;
    uint64_t epoch=++generation;
    if(value)return;
    if(systemBackend.load()) {
        dispatch_async(dispatch_get_main_queue(),^{
            if(!allowed||epoch==generation.load())PSSystemTrigger::Release();
        });
    } else if(queue)dispatch_async(queue,^{ReleaseIfSuspended(epoch);});
}
static void Submit(NativeTrigger::Pair effects) {
    if(!queue||!capable||!allowed)return;
    uint64_t epoch=generation.load(),controller=selection.load();
    if(systemBackend.load()) {
        if(!NSThread.isMainThread||epoch!=generation.load()||!allowed)return;
        unsigned changes=PSSystemTrigger::Apply(effects);
        if(changes) {
            auto count=writes.fetch_add(changes)+changes;
            if(count<=4)Log(@"native_trigger output transport=game_controller requests=%llu physical_feedback_verified=0",count);
        }
        return;
    }
    dispatch_async(queue,^{
        if(epoch!=generation.load()||controller!=selection.load()||!allowed||!capable)return;
        unsigned mask=0;
        for(unsigned i=0;i<2;++i) {
            bool nonzero=NativeTrigger::Encode(effects[i])[0]!=5;
            if((nonzero&&(!owned[i]||effects[i]!=sent[i]))||(!nonzero&&owned[i]))mask|=1u<<i;
        }
        if(!mask)return;
        if(!Write(mask,effects)){++generation;Close();retryAt=CACurrentMediaTime()+3;return;}
        for(unsigned i=0;i<2;++i)if(mask&(1u<<i)){sent[i]=effects[i];owned[i]=NativeTrigger::Encode(effects[i])[0]!=5;}
    });
}
static void PollController() {
    if(!queue||CACurrentMediaTime()<nextSelectionAt)return;
    nextSelectionAt=CACurrentMediaTime()+.2;
    NSArray<GCController *> *connected=GCController.controllers;
    GCController *c=GCController.current;
    if(!c&&connected.count==1)c=connected.firstObject;
    if(!c||c.isSnapshot||!c.extendedGamepad||![connected containsObject:c])c=nil;
    GCDualSenseGamepad *pad=c?PSSystemTrigger::Profile(c):nil;
    if(c&&!pad&&[(c.vendorName?:@"") rangeOfString:@"DualSense" options:NSCaseInsensitiveSearch].location==NSNotFound)c=nil;
    // Some reconnects replace the physical profile while retaining the GC
    // controller object. Retire both endpoints before accepting new effects.
    bool endpointChanged=c&&((pad&&!PSSystemTrigger::Matches(pad))||(!pad&&systemBackend.load()));
    if(c!=selected||endpointChanged) {
        capable=false;
        Suspend();
        PSSystemTrigger::Reset();
        selected=c;
        bool useSystem=pad&&PSSystemTrigger::Select(pad);
        systemBackend=useSystem;
        uint64_t version=++selection;
        dispatch_async(queue,^{
            Close();
            retryAt=0;
            if(version!=selection.load())return;
            if(useSystem) {
                // Retire any old raw reports before publishing the system
                // endpoint so an old Off report cannot erase a new effect.
                dispatch_async(dispatch_get_main_queue(),^{
                    if(version!=selection.load())return;
                    capable=true;
                    Log(@"native_trigger endpoint game_controller=1 real_controller=1 dualsense_left_right=1");
                });
            } else if(c)Open();
        });
    } else if(c&&!capable&&!systemBackend.load()) {
        uint64_t version=selection.load();
        dispatch_async(queue,^{if(version==selection.load())Open();});
    }
}
static void Install() {
    if(queue)return;queue=dispatch_queue_create("local.genshin.ps.triggers",DISPATCH_QUEUE_SERIAL);
    NSNotificationCenter *nc=NSNotificationCenter.defaultCenter;
    for(NSString *name in @[GCControllerDidConnectNotification,GCControllerDidDisconnectNotification,GCControllerDidBecomeCurrentNotification])
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){nextSelectionAt=0;PollController();}];
    [nc addObserverForName:@"NSApplicationWillTerminateNotification" object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){Suspend();PSSystemTrigger::Reset();dispatch_sync(queue,^{Close();});}];
    PollController();
}
}
