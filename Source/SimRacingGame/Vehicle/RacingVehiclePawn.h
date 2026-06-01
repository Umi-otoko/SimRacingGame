#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "RacingVehiclePawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class URacingVehicleMovement;
class URacingAudio;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

/**
 * ARacingVehiclePawn
 * Vehículo principal del sim-racing. Usa ChaosVehicles como base
 * e integra nuestro modelo de neumáticos Pacejka mediante
 * URacingVehicleMovement (subclase de UChaosWheeledVehicleMovementComponent).
 *
 * Replicación:
 * - El servidor tiene autoridad sobre las físicas.
 * - El cliente aplica Client-Side Prediction y reconcilia con snapshots del servidor.
 */
UCLASS(Blueprintable)
class SIMRACINGGAME_API ARacingVehiclePawn : public AWheeledVehiclePawn
{
    GENERATED_BODY()

public:
    ARacingVehiclePawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Habilitar/deshabilitar control (bloqueado durante countdown)
    UFUNCTION(BlueprintCallable, Category = "Vehicle")
    void SetVehicleEnabled(bool bEnabled);

    // Notificación de cruce de checkpoint (llamada por trigger volume en el nivel)
    UFUNCTION(BlueprintCallable, Category = "Race")
    void NotifyCheckpointCrossed(int32 CheckpointIndex);

    // Notificación de cruce de línea de meta
    UFUNCTION(BlueprintCallable, Category = "Race")
    void NotifyFinishLineCrossed();

    // Acceso al componente de movimiento especializado
    UFUNCTION(BlueprintCallable, Category = "Vehicle")
    URacingVehicleMovement* GetRacingMovement() const;

protected:
    // ---- Componentes ----
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Camera")
    USpringArmComponent* ChaseCameraArm;

    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Camera")
    UCameraComponent* ChaseCamera;

    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Camera")
    UCameraComponent* CockpitCamera;

    // ---- Audio (síntesis procedural — no requiere assets externos) ----
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Audio")
    URacingAudio* AudioComp;

    // ---- Input (Enhanced Input) ----
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* RacingMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Throttle;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Brake;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Steering;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Handbrake;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_GearUp;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_GearDown;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_SwitchCamera;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_ResetVehicle;

    // ---- Estado replicado ----
    // Velocidad replicada para que otros clientes puedan interpolar
    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSpeed, BlueprintReadOnly, Category = "Vehicle")
    float ReplicatedSpeedKmh = 0.0f;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vehicle")
    int32 ReplicatedGear = 1;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vehicle")
    float ReplicatedEngineRPM = 0.0f;

    // ---- Control ----
    bool bVehicleEnabled = false;
    bool bUsingCockpitCamera = false;

    // ---- Laptime local (en el servidor mide el tiempo para la vuelta) ----
    double LapStartTimeLocal = 0.0;

    // ---- Audio state (actualizado en input handlers, leído en Tick) ----
    float CurrentThrottle = 0.0f;

    // ---- Fallback auto-enable (por si el GameMode no llama SetVehicleEnabled) ----
    FTimerHandle EnableFallbackTimer;
    // Habilita el vehículo si todavía está desactivado cuando suena el timer
    void TryAutoEnable();

    // ---- Input handlers ----
    void Input_Throttle(const FInputActionValue& Value);
    void Input_Brake(const FInputActionValue& Value);
    void Input_Steering(const FInputActionValue& Value);
    void Input_Handbrake(const FInputActionValue& Value);
    void Input_GearUp(const FInputActionValue& Value);
    void Input_GearDown(const FInputActionValue& Value);
    void Input_SwitchCamera(const FInputActionValue& Value);
    void Input_ResetVehicle(const FInputActionValue& Value);

    // ---- RPCs ----
    UFUNCTION(Server, Reliable)
    void Server_GearUp();

    UFUNCTION(Server, Reliable)
    void Server_GearDown();

    UFUNCTION(Server, Reliable)
    void Server_ResetVehicle();

    UFUNCTION()
    void OnRep_ReplicatedSpeed();

    // Actualiza los valores replicados (llamado en el servidor cada tick)
    void UpdateReplicatedState();
};
