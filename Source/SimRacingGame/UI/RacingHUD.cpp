#include "RacingHUD.h"
#include "RacingVehiclePawn.h"
#include "RacingVehicleMovement.h"
#include "RacingGameState.h"
#include "RacingPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"

ARacingHUD::ARacingHUD()
{
    PrimaryActorTick.bCanEverTick = true;

    static ConstructorHelpers::FObjectFinder<UFont>
        F_Font(TEXT("/Engine/EngineFonts/Roboto"));
    if (F_Font.Succeeded())
        HUDFont = F_Font.Object;
}

void ARacingHUD::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (GoTimer > 0.0f)
        GoTimer -= DeltaTime;
}

void ARacingHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;

    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;
    const float S = H / 1080.0f;   // uniform scale: 1.0 at 1080p

    ARacingVehiclePawn* Vehicle = Cast<ARacingVehiclePawn>(GetOwningPawn());
    URacingVehicleMovement* VM  = Vehicle ? Vehicle->GetRacingMovement() : nullptr;

    ARacingGameState* GS = GetWorld() ? GetWorld()->GetGameState<ARacingGameState>() : nullptr;
    const ERacePhase Phase = GS ? GS->GetRacePhase() : ERacePhase::Lobby;

    APlayerController* PC = GetOwningPlayerController();
    ARacingPlayerState* PS = PC ? PC->GetPlayerState<ARacingPlayerState>() : nullptr;

    // Detect Racing phase transition → trigger "GO!" flash
    if (Phase == ERacePhase::Racing && LastPhase == ERacePhase::Countdown)
        GoTimer = 2.0f;
    LastPhase = Phase;

    // ---- Telemetry (always shown while in vehicle) ----
    if (VM)
    {
        DrawSpeedGear(VM, W, H, S);
        DrawRPMBar(VM, W, H, S);
        DrawTireStatus(VM, W, H, S);
        DrawGForceBall(VM, W, H, S);
    }

    DrawLapInfo(PS, GS, W, H, S);

    // ---- Overlays ----
    if (Phase == ERacePhase::Countdown && GS)
        DrawCountdown(GS->GetCountdownSecondsRemaining(), W, H, S);
    else if (GoTimer > 0.0f)
        DrawGoFlash(W, H, S);
}

// =============================================================================
// Speed + gear panel — bottom right
// =============================================================================

void ARacingHUD::DrawSpeedGear(URacingVehicleMovement* VM, float W, float H, float S)
{
    const float SpeedKmh = FMath::Abs(VM->GetForwardSpeedMPH() * 1.60934f);
    const int32 SpeedInt = FMath::RoundToInt(SpeedKmh);
    const int32 Gear     = VM->GetCurrentGear();

    const float PX = W - 270.0f * S;
    const float PY = H - 155.0f * S;
    const float PW = 260.0f * S;
    const float PH = 145.0f * S;

    // Panel background
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.60f), PX, PY, PW, PH);

    // Gear indicator (left side of panel)
    FString GearStr;
    FLinearColor GearColor;
    if (Gear > 0)  { GearStr = FString::Printf(TEXT("%d"), Gear); GearColor = FLinearColor(1.0f, 0.75f, 0.0f, 1.0f); }
    else if (Gear == 0) { GearStr = TEXT("N"); GearColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.0f); }
    else               { GearStr = TEXT("R"); GearColor = FLinearColor(1.0f, 0.3f, 0.3f, 1.0f); }

    ScaledText(GearStr, GearColor, PX + 8.0f * S, PY + 10.0f * S, 5.5f, S);

    // Speed number
    ScaledText(FString::Printf(TEXT("%d"), SpeedInt),
               FLinearColor::White, PX + 90.0f * S, PY + 8.0f * S, 4.5f, S);

    // km/h unit
    ScaledText(TEXT("km/h"), FLinearColor(0.6f, 0.6f, 0.6f, 1.0f),
               PX + 95.0f * S, PY + 100.0f * S, 1.3f, S);
}

