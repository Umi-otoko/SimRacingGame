#pragma once

#include "CoreMinimal.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Physics/TireModel.h"
#include "RacingVehicleMovement.generated.h"

/**
 * URacingVehicleMovement
 * Extiende UChaosWheeledVehicleMovementComponent para integrar nuestro
 * modelo de neumáticos Pacejka MF5.2 (de Physics/TireModel.h).
 *
 * La integración funciona así:
 * - Chaos calcula la dinámica del chasis, suspensión y colisiones.
 * - Nuestro Pacejka sobreescribe las fuerzas de contacto de los neumáticos
 *   antes de que Chaos aplique los impulsos al cuerpo rígido.
 * - Esto se hace en el callback OnApplyTireForces().
 *
 * El physics tick corre a 100 Hz gracias a la configuración en DefaultEngine.ini:
 *   MaxPhysicsSubstepDeltaTime=0.010000, MaxSubstepCount=8
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SIMRACINGGAME_API URacingVehicleMovement : public UChaosWheeledVehicleMovementComponent
{
    GENERATED_BODY()

public:
    URacingVehicleMovement();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ---- Parámetros del neumático ajustables desde Blueprint/Setup ----

    // Coeficientes Pacejka para neumáticos delanteros
    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_Bx_Front = 10.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_Cx_Front = 1.65f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_Dx_Front = 1.10f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_By_Front = 8.50f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_Cy_Front = 1.30f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Front")
    float Pacejka_Dy_Front = 1.05f;

    // Coeficientes Pacejka para neumáticos traseros
    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_Bx_Rear = 9.5f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_Cx_Rear = 1.65f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_Dx_Rear = 1.08f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_By_Rear = 8.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_Cy_Rear = 1.28f;

    UPROPERTY(EditDefaultsOnly, Category = "Tire Model|Rear")
    float Pacejka_Dy_Rear = 1.03f;

    // Temperatura inicial de los neumáticos
    UPROPERTY(EditDefaultsOnly, Category = "Tire Model")
    float InitialTireTemp = 60.0f;

    // ---- Estado de los neumáticos (lectura para HUD/telemetría) ----

    UFUNCTION(BlueprintCallable, Category = "Tire")
    float GetTireTemperature(int32 WheelIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Tire")
    float GetTireWear(int32 WheelIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Tire")
    float GetTireLateralSlip(int32 WheelIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Tire")
    float GetTireLongitudinalSlip(int32 WheelIndex) const;

    // ---- Telemetría completa para transmitir al HUD ----
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    float GetGForceLateral() const   { return GForce_Lateral; }

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    float GetGForceLongitudinal() const { return GForce_Longitudinal; }

    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    float GetUndersteerAngleDeg() const { return UndersteerAngleDeg; }

protected:
    // Modelos de neumático para eje delantero/trasero
    TUniquePtr<SimRacing::PacejkaTireModel> FrontTireModel;
    TUniquePtr<SimRacing::PacejkaTireModel> RearTireModel;

    // Estado térmico de las 4 ruedas [FL, FR, RL, RR]
    SimRacing::TireTemperatureModel::TireThermo WheelThermo[4];

    // Slips y fuerzas actuales (para telemetría y HUD)
    float WheelLateralSlip[4]     = {};
    float WheelLongitudinalSlip[4] = {};
    float WheelLoad[4]            = {};

    // G-forces actuales (calculados en TickComponent)
    float GForce_Lateral      = 0.0f;
    float GForce_Longitudinal = 0.0f;
    float UndersteerAngleDeg  = 0.0f;

    FVector PrevVelocityWorld = FVector::ZeroVector;

    // Inicializa los modelos Pacejka con los parámetros del componente
    void InitTireModels();

    // Calcula las fuerzas Pacejka y las aplica como impulsos en cada substep
    void UpdatePacejkaTireForces(float SubstepDt);

    // Calcula G-forces para el HUD
    void UpdateGForces(float DeltaTime);
};
