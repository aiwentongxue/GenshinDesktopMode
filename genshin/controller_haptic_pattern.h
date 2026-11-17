#pragma once
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#include "controller_haptic_state.h"

// This builds data only. Creating a pattern never starts a haptic engine.
static CHHapticPattern *ControllerMotorPattern(ControllerHapticState::Motor motor,NSError **error) {
    CHHapticEvent *event=[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
        parameters:@[[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1],
                     [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0]]
        relativeTime:0 duration:GCHapticDurationInfinite];
    CHHapticParameterCurve *intensity=[[CHHapticParameterCurve alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
        controlPoints:@[[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:0 value:motor.level]] relativeTime:0];
    CHHapticParameterCurve *sharpness=[[CHHapticParameterCurve alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl
        controlPoints:@[[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:0 value:motor.sharpness]] relativeTime:0];
    return [[CHHapticPattern alloc] initWithEvents:@[event] parameterCurves:@[intensity,sharpness] error:error];
}