// =============================================================================
// RPM bar — bottom center
// =============================================================================

void ARacingHUD::DrawRPMBar(URacingVehicleMovement* VM, float W, float H, float S)
{
    const float RPM        = VM->GetEngineRotationSpeed();
    const float MaxRPM     = 7000.0f;
    const float RPMRatio   = FMath::Clamp(RPM / MaxRPM, 0.0f, 1.0f);
    const float RedlineAt  = 0.85f;

    const float BX = W * 0.2f;
    const float BY = H - 30.0f * S;
    const float BW = W * 0.6f;
    const float BH = 20.0f * S;

    // Track
    DrawRect(FLinearColor(0.12f, 0.12f, 0.12f, 0.80f), BX, BY, BW, BH);

    // Fill color ramps green → yellow → red
    FLinearColor BarColor;
    if      (RPMRatio < 0.6f)       BarColor = FLinearColor(0.1f, 0.85f, 0.2f, 1.0f);
    else if (RPMRatio < RedlineAt)  BarColor = FMath::Lerp(FLinearColor(0.8f,0.6f,0.0f,1.0f), FLinearColor(1.0f,0.3f,0.0f,1.0f), (RPMRatio - 0.6f) / (RedlineAt - 0.6f));
    else                            BarColor = FLinearColor(1.0f, 0.05f, 0.05f, 1.0f);

    DrawRect(BarColor, BX, BY, BW * RPMRatio, BH);

    // Redline marker
    DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), BX + BW * RedlineAt, BY, 2.0f * S, BH);

    // RPM label
    ScaledText(FString::Printf(TEXT("%.0f RPM"), RPM),
               FLinearColor(0.75f, 0.75f, 0.75f, 1.0f),
               BX, BY - 20.0f * S, 1.1f, S);
}

// =============================================================================
// Tire status — bottom left (2×2 grid FL/FR/RL/RR)
// =============================================================================

void ARacingHUD::DrawTireStatus(URacingVehicleMovement* VM, float W, float H, float S)
{
    const float BoxW = 58.0f * S;
    const float BoxH = 68.0f * S;
    const float Gap  = 8.0f  * S;
    const float BX   = 15.0f * S;
    const float BY   = H - (BoxH * 2.0f + Gap + 10.0f * S);

    // Panel background
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.60f),
             BX - 5.0f * S, BY - 5.0f * S,
             (BoxW * 2.0f + Gap + 10.0f * S), (BoxH * 2.0f + Gap + 10.0f * S));

    // Wheel order: 0=FL, 1=FR, 2=RL, 3=RR
    const TCHAR* Labels[4] = { TEXT("FL"), TEXT("FR"), TEXT("RL"), TEXT("RR") };
    // Column / row for each wheel
    const int32 Col[4] = { 0, 1, 0, 1 };
    const int32 Row[4] = { 0, 0, 1, 1 };

    for (int32 i = 0; i < 4; ++i)
    {
        const float X = BX + Col[i] * (BoxW + Gap);
        const float Y = BY + Row[i] * (BoxH + Gap);

        const float Temp = VM->GetTireTemperature(i);
        const float Wear = VM->GetTireWear(i);

        const FLinearColor TempColor = TempToColor(Temp);
        const FLinearColor WearColor = WearToColor(Wear);
        const float TempBarH = BoxH * 0.80f;

        // Temperature colored box
        DrawRect(TempColor * 0.65f + FLinearColor(0.05f, 0.05f, 0.05f, 0.0f),
                 X, Y, BoxW, TempBarH);

        // Wear bar (bottom strip — full width = new, shrinks left = worn)
        DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 0.9f),
                 X, Y + TempBarH, BoxW, BoxH - TempBarH);
        DrawRect(WearColor, X, Y + TempBarH, BoxW * (1.0f - Wear), BoxH - TempBarH);

        // Labels
        ScaledText(Labels[i], FLinearColor::White, X + 4.0f * S, Y + 4.0f * S, 1.0f, S);
        ScaledText(FString::Printf(TEXT("%.0f°"), Temp),
                   FLinearColor::White, X + 4.0f * S, Y + 28.0f * S, 0.95f, S);
    }
}

