#import <AppKit/AppKit.h>
#include "../genshin/key_alert_guard.h"
#include "../genshin/pointer_event.h"
#include <cassert>
#include <iostream>

static bool gameContext;
static unsigned originalCalls;
static bool Context() {return gameContext;}
static void OriginalFallback(id,SEL,SEL) {++originalCalls;}

int main() {
    @autoreleasepool {
        // Exercise the real ObjC interception without posting keyboard input
        // or invoking the system's audible fallback in this test process.
        Method method=class_getInstanceMethod(NSResponder.class,@selector(noResponderFor:));
        IMP systemOriginal=method_setImplementation(method,(IMP)OriginalFallback);
        assert(DesktopKeyAlerts::Install(Context));
        NSResponder *responder=[NSResponder new];
        gameContext=true;
        [responder noResponderFor:@selector(keyDown:)];assert(originalCalls==0);
        [responder noResponderFor:@selector(mouseDown:)];assert(originalCalls==1);
        gameContext=false;
        [responder noResponderFor:@selector(keyDown:)];assert(originalCalls==2);
        const NSUInteger modifierCases[]={NSEventModifierFlagControl,NSEventModifierFlagOption,
                              NSEventModifierFlagControl|NSEventModifierFlagOption};
        for(NSUInteger flags:modifierCases) {
            NSEvent *key=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                modifierFlags:flags timestamp:1 windowNumber:0 context:nil characters:@"w"
                charactersIgnoringModifiers:@"w" isARepeat:YES keyCode:13];
            gameContext=DesktopKeyAlerts::IsSilentGameKey((unsigned)key.type,key.modifierFlags);
            [responder noResponderFor:@selector(keyDown:)];assert(originalCalls==2);
        }
        gameContext=DesktopKeyAlerts::IsSilentGameKey(NSEventTypeKeyDown,NSEventModifierFlagCommand|NSEventModifierFlagControl);
        [responder noResponderFor:@selector(keyDown:)];assert(originalCalls==3);
        assert(!DesktopKeyAlerts::IsSilentGameKey(NSEventTypeKeyUp,0));
        method_setImplementation(method,systemOriginal);

        DesktopMouseEventSequence sequence;
        unsigned delivered=0;
        for(unsigned i=0;i<300;++i) {
            NSEvent *event=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
                location:NSMakePoint(40+i,80) modifierFlags:0 timestamp:1+i*0.001
                windowNumber:0 context:nil eventNumber:7 clickCount:1 pressure:1];
            // Hold one button while eventNumber remains constant, and pass
            // every real packet through all three monitored entry points.
            for(int path=0;path<3;++path) if(sequence.firstDelivery(event)) ++delivered;
        }
        assert(delivered==300);
        sequence.reset();
        NSObject *receiverA=[NSObject new],*receiverB=[NSObject new];
        for(auto type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}) {
            NSEvent *event=[NSEvent mouseEventWithType:type location:NSMakePoint(15,20)
                modifierFlags:0 timestamp:3 windowNumber:0 context:nil eventNumber:8 clickCount:1 pressure:1];
            // A duplicate on the same native target is ignored, while a menu
            // can legitimately retarget that packet into its popup window.
            assert(sequence.firstDelivery(event,receiverA));
            assert(!sequence.firstDelivery(event,receiverA));
            assert(sequence.firstDelivery(event,receiverB));
            assert(!sequence.firstDelivery(event,receiverB));
        }
        sequence.reset();
        for(auto type:{NSEventTypeMouseMoved,NSEventTypeRightMouseDragged,NSEventTypeOtherMouseDragged}) {
            NSEvent *event=[NSEvent mouseEventWithType:type location:NSMakePoint(10,10)
                modifierFlags:0 timestamp:4 windowNumber:0 context:nil eventNumber:9 clickCount:1 pressure:1];
            assert(sequence.firstDelivery(event));assert(!sequence.firstDelivery(event));
        }
        std::cout<<"AppKit: scoped unhandled-key suppression and 300 held-button packets across three paths passed\n";
    }
}
