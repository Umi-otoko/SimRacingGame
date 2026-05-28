#pragma once

#include "CoreMinimal.h"
#include "RacingTypes.generated.h"

/** Fases de la carrera — compartido entre GameMode, GameState y UI */
UENUM(BlueprintType)
enum class ERacePhase : uint8
{
    Lobby       UMETA(DisplayName = "Lobby"),
    Countdown   UMETA(DisplayName = "Countdown"),
    Racing      UMETA(DisplayName = "Racing"),
    Finished    UMETA(DisplayName = "Finished")
};
