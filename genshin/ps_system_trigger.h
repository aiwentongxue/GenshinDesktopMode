#pragma once
#import <GameController/GameController.h>

namespace PSSystemTrigger {
static GCDualSenseAdaptiveTrigger *sides[2];
static NativeTrigger::Pair sent{};
static bool owned[2];
static double lastWrite[2];

static bool Matches(GCDualSenseGamepad *pad) {
    return pad&&sides[0]==pad.leftTrigger&&sides[1]==pad.rightTrigger;
}

static GCDualSenseGamepad *Profile(GCController *controller) {
    id extended=controller.extendedGamepad;
    if([extended isKindOfClass:GCDualSenseGamepad.class])return extended;
    id physical=controller.physicalInputProfile;
    return [physical isKindOfClass:GCDualSenseGamepad.class]?physical:nil;
}

static bool Select(GCDualSenseGamepad *pad) {
    sides[0]=pad.leftTrigger;
    sides[1]=pad.rightTrigger;
    if(sides[0]&&sides[1])return true;
    sides[0]=sides[1]=nil;
    return false;
}

static void Release() {
    for(unsigned i=0;i<2;++i) {
        if(owned[i])[sides[i] setModeOff];
        owned[i]=false;
        lastWrite[i]=0;
    }
    sent={};
}

static void Reset() {
    Release();
    sides[0]=sides[1]=nil;
}

static float Zone(unsigned zone) {return (zone+.5f)/10.f;}

static unsigned Apply(const NativeTrigger::Pair &effects) {
    using NativeTrigger::Mode;
    unsigned changes=0;
    const double now=CACurrentMediaTime();
    for(unsigned i=0;i<2;++i) {
        GCDualSenseAdaptiveTrigger *side=sides[i];
        const auto &effect=effects[i];
        bool active=NativeTrigger::Encode(effect)[0]!=5;
        if(!side||(!active&&!owned[i]))continue;
        if(active&&owned[i]&&effect==sent[i]) {
            // Readback is asynchronous. Give it time to settle, and only
            // recover a lost mode. Weapon firing/no-load status is not a fault.
            if(now>=lastWrite[i]&&now-lastWrite[i]<1.)continue;
            auto expected=effect.mode==Mode::Weapon?GCDualSenseAdaptiveTriggerModeWeapon:
                effect.mode==Mode::Feedback?GCDualSenseAdaptiveTriggerModeFeedback:GCDualSenseAdaptiveTriggerModeVibration;
            auto actual=side.mode;
            if(actual==GCDualSenseAdaptiveTriggerModeOff) { /* lost device state */ }
            else if(actual==expected||(actual!=GCDualSenseAdaptiveTriggerModeFeedback&&
                    actual!=GCDualSenseAdaptiveTriggerModeWeapon&&actual!=GCDualSenseAdaptiveTriggerModeVibration))continue;
        }
        ++changes;
        const float strength=effect.strength/8.f;
        if(!active)[side setModeOff];
        else if(effect.mode==Mode::Weapon)
            [side setModeWeaponWithStartPosition:Zone(effect.start) endPosition:Zone(effect.end) resistiveStrength:strength];
        else if(effect.mode==Mode::Feedback) {
            if(@available(macOS 12.3,macCatalyst 15.4,*)) {
                GCDualSenseAdaptiveTriggerPositionalResistiveStrengths strengths{};
                for(unsigned zone=effect.start;zone<10;++zone)strengths.values[zone]=strength;
                [side setModeFeedbackWithResistiveStrengths:strengths];
            } else [side setModeFeedbackWithStartPosition:Zone(effect.start) resistiveStrength:strength];
        } else if(effect.mode==Mode::Vibration) {
            // Apple uses normalized frequency rather than the ScePad byte.
            const float frequency=effect.frequency/255.f;
            if(@available(macOS 12.3,macCatalyst 15.4,*)) {
                GCDualSenseAdaptiveTriggerPositionalAmplitudes amplitudes{};
                for(unsigned zone=effect.start;zone<10;++zone)amplitudes.values[zone]=strength;
                [side setModeVibrationWithAmplitudes:amplitudes frequency:frequency];
            } else [side setModeVibrationWithStartPosition:Zone(effect.start) amplitude:strength frequency:frequency];
        }
        owned[i]=active;
        sent[i]=effect;
        lastWrite[i]=now;
    }
    return changes;
}
}
