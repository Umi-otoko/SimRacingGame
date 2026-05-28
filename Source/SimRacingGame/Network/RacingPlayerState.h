#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RacingPlayerState.generated.h"

/**
 * ARacingPlayerState — Estado por jugador, replicado a todos los clientes.
 * Almacena tiempos de vuelta, posición en carrera y estadísticas de la sesión.
 */
UCLASS()
class SIMRACINGGAME_API ARacingPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    ARacingPlayerState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Registra un tiempo de vuelta validado por el servidor
    void RegisterLapTime(int64 LapTimeMs);

    // Iniciar/finalizar medición del lap actual
    void StartLapTimer();
    float GetCurrentLapTime() const;  // Segundos transcurridos del lap actual

    // Getters para UI
    UFUNCTION(BlueprintCallable, Category = "Race")
    float GetBestLapSeconds() const { return BestLapMs > 0 ? BestLapMs / 1000.0f : 0.0f; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    float GetLastLapSeconds() const { return LastLapMs / 1000.0f; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    int32 GetCurrentLap() const { return CurrentLap; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    int32 GetRacePosition() const { return RacePosition; }

    UFUNCTION(BlueprintCallable, Category = "Race")
    bool HasFinishedRace() const { return bFinished; }

    // Llamado por GameMode al finalizar la carrera
    void SetFinished(int32 FinalPosition);

    // SteamID64 del jugador (para leaderboards)
    FString SteamID64;

protected:
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    int32 CurrentLap = 1;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    int32 RacePosition = 0;

    // int64 en lugar de uint64 — Blueprint no soporta uint64
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    int64 BestLapMs = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    int64 LastLapMs = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Race")
    bool bFinished = false;

    // No replicado — solo en el servidor que mide el tiempo
    double LapStartTimeSeconds = 0.0;
    TArray<int64> AllLapTimes;  // Historial de vueltas de esta sesión
};
