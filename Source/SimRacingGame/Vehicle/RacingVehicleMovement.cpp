#include "RacingVehicleMovement.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

URacingVehicleMovement::URacingVehicleMovement()
{
    PrimaryComponentTick.bCanEverTick = true;

    // Inicializar estado térmico de ruedas
    for (int32 i = 0; i < 4; ++i)
    {
        WheelThermo[i].surface_temp = InitialTireTemp;
        WheelThermo[i].core_temp    = InitialTireTemp - 10.0f;
        WheelThermo[i].wear_factor  = 0.0f;
    }
}

void URacingVehicleMovement::BeginPlay()
{
    Super::BeginPlay();
    InitTireModels();
}

void URacingVehicleMovement::InitTireModels()
{
    // Coeficientes delanteros
    SimRacing::PacejkaCoefficients FrontCoeffs;
    FrontCoeffs.Bx = Pacejka_Bx_Front;
    FrontCoeffs.Cx = Pacejka_Cx_Front;
    FrontCoeffs.Dx = Pacejka_Dx_Front;
    FrontCoeffs.By = Pacejka_By_Front;
    FrontCoeffs.Cy = Pacejka_Cy_Front;
    FrontCoeffs.Dy = Pacejka_Dy_Front;
    FrontCoeffs.Fz_nominal_N = 3200.0f;
    FrontCoeffs.temp_peak_celsius = 90.0f;
    FrontTireModel = MakeUnique<SimRacing::PacejkaTireModel>(FrontCoeffs);

    // Coeficientes traseros
    SimRacing::PacejkaCoefficients RearCoeffs;
    RearCoeffs.Bx = Pacejka_Bx_Rear;
    RearCoeffs.Cx = Pacejka_Cx_Rear;
    RearCoeffs.Dx = Pacejka_Dx_Rear;
    RearCoeffs.By = Pacejka_By_Rear;
    RearCoeffs.Cy = Pacejka_Cy_Rear;
    RearCoeffs.Dy = Pacejka_Dy_Rear;
    RearCoeffs.Fz_nominal_N = 3800.0f;  // Trasero carga más por distribución de peso
    RearCoeffs.temp_peak_celsius = 85.0f;
    RearTireModel = MakeUnique<SimRacing::PacejkaTireModel>(RearCoeffs);

    UE_LOG(LogTemp, Log, TEXT("[RacingVehicleMovement] Pacejka tire models initialized."));
}

void URacingVehicleMovement::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Las fuerzas Pacejka se calculan en el substep de física (100 Hz).
    // Este tick visual (60 Hz) solo actualiza telemetría para el HUD.
    UpdatePacejkaTireForces(DeltaTime);
    UpdateGForces(DeltaTime);
}

void URacingVehicleMovement::UpdatePacejkaTireForces(float SubstepDt)
{
    if (!FrontTireModel || !RearTireModel) return;

    const float SpeedMps = GetForwardSpeed() / 100.0f;  // UE cm/s → m/s

    for (int32 i = 0; i < 4; ++i)
    {
        const bool bFront = (i < 2);
        SimRacing::PacejkaTireModel* Model = bFront ? FrontTireModel.Get() : RearTireModel.Get();

        // Carga nominal — distribución 45/55 F/R para ~1400 kg
        const float NormalLoad = bFront ? 3087.0f : 3773.0f;
        WheelLoad[i] = NormalLoad;

        SimRacing::TireContactPatch Contact;
        Contact.vertical_load_N     = NormalLoad;
        Contact.speed_mps           = FMath::Abs(SpeedMps);
        Contact.longitudinal_slip   = WheelLongitudinalSlip[i];
        Contact.lateral_slip_rad    = WheelLateralSlip[i];
        Contact.temperature_celsius = WheelThermo[i].surface_temp;

        SimRacing::TireForces Forces = Model->Evaluate(Contact);

        SimRacing::TireTemperatureModel ThermoModel;
        WheelThermo[i] = ThermoModel.Update(WheelThermo[i], Contact, SubstepDt);

        // Pacejka es telemetría por ahora — Chaos maneja su propio modelo de fricción.
        // TODO: override via SimulateTireForces cuando se integre el callback de Chaos.
        (void)Forces;
    }
}

void URacingVehicleMovement::UpdateGForces(float DeltaTime)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    const FVector CurrentVelocity = Owner->GetVelocity();  // cm/s en UE
    const FVector Acceleration = (CurrentVelocity - PrevVelocityWorld) / (DeltaTime * 100.0f);  // m/s²

    // Proyectar aceleración en los ejes del coche
    const FVector Forward = Owner->GetActorForwardVector();
    const FVector Right   = Owner->GetActorRightVector();

    GForce_Longitudinal = FVector::DotProduct(Acceleration, Forward) / 9.81f;
    GForce_Lateral      = FVector::DotProduct(Acceleration, Right)   / 9.81f;

    // Ángulo de understeer: diferencia entre heading del coche y dirección de velocidad
    if (CurrentVelocity.SizeSquared() > 100.0f)  // > 1 m/s
    {
        FVector VelDir = CurrentVelocity.GetSafeNormal();
        float DotFwd   = FVector::DotProduct(VelDir, Forward);
        float DotRight = FVector::DotProduct(VelDir, Right);
        UndersteerAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(DotRight, DotFwd));
    }
    else
    {
        UndersteerAngleDeg = 0.0f;
    }

    PrevVelocityWorld = CurrentVelocity;
}

float URacingVehicleMovement::GetTireTemperature(int32 WheelIndex) const
{
    if (WheelIndex < 0 || WheelIndex >= 4) return 0.0f;
    return WheelThermo[WheelIndex].surface_temp;
}

float URacingVehicleMovement::GetTireWear(int32 WheelIndex) const
{
    if (WheelIndex < 0 || WheelIndex >= 4) return 0.0f;
    return WheelThermo[WheelIndex].wear_factor;
}

float URacingVehicleMovement::GetTireLateralSlip(int32 WheelIndex) const
{
    if (WheelIndex < 0 || WheelIndex >= 4) return 0.0f;
    return WheelLateralSlip[WheelIndex];
}

float URacingVehicleMovement::GetTireLongitudinalSlip(int32 WheelIndex) const
{
    if (WheelIndex < 0 || WheelIndex >= 4) return 0.0f;
    return WheelLongitudinalSlip[WheelIndex];
}
