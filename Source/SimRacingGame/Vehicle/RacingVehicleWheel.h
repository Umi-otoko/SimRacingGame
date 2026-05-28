#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"
#include "RacingVehicleWheel.generated.h"

UCLASS()
class SIMRACINGGAME_API URacingWheelFront : public UChaosVehicleWheel
{
    GENERATED_BODY()
public:
    URacingWheelFront();
};

UCLASS()
class SIMRACINGGAME_API URacingWheelRear : public UChaosVehicleWheel
{
    GENERATED_BODY()
public:
    URacingWheelRear();
};
