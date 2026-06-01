#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "RacingAudio.generated.h"

/**
 * URacingAudio — Síntesis FM procedural de motor + chirrido de neumáticos.
 * No requiere ningún asset de audio. Corre en el audio thread.
 *
 * Uso desde RacingVehiclePawn cada tick:
 *   SetRPM(float)       — RPM actual del motor
 *   SetThrottle(float)  — 0..1
 *   SetTireSlip(float)  — max slip ratio de las 4 ruedas (0..1)
 */
UCLASS(ClassGroup = Audio, meta = (BlueprintSpawnableComponent))
class SIMRACINGGAME_API URacingAudio : public USynthComponent
{
    GENERATED_BODY()

public:
    URacingAudio();

    /** Llamado cada game tick desde el vehicle pawn */
    void SetRPM(float RPM);
    void SetThrottle(float Throttle);
    void SetTireSlip(float MaxSlipRatio);

    /** Número de cilindros (afecta la frecuencia de encendido) */
    UPROPERTY(EditDefaultsOnly, Category = "Engine Audio",
              meta = (ClampMin = 1, ClampMax = 12))
    int32 NumCylinders = 4;

    /** Volumen base (sin acelerador) */
    UPROPERTY(EditDefaultsOnly, Category = "Engine Audio",
              meta = (ClampMin = 0.0f, ClampMax = 1.0f))
    float BaseVolume = 0.40f;

    /** Cuánto sube el volumen al pisar el acelerador */
    UPROPERTY(EditDefaultsOnly, Category = "Engine Audio",
              meta = (ClampMin = 0.0f, ClampMax = 1.0f))
    float ThrottleVolumeBoost = 0.22f;

    /** Volumen máximo del chirrido de neumáticos */
    UPROPERTY(EditDefaultsOnly, Category = "Tire Audio",
              meta = (ClampMin = 0.0f, ClampMax = 1.0f))
    float MaxSquealVolume = 0.28f;

protected:
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
    // Init recibe el SampleRate del audio engine antes de empezar a generar
    virtual bool  Init(int32& SampleRate) override;

private:
    // ---- Shared (written on game thread, read on audio thread) ----
    // Using volatile so the compiler doesn't cache stale values.
    // On x86 a float write/read is atomic enough for audio interpolation.
    volatile float TargetFreq   = 40.0f;
    volatile float TargetVol    = 0.40f;
    volatile float TargetSqueal = 0.0f;

    // ---- Audio thread state (only touched in OnGenerateAudio) ----
    float CurrentFreq   = 40.0f;
    float CurrentVol    = 0.40f;
    float CurrentSqueal = 0.0f;
    float CachedSR      = 48000.0f;    // sample rate
    float Phases[4]     = {};           // phase accumulators for 4 harmonics

    // Lightweight xorshift32 noise (deterministic, cheap)
    uint32 NoiseReg = 0xDEADC0DE;
    FORCEINLINE float NextNoise()
    {
        NoiseReg ^= NoiseReg << 13;
        NoiseReg ^= NoiseReg >> 17;
        NoiseReg ^= NoiseReg <<  5;
        return static_cast<float>(static_cast<int32>(NoiseReg))
               * (1.0f / 2147483648.0f);
    }
};