// =============================================================================
// Lap info — top left
// =============================================================================

void ARacingHUD::DrawLapInfo(ARacingPlayerState* PS, ARacingGameState* GS, float W, float H, float S)
{
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 10.0f * S, 10.0f * S, 310.0f * S, 90.0f * S);

    if (GS)
    {
        // Race elapsed timer (top line, large)
        const float RaceTime = GS->GetRaceTimeSeconds();
        ScaledText(FString::Printf(TEXT("RACE  %s"), *FormatLapTime(RaceTime)),
                   FLinearColor::White, 18.0f * S, 16.0f * S, 1.4f, S);
    }

    if (PS)
    {
        const float Best = PS->GetBestLapSeconds();
        const float Last = PS->GetLastLapSeconds();
        const int32 Lap  = PS->GetCurrentLap();

        ScaledText(FString::Printf(TEXT("LAP %d"), Lap),
                   FLinearColor(0.6f, 0.85f, 1.0f, 1.0f), 18.0f * S, 46.0f * S, 1.1f, S);

        if (Best > 0.0f)
            ScaledText(FString::Printf(TEXT("BEST  %s"), *FormatLapTime(Best)),
                       FLinearColor(0.4f, 1.0f, 0.45f, 1.0f), 18.0f * S, 68.0f * S, 1.1f, S);
        else if (Last > 0.0f)
            ScaledText(FString::Printf(TEXT("LAST  %s"), *FormatLapTime(Last)),
                       FLinearColor(0.9f, 0.9f, 0.9f, 1.0f), 18.0f * S, 68.0f * S, 1.1f, S);
    }
}

// =============================================================================
// Countdown overlay — center screen
// =============================================================================

void ARacingHUD::DrawCountdown(float SecsRemaining, float W, float H, float S)
{
    // Dim the screen slightly
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.38f), 0.0f, 0.0f, W, H);

    const int32 Count = FMath::CeilToInt(SecsRemaining);
    const FString Str = Count > 0 ? FString::Printf(TEXT("%d"), Count) : TEXT("GO!");

    // Pulsing scale based on fractional part
    const float Frac  = FMath::Fmod(SecsRemaining, 1.0f);
    const float Scale = FMath::Lerp(10.0f, 7.5f, Frac);

    FLinearColor Color = Count > 0 ? FLinearColor(1.0f, 0.80f, 0.0f, 1.0f)
                                   : FLinearColor(0.2f, 1.0f, 0.3f, 1.0f);

    // Rough centering: each glyph at scale=1 is ~9px wide at Roboto default
    const float GlyphW  = 9.0f * Scale * S;
    const float CenterX = W * 0.5f - GlyphW;
    const float CenterY = H * 0.38f;

    ScaledText(Str, Color, CenterX, CenterY, Scale, S);
}

// =============================================================================
// "GO!" flash after countdown ends
// =============================================================================

void ARacingHUD::DrawGoFlash(float W, float H, float S)
{
    const float Alpha  = FMath::Clamp(GoTimer / 2.0f, 0.0f, 1.0f);
    const float Scale  = FMath::Lerp(6.0f, 12.0f, Alpha);
    const FLinearColor Color(0.2f, 1.0f, 0.3f, Alpha);

    const float GlyphW  = 9.0f * Scale * S * 3.0f; // "GO!" is 3 glyphs
    ScaledText(TEXT("GO!"), Color, W * 0.5f - GlyphW, H * 0.38f, Scale, S);
}

// =============================================================================
// G-force ball — right side, mid-height
// =============================================================================

