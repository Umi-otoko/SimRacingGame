#include "RacingPlayerState.h"
#include "Net/UnrealNetwork.h"

ARacingPlayerState::ARacingPlayerState()
{
    bReplicates = true;
}

void ARacingPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ARacingPlayerState, CurrentLap);
    DOREPLIFETIME(ARacingPlayerState, RacePosition);
    DOREPLIFETIME(ARacingPlayerState, BestLapMs);
    DOREPLIFETIME(ARacingPlayerState, LastLapMs);
    DOREPLIFETIME(ARacingPlayerState, bFinished);
}

void ARacingPlayerState::StartLapTimer()
{
    LapStartTimeSeconds = FPlatformTime::Seconds();
}

float ARacingPlayerState::GetCurrentLapTime() const
{
    if (LapStartTimeSeconds <= 0.0) return 0.0f;
    return static_cast<float>(FPlatformTime::Seconds() - LapStartTimeSeconds);
}

void ARacingPlayerState::RegisterLapTime(int64 LapTimeMs)
{
    LastLapMs = LapTimeMs;
    AllLapTimes.Add(LapTimeMs);

    if (BestLapMs == 0 || LapTimeMs < BestLapMs)
        BestLapMs = LapTimeMs;

    CurrentLap++;
    LapStartTimeSeconds = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Log, TEXT("[PlayerState] %s — Lap %d: %.3fs (Best: %.3fs)"),
           *GetPlayerName(),
           CurrentLap - 1,
           LapTimeMs / 1000.0f,
           BestLapMs / 1000.0f);
}

void ARacingPlayerState::SetFinished(int32 FinalPosition)
{
    bFinished     = true;
    RacePosition  = FinalPosition;
}
