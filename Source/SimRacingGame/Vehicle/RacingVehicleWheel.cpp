#include "RacingVehicleWheel.h"

URacingWheelFront::URacingWheelFront()
{
    WheelRadius             = 32.0f;
    WheelWidth              = 22.0f;
    WheelMass               = 22.0f;   // kg — llanta de aleación + neumático
    FrictionForceMultiplier = 3.0f;
    SlipThreshold           = 20.0f;
    SkidThreshold           = 40.0f;
    MaxSteerAngle           = 30.0f;
    bAffectedBySteering     = true;
    bAffectedByBrake        = true;
    bAffectedByHandbrake    = false;
    bAffectedByEngine       = false;
    MaxBrakeTorque          = 1500.0f;

    SuspensionMaxRaise      = 10.0f;
    SuspensionMaxDrop       = 14.0f;
    SuspensionDampingRatio  = 0.55f;
    SpringRate              = 350.0f;  // N/cm — equivalente a ~3 Hz frecuencia natural
    SpringPreload           = 1200.0f; // N — preload para ride height correcto
}

URacingWheelRear::URacingWheelRear()
{
    WheelRadius             = 32.0f;
    WheelWidth              = 26.0f;
    WheelMass               = 24.0f;
    FrictionForceMultiplier = 3.0f;
    SlipThreshold           = 20.0f;
    SkidThreshold           = 40.0f;
    MaxSteerAngle           = 0.0f;
    bAffectedBySteering     = false;
    bAffectedByBrake        = true;
    bAffectedByHandbrake    = true;
    bAffectedByEngine       = true;
    MaxBrakeTorque          = 1800.0f;
    MaxHandBrakeTorque      = 3000.0f;

    SuspensionMaxRaise      = 10.0f;
    SuspensionMaxDrop       = 14.0f;
    SuspensionDampingRatio  = 0.55f;
    SpringRate              = 400.0f;
    SpringPreload           = 1400.0f;
}