void ARacingHUD::DrawGForceBall(URacingVehicleMovement* VM, float W, float H, float S)
{
    const float CX = W - 80.0f * S;
    const float CY = H - 290.0f * S;
    const float R  = 45.0f * S;

    // Background circle (approximate with square)
    DrawRect(FLinearColor(0.08f, 0.08f, 0.08f, 0.70f), CX - R, CY - R, R * 2.0f, R * 2.0f);

    // Crosshairs
    DrawRect(FLinearColor(0.3f, 0.3f, 0.3f, 0.60f), CX - R, CY, R * 2.0f, 1.5f * S);
    DrawRect(FLinearColor(0.3f, 0.3f, 0.3f, 0.60f), CX, CY - R, 1.5f * S, R * 2.0f);

    // Ball position
    const float MaxG = 3.0f;
    const float GLat  = FMath::Clamp(VM->GetGForceLateral(),       -MaxG, MaxG) / MaxG;
    const float GLong = FMath::Clamp(VM->GetGForceLongitudinal(),   -MaxG, MaxG) / MaxG;

    const float BX = CX + GLat  * R;
    const float BY = CY - GLong * R;

    // Intensity-based color
    const float GTotal = FMath::Sqrt(GLat * GLat + GLong * GLong);
    FLinearColor BallColor = FMath::Lerp(FLinearColor(0.2f, 0.6f, 1.0f, 1.0f),
                                         FLinearColor(1.0f, 0.2f, 0.0f, 1.0f), GTotal);
    const float BD = 8.0f * S;
    DrawRect(BallColor, BX - BD * 0.5f, BY - BD * 0.5f, BD, BD);

    // Label
    ScaledText(TEXT("G"), FLinearColor(0.5f, 0.5f, 0.5f, 0.8f),
               CX - 5.0f * S, CY + R + 4.0f * S, 1.0f, S);
}

// =============================================================================
// Helpers
// =============================================================================

void ARacingHUD::ScaledText(const FString& Text, FLinearColor Color,
                             float X, float Y, float Scale, float S)
{
    DrawText(Text, Color, X, Y, HUDFont, Scale * S, false);
}

FLinearColor ARacingHUD::TempToColor(float T)
{
    if (T < 60.0f)  return FLinearColor(0.0f, 0.35f, 1.0f, 1.0f);  // cold blue
    if (T < 90.0f)  return FMath::Lerp(FLinearColor(0.0f, 0.35f, 1.0f, 1.0f),
                                        FLinearColor(0.1f, 0.90f, 0.2f, 1.0f),
                                        (T - 60.0f) / 30.0f);       // blue → green
    if (T < 110.0f) return FMath::Lerp(FLinearColor(0.1f, 0.90f, 0.2f, 1.0f),
                                        FLinearColor(1.0f, 0.80f, 0.0f, 1.0f),
                                        (T - 90.0f) / 20.0f);       // green → yellow
    if (T < 140.0f) return FMath::Lerp(FLinearColor(1.0f, 0.80f, 0.0f, 1.0f),
                                        FLinearColor(1.0f, 0.10f, 0.0f, 1.0f),
                                        (T - 110.0f) / 30.0f);      // yellow → red
    return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);                    // overheating red
}

FLinearColor ARacingHUD::WearToColor(float Wear)
{
    if (Wear < 0.5f) return FMath::Lerp(FLinearColor(0.1f, 0.90f, 0.15f, 1.0f),
                                          FLinearColor(1.0f, 0.75f, 0.0f,  1.0f),
                                          Wear * 2.0f);
    return FMath::Lerp(FLinearColor(1.0f, 0.75f, 0.0f, 1.0f),
                       FLinearColor(1.0f, 0.10f, 0.0f, 1.0f),
                       (Wear - 0.5f) * 2.0f);
}

FString ARacingHUD::FormatLapTime(float Secs)
{
    if (Secs <= 0.0f) return TEXT("--:--.---");
    const int32 Min = FMath::FloorToInt(Secs / 60.0f);
    const float Sec = FMath::Fmod(Secs, 60.0f);
    return FString::Printf(TEXT("%d:%06.3f"), Min, Sec);
}
