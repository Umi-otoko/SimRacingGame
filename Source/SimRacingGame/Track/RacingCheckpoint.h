#pragma once

#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "RacingCheckpoint.generated.h"

class ARacingVehiclePawn;

/**
 * ARacingCheckpoint
 * Trigger invisible que el jugador debe cruzar en orden.
 * Notifica al GameMode; el GameMode valida si la vuelta es completa.
 *
 * Coloca en el nivel con CheckpointIndex = 0, 1, 2… en sentido de marcha.
 * El Python script los spawna y configura automáticamente.
 */
UCLASS()
class SIMRACINGGAME_API ARacingCheckpoint : public ATriggerBox
{
    GENERATED_BODY()

public:
    ARacingCheckpoint();

    /** Índice del checkpoint (0-based, en orden de marcha) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race")
    int32 CheckpointIndex = 0;

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                        bool bFromSweep, const FHitResult& SweepResult);
};
