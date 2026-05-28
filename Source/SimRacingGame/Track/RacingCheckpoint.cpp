#include "RacingCheckpoint.h"
#include "RacingVehiclePawn.h"
#include "Components/BoxComponent.h"

ARacingCheckpoint::ARacingCheckpoint()
{
    PrimaryActorTick.bCanEverTick = false;
    GetCollisionComponent()->SetGenerateOverlapEvents(true);
}

void ARacingCheckpoint::BeginPlay()
{
    Super::BeginPlay();
    GetCollisionComponent()->OnComponentBeginOverlap.AddDynamic(
        this, &ARacingCheckpoint::OnOverlapBegin);
}

void ARacingCheckpoint::OnOverlapBegin(
    UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    ARacingVehiclePawn* Vehicle = Cast<ARacingVehiclePawn>(OtherActor);
    if (!Vehicle) return;
    Vehicle->NotifyCheckpointCrossed(CheckpointIndex);
}
