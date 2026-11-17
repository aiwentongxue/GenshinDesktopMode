#pragma once
#import <CoreHaptics/CoreHaptics.h>
#include "controller_haptic_state.h"
#include "controller_haptic_pattern.h"

// All hardware work stays off the Unity/main thread. No controller handlers,
// input maps, cursor methods or game settings are replaced by this backend.
@interface GDMHapticChannel : NSObject
@property(nonatomic,strong) CHHapticEngine *engine;
@property(nonatomic,strong) id<CHHapticPatternPlayer> player;
@property(nonatomic) float level;
@property(nonatomic) float sharpness;
@end
@implementation GDMHapticChannel
@end

namespace ControllerHaptics {
static dispatch_queue_t outputQueue;
static dispatch_source_t expiryTimer;
static GCController *outputController;
static GDMHapticChannel *channels[2];
static ControllerHapticState state;
static void *motorOwner;
static std::atomic<bool> capable{false},foreground{false};
static std::atomic<uint64_t> epoch{0};
static std::atomic<uint64_t> controllerRevision{0};
static uint64_t sessionRevision;
static std::atomic<uint64_t> nativeRequests{0},sdkRequests{0},nativeObserved{0},sdkObserved{0};
static std::atomic<bool> rewiredBridgeReady{false};
static uint64_t playersStarted;
static double retryAfter;
static bool splitHandles,installed;
static bool sdkInstalled,sdkRefused;
static bool capabilityReported;

static bool Allowed(void) {
    return NativeVibrationSettings::Enabled() && capable.load() && foreground.load() && NativeBridge::controllerInputActive.load() &&
        !NativeBridge::nativeOverlayActive.load() && !PSAudio::OwnsControllerFeedback();
}
static void StopNow(void) {
    ++sessionRevision;
    state.stop();motorOwner=nullptr;
    for(auto &channel:channels) {
        [channel.player stopAtTime:CHHapticTimeImmediate error:nil];
        channel.engine.resetHandler=^{};channel.engine.stoppedHandler=^(CHHapticEngineStoppedReason){};
        [channel.engine stopWithCompletionHandler:nil];
        channel=nil;
    }
    if(expiryTimer) dispatch_source_set_timer(expiryTimer,DISPATCH_TIME_FOREVER,DISPATCH_TIME_FOREVER,0);
}
static void Stop(void) {
    PSAudio::Silence();
    ++epoch;
    if(outputQueue) dispatch_async(outputQueue,^{StopNow();});
}
static void ScheduleExpiry(void) {
    double deadline=state.nextDeadline();
    double now=CACurrentMediaTime();
    if(now<retryAfter&&(state.output(0).level||state.output(1).level))
        deadline=deadline?std::min(deadline,retryAfter):retryAfter;
    if(!deadline) {
        dispatch_source_set_timer(expiryTimer,DISPATCH_TIME_FOREVER,DISPATCH_TIME_FOREVER,0);return;
    }
    // Very long/infinite-like native values must not overflow dispatch time.
    int64_t delay=(int64_t)(std::min(3600.,std::max(0.,deadline-now))*NSEC_PER_SEC);
    dispatch_source_set_timer(expiryTimer,dispatch_time(DISPATCH_TIME_NOW,delay),DISPATCH_TIME_FOREVER,NSEC_PER_MSEC);
}
static void EngineInterrupted(uint64_t session,unsigned side,CHHapticEngine *engine) {
    dispatch_async(outputQueue,^{
        if(session!=sessionRevision || channels[side].engine!=engine) return;
        ++epoch;StopNow();
        // Do not replay old combat effects after a haptics service restart.
    });
}
static bool RenderChannel(unsigned side,ControllerHapticState::Motor motor) {
    GDMHapticChannel *channel=channels[side];
    if(!motor.level) {
        [channel.player stopAtTime:CHHapticTimeImmediate error:nil];
        channel.engine.resetHandler=^{};channel.engine.stoppedHandler=^(CHHapticEngineStoppedReason){};
        [channel.engine stopWithCompletionHandler:nil];channels[side]=nil;return true;
    }
    if(!channel) {
        GCHapticsLocality locality=splitHandles?(side?GCHapticsLocalityRightHandle:GCHapticsLocalityLeftHandle):GCHapticsLocalityDefault;
        CHHapticEngine *engine=[outputController.haptics createEngineWithLocality:locality];
        if(!engine) {Log(@"controller_haptics engine_unavailable locality=%@",locality);return false;}
        channel=[GDMHapticChannel new];channel.engine=engine;channels[side]=channel;
        engine.playsHapticsOnly=YES;engine.autoShutdownEnabled=YES;
        uint64_t session=sessionRevision;
        __weak CHHapticEngine *weakEngine=engine;
        engine.resetHandler=^{CHHapticEngine *current=weakEngine;if(current)EngineInterrupted(session,side,current);};
        engine.stoppedHandler=^(CHHapticEngineStoppedReason){CHHapticEngine *current=weakEngine;if(current)EngineInterrupted(session,side,current);};
    }
    NSError *error=nil;
    if(!channel.player) {
        if(![channel.engine startAndReturnError:&error]) {
            Log(@"controller_haptics engine_start_failed domain=%@ code=%ld",error.domain,(long)error.code);return false;
        }
        // Initial controls are part of the pattern, avoiding a full-strength
        // sample between starting a player and its first intensity update.
        CHHapticPattern *pattern=ControllerMotorPattern(motor,&error);
        if(!pattern) {Log(@"controller_haptics pattern_failed domain=%@ code=%ld",error.domain,(long)error.code);return false;}
        channel.player=[channel.engine createPlayerWithPattern:pattern error:&error];
        if(!channel.player || ![channel.player startAtTime:CHHapticTimeImmediate error:&error]) {
            Log(@"controller_haptics player_start_failed domain=%@ code=%ld",error.domain,(long)error.code);return false;
        }
        if(++playersStarted<=4)Log(@"controller_haptics playback_api_accepted side=%u level=%.3f physical_feel_unverified=1",side,motor.level);
    } else if(channel.level!=motor.level || channel.sharpness!=motor.sharpness) {
        NSArray *parameters=@[
            [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:motor.level relativeTime:0],
            [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl value:motor.sharpness relativeTime:0]];
        if(![channel.player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) {
            Log(@"controller_haptics parameter_update_failed domain=%@ code=%ld",error.domain,(long)error.code);return false;
        }
    }
    channel.level=motor.level;channel.sharpness=motor.sharpness;
    return true;
}
static void Render(void) {
    if(!Allowed()) {StopNow();return;}
    double now=CACurrentMediaTime();state.expire(now);
    if(now<retryAfter) {ScheduleExpiry();return;}
    auto left=state.output(0),right=state.output(1);
    bool ok=splitHandles?(RenderChannel(0,left)&&RenderChannel(1,right)):
        RenderChannel(0,left.level>=right.level?left:right);
    if(!ok) {
        // Preserve the active deadlines through a short service interruption.
        // StopNow still discards state when focus, device, or mode changes.
        auto pending=state;
        void *owner=motorOwner;
        StopNow();
        state=pending;motorOwner=owner;
        retryAfter=now+2;
        Log(@"controller_haptics output_failed=1 retry_after_seconds=2");
    }
    ScheduleExpiry();
}
static void SubmitMotor(void *owner,int motor,float level,float duration,bool stopOthers) {
    if(!Allowed()||motor<0||motor>1||!std::isfinite(duration)||duration<0) return;
    if(++nativeRequests<=4) Log(@"controller_haptics request=rewired motor=%d level=%.3f duration=%.3f",motor,level,duration);
    uint64_t request=epoch.load();
    dispatch_async(outputQueue,^{
        if(request!=epoch.load() || !Allowed()) return;
        if(motorOwner&&motorOwner!=owner) state.motors[0]=state.motors[1]={};
        motorOwner=owner;
        if(state.set(motor,level,duration,CACurrentMediaTime(),stopOthers)) Render();
    });
}
static void SubmitPair(void *owner,float left,float right,float leftDuration,float rightDuration) {
    if(!Allowed()||!std::isfinite(leftDuration)||!std::isfinite(rightDuration)||leftDuration<0||rightDuration<0)return;
    if(++nativeRequests<=4) Log(@"controller_haptics request=rewired_pair left=%.3f right=%.3f",left,right);
    uint64_t request=epoch.load();
    dispatch_async(outputQueue,^{
        if(request!=epoch.load() || !Allowed()) return;
        if(motorOwner&&motorOwner!=owner) state.motors[0]=state.motors[1]={};
        motorOwner=owner;double now=CACurrentMediaTime();
        state.set(0,left,leftDuration,now);state.set(1,right,rightDuration,now);Render();
    });
}
static void StopMotors(void *owner) {
    if(!outputQueue)return;
    dispatch_async(outputQueue,^{
        if(motorOwner!=owner) return;
        state.motors[0]=state.motors[1]={};motorOwner=nullptr;Render();
    });
}
static void SubmitPulse(long long milliseconds,float intensity,float sharpness) {
    if(!Allowed())return;
    if(++sdkRequests<=4) Log(@"controller_haptics request=sdk duration_ms=%lld intensity=%.3f sharpness=%.3f",milliseconds,intensity,sharpness);
    uint64_t request=epoch.load();
    dispatch_async(outputQueue,^{
        if(request!=epoch.load() || !Allowed()) return;
        if(state.pulse(milliseconds,intensity,sharpness,CACurrentMediaTime())) Render();
    });
}
static void RefreshController(GCController *preferred=nil) {
    GCController *controller=preferred?:GCController.current;
    NSArray<GCController *> *connected=GCController.controllers;
    if(!controller && connected.count==1) controller=connected.firstObject;
    NSString *vendor=controller.vendorName?:@"unknown";
    NSSet *localities=controller.haptics.supportedLocalities;
    bool snapshot=controller.isSnapshot;
    if(!controller || ![connected containsObject:controller] || snapshot || !controller.extendedGamepad || !localities.count) controller=nil;
    uint64_t request=++controllerRevision;
    dispatch_async(outputQueue,^{
        if(request!=controllerRevision.load()) return;
        if(capabilityReported&&outputController==controller && capable.load()==(controller!=nil))return;
        capabilityReported=true;
        capable=false;++epoch;
        StopNow();outputController=controller;retryAfter=0;
        splitHandles=[localities containsObject:GCHapticsLocalityLeftHandle]&&[localities containsObject:GCHapticsLocalityRightHandle];
        capable=controller!=nil;
        Log(@"controller_haptics available=%d separate_handles=%d connected=%lu vendor=%@ localities=%@ snapshot=%d native_requests_only=1",
            capable.load(),splitHandles,(unsigned long)connected.count,vendor,localities,snapshot);
    });
}
static void ObserveForeground(bool active) {
    PSAudio::ObserveGameState(active);
    // AppKit/UIKit activation notifications can run before both applications
    // report active. Reconcile from the same real focus snapshot as input,
    // instead of leaving an early false value latched for the whole session.
    bool previous=foreground.exchange(active);
    if(previous==active)return;
    if(active)RefreshController();else {Stop();NativeTrigger::Suspend();}
    Log(@"controller_haptics foreground=%d",active);
}
static void ReportStatus(void) {
    Log(@"controller_haptics status capable=%d foreground=%d controller_mode=%d native_observed=%llu native_requests=%llu sdk_observed=%llu sdk_requests=%llu sdk_hook=%d rewired_ready=%d",
        capable.load(),foreground.load(),NativeBridge::controllerInputActive.load(),
        nativeObserved.load(),nativeRequests.load(),sdkObserved.load(),sdkRequests.load(),sdkInstalled,rewiredBridgeReady.load());
}
static void ObserveNativeCommand(void) {
    if(++nativeObserved<=4)Log(@"controller_haptics native_command_seen allowed=%d capable=%d foreground=%d controller_mode=%d",
        Allowed(),capable.load(),foreground.load(),NativeBridge::controllerInputActive.load());
}

static BOOL (*originalPhoneSupports)(id,SEL);
static void (*originalPhonePulse)(id,SEL,long long,float,float);
static BOOL PhoneSupports(id self,SEL selector) {
    return originalPhoneSupports(self,selector) || capable.load();
}
static void PhonePulse(id self,SEL selector,long long milliseconds,float intensity,float sharpness) {
    if(++sdkObserved<=4)Log(@"controller_haptics sdk_command_seen allowed=%d duration_ms=%lld",Allowed(),milliseconds);
    if(Allowed()) {SubmitPulse(milliseconds,intensity,sharpness);return;}
    originalPhonePulse(self,selector,milliseconds,intensity,sharpness);
}
static bool ArgumentIs(Method method,unsigned index,const char *expected) {
    char type[24]={};method_getArgumentType(method,index,type,sizeof(type));return !strcmp(type,expected);
}
static bool InstallSDKPulse(void) {
    if(sdkInstalled||sdkRefused)return true;
    Class cls=NSClassFromString(@"MHYHapticManager");
    if(!cls)return false;
    SEL pulse=sel_registerName("triggerVibrationWithDuration:intensity:sharpness:"),supports=sel_registerName("supportsHaptics");
    Method pulseMethod=cls?class_getInstanceMethod(cls,pulse):nullptr;
    Method supportMethod=cls?class_getClassMethod(cls,supports):nullptr;
    char result[16]={};if(pulseMethod)method_getReturnType(pulseMethod,result,sizeof(result));
    bool valid=pulseMethod && supportMethod && method_getNumberOfArguments(pulseMethod)==5 && !strcmp(result,"v") &&
        ArgumentIs(pulseMethod,2,"q") && ArgumentIs(pulseMethod,3,"f") && ArgumentIs(pulseMethod,4,"f");
    if(supportMethod)method_getReturnType(supportMethod,result,sizeof(result));
    valid=valid && !strcmp(result,"B") && method_getNumberOfArguments(supportMethod)==2;
    if(!valid) {sdkRefused=true;Log(@"controller_haptics sdk_pulse=0 reason=signature_mismatch");return true;}
    originalPhonePulse=(decltype(originalPhonePulse))method_getImplementation(pulseMethod);
    originalPhoneSupports=(decltype(originalPhoneSupports))method_getImplementation(supportMethod);
    class_replaceMethod(cls,pulse,(IMP)PhonePulse,method_getTypeEncoding(pulseMethod));
    class_replaceMethod(object_getClass(cls),supports,(IMP)PhoneSupports,method_getTypeEncoding(supportMethod));
    sdkInstalled=true;
    Log(@"controller_haptics sdk_pulse=1 duration_units=milliseconds supported_clients=shared_objc_selector");
    return true;
}
static bool TryInstallRewired(void);
static void Install(void) {
    if(installed||!IsCompatibleRuntime())return;
    installed=true;
    PSAudio::Install();NativeTrigger::Install();
    outputQueue=dispatch_queue_create("local.genshin.controller.haptics",DISPATCH_QUEUE_SERIAL);
    expiryTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,outputQueue);
    dispatch_source_set_event_handler(expiryTimer,^{Render();});
    dispatch_source_set_timer(expiryTimer,DISPATCH_TIME_FOREVER,DISPATCH_TIME_FOREVER,0);dispatch_resume(expiryTimer);
    NativeBridge::stopControllerFeedback=[](){Stop();NativeTrigger::Suspend();};
    foreground=NativeBridge::IsForeground();RefreshController();InstallSDKPulse();
    NSNotificationCenter *nc=NSNotificationCenter.defaultCenter;
    for(NSString *name in @[GCControllerDidConnectNotification,GCControllerDidDisconnectNotification,GCControllerDidBecomeCurrentNotification])
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {RefreshController();}];
    for(NSString *name in @[UIApplicationWillResignActiveNotification,@"NSApplicationWillResignActiveNotification",@"NSApplicationWillTerminateNotification"])
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {foreground=false;Stop();}];
    for(NSString *name in @[UIApplicationDidBecomeActiveNotification,@"NSApplicationDidBecomeActiveNotification"])
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
            dispatch_async(dispatch_get_main_queue(),^{ObserveForeground(NativeBridge::IsForeground());});
        }];
    bool sdkReady=InstallSDKPulse(),rewiredReady=TryInstallRewired();
    if(!sdkReady||!rewiredReady) {
        __block unsigned attempts=0;
        [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
            bool sdk=InstallSDKPulse(),rewired=TryInstallRewired();
            if((sdk&&rewired)||++attempts>=60) {
                Log(@"controller_haptics binding_finished sdk=%d rewired_resolved=%d attempts=%u",sdkInstalled,rewired,attempts);
                ReportStatus();[timer invalidate];
            }
        }];
    }
}
}

#include "controller_haptics_rewired.h"
