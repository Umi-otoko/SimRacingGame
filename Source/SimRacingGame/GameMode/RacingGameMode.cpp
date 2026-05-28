#include "RacingGameMode.h"
#include "RacingGameState.h"
#include "RacingPlayerState.h"
#include "RacingVehiclePawn.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ARacingGameMode::ARacingGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.016f;  // ~60 Hz tick del GameMode

    DefaultPawnClass    = ARacingVehiclePawn::StaticClass();
    GameStateClass      = ARacingGameState::StaticClass();
    PlayerStateClass    = ARacingPlayerState::StaticClass();
}

void ARacingGameMode::BeginPlay()
{
    Super::BeginPlay();

    // Inicializar la fase de lobby — esperar jugadores
    CurrentPhase = ERacePhase::Lobby;
    UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] Race initialized. Waiting for players. Max: %d"), MaxPlayers);
}

void ARacingGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    switch (CurrentPhase)
    {
    case ERacePhase::Countdown:
        TickCountdown(DeltaTime);
        break;
    case ERacePhase::Racing:
        BroadcastRaceState();
        break;
    default:
        break;
    }
}

void ARacingGameMode::StartRaceSequence()
{
    if (CurrentPhase != ERacePhase::Lobby) return;

    CurrentPhase = ERacePhase::Countdown;
    CountdownTimer = CountdownDuration;

    if (ARacingGameState* GS = GetGameState<ARacingGameState>())
    {
        GS->SetRacePhase(ERacePhase::Countdown);
    }

    UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] Countdown started: %.0f seconds"), CountdownDuration);
}

void ARacingGameMode::TickCountdown(float DeltaTime)
{
    CountdownTimer -= DeltaTime;
    if (CountdownTimer <= 0.0f)
    {
        CurrentPhase = ERacePhase::Racing;

        if (ARacingGameState* GS = GetGameState<ARacingGameState>())
        {
            GS->SetRacePhase(ERacePhase::Racing);
            GS->StartRaceTimer();
        }

        // Habilitar física de todos los vehículos (estaban bloqueados en countdown)
        for (TActorIterator<ARacingVehiclePawn> It(GetWorld()); It; ++It)
        {
            It->SetVehicleEnabled(true);
        }

        UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] RACE STARTED!"));
    }
}

bool ARacingGameMode::ValidateLapCompletion(ARacingVehiclePawn* Vehicle) const
{
    if (!Vehicle) return false;

    APlayerController* PC = Cast<APlayerController>(Vehicle->GetController());
    if (!PC) return false;

    const uint32* CheckpointBits = PlayerCheckpoints.Find(PC);
    if (!CheckpointBits) return false;

    // Verificar que todos los checkpoints fueron marcados
    uint32 RequiredMask = (1u << TotalCheckpoints) - 1u;
    return (*CheckpointBits & RequiredMask) == RequiredMask;
}

void ARacingGameMode::OnPlayerCrossedFinishLine(ARacingVehiclePawn* Vehicle, float LapTimeSeconds)
{
    if (CurrentPhase != ERacePhase::Racing) return;

    if (!ValidateLapCompletion(Vehicle))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RacingGameMode] Lap REJECTED — missing checkpoints for player %s"),
               *Vehicle->GetName());
        return;
    }

    SubmitValidatedLapTime(Vehicle, LapTimeSeconds);

    // Resetear checkpoints para la siguiente vuelta
    APlayerController* PC = Cast<APlayerController>(Vehicle->GetController());
    if (PC) PlayerCheckpoints.Add(PC, 0u);
}

void ARacingGameMode::SubmitValidatedLapTime(ARacingVehiclePawn* Vehicle, float LapTimeSeconds)
{
    ARacingPlayerState* PS = Vehicle->GetPlayerState<ARacingPlayerState>();
    if (!PS) return;

    const int64 LapTimeMs = static_cast<int64>(LapTimeSeconds * 1000.0f);
    PS->RegisterLapTime(static_cast<int64>(LapTimeMs));

    UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] VALID LAP — Player: %s | Time: %.3fs"),
           *PS->GetPlayerName(), LapTimeSeconds);

    // TODO: Firmar con HMAC y enviar al backend (AWS DynamoDB Leaderboard)
    // SubmitToLeaderboard(PS->GetSteamID(), LapTimeMs, GetWorld()->GetMapName());
}

void ARacingGameMode::BroadcastRaceState()
{
    // El GameState replica automáticamente a todos los clientes
    // Este tick actualiza el timer de carrera en el servidor
    if (ARacingGameState* GS = GetGameState<ARacingGameState>())
    {
        GS->TickRaceTimer(PrimaryActorTick.TickInterval);
    }
}
