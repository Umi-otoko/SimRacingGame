#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RacingTypes.h"
#include "RacingHUD.generated.h"

class URacingVehicleMovement;
class ARacingVehiclePawn;
class ARacingGameState;
class ARacingPlayerState;

/**
 * ARacingHUD — Pure-C++ canvas HUD.
 * Draws speed, gear, RPM bar, tire temps, G-force ball, lap timer, countdown.
 * No Blueprint or UMG assets required — fully runtime-constructed.
 */
UCLASS()
class SIMRACINGGAME_API ARacingHUD : public AHUD
{
    GENERATED_BODY()

public:
    ARacingHUD();

    virtual void DrawHUD() override;
    virtual void Tick(float DeltaTime) override;

private:
    UPROPERTY()
    UFont* HUDFont = nullptr;

    ERacePhase LastPhase  = ERacePhase::Lobby;
    float      GoTimer    = 0.0f;   // seconds left to show "GO!"

    // ---- drawing passes ----
    void DrawSpeedGear(URacingVehicleMovement* VM, float W, float H, float S);
    void DrawRPMBar(URacingVehicleMovement* VM, float W, float H, float S);
    void DrawTireStatus(URacingVehicleMovement* VM, float W, float H, float S);
    void DrawLapInfo(ARacingPlayerState* PS, ARacingGameState* GS, float W, float H, float S);
    void DrawCountdown(float SecsRemaining, float W, float H, float S);
    void DrawGoFlash(float W, float H, float S);
    void DrawGForceBall(URacingVehicleMovement* VM, float W, float H, float S);

    // ---- helpers ----
    static FLinearColor TempToColor(float TempC);
    static FLinearColor WearToColor(float Wear);
    static FString      FormatLapTime(float Secs);

    // Thin wrapper that multiplies scale by S so the HUD looks the same at any resolution
    void ScaledText(const FString& Text, FLinearColor Color,
                    float X, float Y, float Scale, float S);
};
