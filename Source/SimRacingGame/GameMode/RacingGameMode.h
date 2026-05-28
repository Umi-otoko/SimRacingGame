#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RacingTypes.h"
#include "RacingGameMode.generated.h"

class ARacingVehiclePawn;
class ARacingGameState;
class ARacingCheckpoint;

/**
 * ARacingGameMode
 * Solo existe en el servidor (GameMode no se replica a clientes).
 * Controla las fases de la carrera, validación de tiempos de vuelta
 * y autoridad sobre el estado de la carrera.
 */
UCLASS()
class SIMRACINGGAME_API ARacingGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARacingGameMode();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // Llamado cuando un jugador cruza la línea de meta (servidor)
    void OnPlayerCrossedFinishLine(ARacingVehiclePawn* Vehicle, float LapTimeSeconds);

    // Validación de vuelta: verifica que el jugador pasó todos los checkpoints
    bool ValidateLapCompletion(ARacingVehiclePawn* Vehicle) const;

    // Registra que un jugador cruzó un checkpoint en esta vuelta
    void RegisterCheckpointCrossed(APlayerController* PC, int32 CheckpointIndex);

    // Inicia la carrera (transición Lobby → Countdown → Racing)
    UFUNCTION(BlueprintCallable, Category = "Race")
    void StartRaceSequence();

    UPROPERTY(EditDefaultsOnly, Category = "Race")
    int32 TotalLaps = 5;

    UPROPERTY(EditDefaultsOnly, Category = "Race")
    int32 MaxPlayers = 16;

    UPROPERTY(EditDefaultsOnly, Category = "Race")
    float CountdownDuration = 3.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Race")
    int32 TotalCheckpoints = 8;

protected:
    UPROPERTY()
    ERacePhase CurrentPhase = ERacePhase::Lobby;

    float CountdownTimer = 0.0f;

    // Mapa de checkpoints alcanzados por jugador (player_id → bitfield)
    TMap<APlayerController*, uint32> PlayerCheckpoints;

    void TickCountdown(float DeltaTime);
    void BroadcastRaceState();

    // Genera y firma un LapTimeRecord con HMAC del servidor
    void SubmitValidatedLapTime(ARacingVehiclePawn* Vehicle, float LapTimeSeconds);
};
