#include "RacingGameState.h"
#include "Net/UnrealNetwork.h"

ARacingGameState::ARacingGameState()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ARacingGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ARacingGameState, RacePhase);
    DOREPLIFETIME(ARacingGameState, RaceTimeSeconds);
    DOREPLIFETIME(ARacingGameState, bRaceTimerRunning);
    DOREPLIFETIME(ARacingGameState, CountdownSecondsRemaining);
    DOREPLIFETIME(ARacingGameState, DriverStandings);
}

void ARacingGameState::SetRacePhase(ERacePhase NewPhase)
{
    RacePhase = NewPhase;
}

void ARacingGameState::StartRaceTimer()
{
    RaceTimeSeconds   = 0.0f;
    bRaceTimerRunning = true;
}

void ARacingGameState::TickRaceTimer(float DeltaTime)
{
    if (bRaceTimerRunning)
        RaceTimeSeconds += DeltaTime;
}

void ARacingGameState::UpdateStandings(const TArray<FDriverStanding>& NewStandings)
{
    DriverStandings = NewStandings;
}

void ARacingGameState::SetCountdownSeconds(float Seconds)
{
    CountdownSecondsRemaining = Seconds;
}

void ARacingGameState::OnRep_RacePhase()
{
    UE_LOG(LogTemp, Log, TEXT("[GameState] Race phase changed to: %d"), (int32)RacePhase);
    // Aqui se puede disparar un evento de Blueprint para actualizar la UI
}

void ARacingGameState::OnRep_Standings()
{
    // Actualizar el HUD de clasificación (se implementa en Blueprint/Widget)
}
