#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "RacingTypes.h"
#include "RacingGameState.generated.h"

USTRUCT(BlueprintType)
struct FDriverStanding
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString PlayerName;
    UPROPERTY(BlueprintReadOnly) int32   CurrentLap      = 1;
    UPROPERTY(BlueprintReadOnly) float   BestLapSeconds  = 0.0f;
    UPROPERTY(BlueprintReadOnly) float   LastLapSeconds  = 0.0f;
    UPROPERTY(BlueprintReadOnly) int32   Position        = 0;
    UPROPERTY(BlueprintReadOnly) float   GapToLeader     = 0.0f;
    UPROPERTY(BlueprintReadOnly) bool    bFinished       = false;
};

/**
 * ARacingGameState — Replicado a todos los clientes.
 * Contiene el estado global de la carrera: fase, timer, clasificación.
 */
UCLASS()
class SIMRACINGGAME_API ARacingGameState : public AGameState
{
    GENERATED_BODY()

public:
    ARacingGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Llamado desde GameMode (solo servidor)
    void SetRacePhase(ERacePhase NewPhase);
    void StartRaceTimer();
    void TickRaceTimer(float DeltaTime);
    void UpdateStandings(const TArray<FDriverStanding>& NewStandings);

    // Getters para UI (clientes)
    UFUNCTION(BlueprintCallable, Category = "Race")
    ERacePhase GetRacePhase() const { return RacePhase; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    float GetRaceTimeSeconds() const { return RaceTimeSeconds; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    const TArray<FDriverStanding>& GetStandings() const { return DriverStandings; }

protected:
    UPROPERTY(ReplicatedUsing = OnRep_RacePhase, BlueprintReadOnly, Category = "Race")
    ERacePhase RacePhase = ERacePhase::Lobby;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    float RaceTimeSeconds = 0.0f;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    bool bRaceTimerRunning = false;

    UPROPERTY(ReplicatedUsing = OnRep_Standings, BlueprintReadOnly, Category = "Race")
    TArray<FDriverStanding> DriverStandings;

    UFUNCTION()
    void OnRep_RacePhase();

    UFUNCTION()
    void OnRep_Standings();
};
