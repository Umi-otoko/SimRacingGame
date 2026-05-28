#pragma once

#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "RacingFinishLine.generated.h"

class ARacingVehiclePawn;

/**
 * ARacingFinishLine
 * Trigger invisible en la línea de meta.
 * Cuando el vehículo lo cruza, llama a NotifyFinishLineCrossed()
 * en el pawn, que a su vez notifica al GameMode para validar la vuelta.
 *
 * El GameMode rechaza vueltas incompletas (faltan checkpoints).
 * La línea se coloca justo debajo del pórtico de salida.
 */
UCLASS()
class SIMRACINGGAME_API ARacingFinishLine : public ATriggerBox
{
    GENERATED_BODY()

public:
    ARacingFinishLine();

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                        bool bFromSweep, const FHitResult& SweepResult);
};
