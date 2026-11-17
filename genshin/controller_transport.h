#pragma once
#import <IOKit/IOKitLib.h>
#import <IOKit/hid/IOHIDKeys.h>
#include "controller_ui_policy.h"

// Read-only I/O Registry metadata. No HID handle is opened, no reports are sent
// and no input callback or input-monitoring permission is requested here.
namespace DesktopControllerTransport {
using namespace DesktopControllerUI;
struct Snapshot {
    EndpointSummary dualSense,dualShock;
    bool operator==(const Snapshot &b)const {
        return dualSense.count==b.dualSense.count&&dualSense.transport==b.dualSense.transport&&
            dualShock.count==b.dualShock.count&&dualShock.transport==b.dualShock.transport;
    }
};
static unsigned Number(NSDictionary *p,NSString *key) {
    id v=p[key];return [v isKindOfClass:NSNumber.class]?[v unsignedIntValue]:0;
}
static Snapshot Read() {
    Snapshot result;io_iterator_t it=IO_OBJECT_NULL;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOHIDDevice"),&it)!=KERN_SUCCESS)return result;
    while(io_service_t service=IOIteratorNext(it)){
        CFMutableDictionaryRef raw=nullptr;
        if(IORegistryEntryCreateCFProperties(service,&raw,kCFAllocatorDefault,0)==KERN_SUCCESS&&raw){
            NSDictionary *p=CFBridgingRelease(raw);
            if(Number(p,@"VendorID")==0x054c&&Number(p,@"PrimaryUsagePage")==1&&
               (Number(p,@"PrimaryUsage")==5||Number(p,@"PrimaryUsage")==4)&&
               ![p[@"VirtualHIDevice"] boolValue]){
                unsigned product=Number(p,@"ProductID");id t=p[@"Transport"];
                Transport transport=[t isEqual:@"USB"]?Transport::USB:
                    [t isEqual:@"Bluetooth"]?Transport::Bluetooth:Transport::Unknown;
                if(product==0x0ce6||product==0x0df2)result.dualSense.add(transport);
                if(product==0x05c4||product==0x09cc)result.dualShock.add(transport);
            }
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(it);return result;
}
}
