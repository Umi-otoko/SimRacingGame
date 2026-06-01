#include "RacingAudio.h"

URacingAudio::URacingAudio(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;  // empieza a generar audio al BeginPlay
}

bool URacingAudio::Init(int32& SampleRate)
{
    // El audio engine nos pasa el SampleRate antes de empezar OnGenerateAudio
    CachedSR = SampleRate > 0 ? static_cast<float>(SampleRate) : 48000.0f;
    return true;  // true = listo para generar audio
}

// =============================================================================
// Game thread API
// =============================================================================

void URacingAudio::SetRPM(float RPM)
{
    // Frecuencia de encendido (Hz) = RPM/60 × cilindros/2
    // Se multiplica por 2 para mover el tono a un rango más audible y rico.
    const float FiringHz = (RPM / 60.0f) * static_cast<float>(NumCylinders) * 0.5f;
    TargetFreq = FMath::Max(18.0f, FiringHz);
}

void URacingAudio::SetThrottle(float Throttle)
{
    TargetVol = BaseVolume + FMath::Clamp(Throttle, 0.0f, 1.0f) * ThrottleVolumeBoost;
}

void URacingAudio::SetTireSlip(float MaxSlipRatio)
{
    // Chirrido empieza al 15% de slip, alcanza máximo al 65%
    const float Normalized = FMath::Clamp((MaxSlipRatio - 0.15f) / 0.50f, 0.0f, 1.0f);
    TargetSqueal = Normalized * MaxSquealVolume;
}

// =============================================================================
// Audio thread — síntesis
// =============================================================================

int32 URacingAudio::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
    // ---- Constantes de síntesis ----------------------------------------

    // Estructura armónica de un motor de 4 cilindros:
    //   1er armónico: frecuencia de encendido (golpe dominante)
    //   2do: octava (refuerza el cuerpo)
    //   3er/4to: sabor metálico
    static constexpr float HFreq[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    static constexpr float HAmp[4]  = { 0.42f, 0.32f, 0.16f, 0.10f };

    const float TwoPi = 2.0f * UE_PI;

    // Factores de suavizado por muestra (ventana ~10ms a 48 kHz → ÷ ~480)
    const float FreqAlpha   = 0.0018f;
    const float VolAlpha    = 0.0018f;
    const float SquealAlpha = 0.004f;

    for (int32 n = 0; n < NumSamples; ++n)
    {
        // Suavizar hacia valores objetivo para evitar clics
        CurrentFreq   += (TargetFreq   - CurrentFreq)   * FreqAlpha;
        CurrentVol    += (TargetVol    - CurrentVol)     * VolAlpha;
        CurrentSqueal += (TargetSqueal - CurrentSqueal)  * SquealAlpha;

        // ---- Motor (armónicos aditivos) --------------------------------
        float sample = 0.0f;
        const float BaseInc = TwoPi * CurrentFreq / CachedSR;

        for (int32 h = 0; h < 4; ++h)
        {
            sample     += HAmp[h] * FMath::Sin(Phases[h]);
            Phases[h]  += BaseInc * HFreq[h];
            // Mantener en [0, 2π) para evitar pérdida de precisión
            if (Phases[h] >= TwoPi) Phases[h] -= TwoPi;
        }
        sample *= CurrentVol;

        // ---- Chirrido de neumáticos (ruido blanco × envelope) ----------
        if (CurrentSqueal > 0.001f)
        {
            sample += NextNoise() * CurrentSqueal;
        }

        OutAudio[n] = FMath::Clamp(sample, -1.0f, 1.0f);
    }

    return NumSamples;
}
