#include "RacingGameMode.h"
#include "RacingGameState.h"
#include "RacingPlayerState.h"
#include "RacingVehiclePawn.h"
#include "RacingCheckpoint.h"
#include "RacingHUD.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ARacingGameMode::ARacingGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.016f;  // ~60 Hz tick del GameMode

    // Usar el Blueprint que tiene la mesh asignada; C++ como fallback si no existe
    static ConstructorHelpers::FClassFinder<APawn> BPVehicle(
        TEXT("/Game/SimRacing/Blueprints/BP_RacingVehicle"));
    if (BPVehicle.Succeeded())
        DefaultPawnClass = BPVehicle.Class;
    else
        DefaultPawnClass = ARacingVehiclePawn::StaticClass();

    GameStateClass   = ARacingGameState::StaticClass();
    PlayerStateClass = ARacingPlayerState::StaticClass();
    HUDClass         = ARacingHUD::StaticClass();
}

void ARacingGameMode::BeginPlay()
{
    Super::BeginPlay();

    CurrentPhase = ERacePhase::Lobby;

    // Auto-detectar cuántos checkpoints hay en el nivel (colocados por el Python script)
    int32 Found = 0;
    for (TActorIterator<ARacingCheckpoint> It(GetWorld()); It; ++It)
        ++Found;
    if (Found > 0)
    {
        TotalCheckpoints = Found;
        UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] %d checkpoint(s) detectados en el nivel."), TotalCheckpoints);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[RacingGameMode] No se encontraron checkpoints — usar TotalCheckpoints por defecto (%d). "
                    "Ejecuta SetupRacingGame.py para colocarlos."), TotalCheckpoints);
    }

    UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] Race initialized. Max players: %d | Checkpoints: %d"),
           MaxPlayers, TotalCheckpoints);

    // Auto-start: inicia countdown inmediatamente para que el vehículo se habilite
    StartRaceSequence();
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
        GS->SetCountdownSeconds(CountdownDuration);
    }

    UE_LOG(LogTemp, Log, TEXT("[RacingGameMode] Countdown started: %.0f seconds"), CountdownDuration);
}

void ARacingGameMode::TickCountdown(float DeltaTime)
{
    CountdownTimer -= DeltaTime;

    if (ARacingGameState* GS = GetGameState<ARacingGameState>())
        GS->SetCountdownSeconds(FMath::Max(0.0f, CountdownTimer));

    if (CountdownTimer <= 0.0f)
    {
        CurrentPhase = ERacePhase::Racing;

        if (ARacingGameState* GS = GetGameState<ARacingGameState>())
        {
            GS->SetRacePhase(ERacePhase::Racing);
            GS->StartRaceTimer();
            GS->SetCountdownSeconds(0.0f);

            for (APlayerState* PS : GS->PlayerArray)
            {
                if (ARacingPlayerState* RPS = Cast<ARacingPlayerState>(PS))
                    RPS->StartLapTimer();
            }
        }

        // Enable all vehicles
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

void ARacingGameMode::RegisterCheckpointCrossed(APlayerController* PC, int32 CheckpointIndex)
{
    if (!PC || CurrentPhase != ERacePhase::Racing) return;
    if (CheckpointIndex < 0 || CheckpointIndex >= 32) return;  // Seguridad: máx 32 checkpoints

    uint32& Bits = PlayerCheckpoints.FindOrAdd(PC, 0u);
    Bits |= (1u << static_cast<uint32>(CheckpointIndex));

    UE_LOG(LogTemp, Verbose,
           TEXT("[RacingGameMode] CP%d → %s | bits: 0x%08X"),
           CheckpointIndex, *PC->GetName(), Bits);
}
