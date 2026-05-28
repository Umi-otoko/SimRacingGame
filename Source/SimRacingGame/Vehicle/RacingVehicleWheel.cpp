#include "RacingVehicleWheel.h"

URacingWheelFront::URacingWheelFront()
{
    WheelRadius        = 30.0f;
    WheelWidth         = 20.0f;
    FrictionForceMultiplier = 3.5f;
    SlipThreshold      = 20.0f;
    SkidThreshold      = 40.0f;
    MaxSteerAngle      = 40.0f;
    bAffectedByHandbrake = false;
    bAffectedByEngine    = false;

    // Suspension
    SuspensionMaxRaise = 10.0f;
    SuspensionMaxDrop  = 12.0f;
    SuspensionDampingRatio = 0.55f;

}

URacingWheelRear::URacingWheelRear()
{
    WheelRadius        = 30.0f;
    WheelWidth         = 22.0f;
    FrictionForceMultiplier = 3.5f;
    SlipThreshold      = 20.0f;
    SkidThreshold      = 40.0f;
    MaxSteerAngle      = 0.0f;
    bAffectedByHandbrake = true;
    bAffectedByEngine    = true;

    SuspensionMaxRaise = 10.0f;
    SuspensionMaxDrop  = 12.0f;
    SuspensionDampingRatio = 0.55f;

}
