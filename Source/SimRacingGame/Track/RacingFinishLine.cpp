#include "RacingFinishLine.h"
#include "RacingVehiclePawn.h"

ARacingFinishLine::ARacingFinishLine()
{
    PrimaryActorTick.bCanEverTick = false;
    GetCollisionComponent()->SetGenerateOverlapEvents(true);
}

void ARacingFinishLine::BeginPlay()
{
    Super::BeginPlay();
    GetCollisionComponent()->OnComponentBeginOverlap.AddDynamic(
        this, &ARacingFinishLine::OnOverlapBegin);
}

void ARacingFinishLine::OnOverlapBegin(
    UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    ARacingVehiclePawn* Vehicle = Cast<ARacingVehiclePawn>(OtherActor);
    if (!Vehicle) return;
    Vehicle->NotifyFinishLineCrossed();
}
