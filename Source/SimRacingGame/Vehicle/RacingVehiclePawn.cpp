#include "RacingVehiclePawn.h"
#include "RacingVehicleMovement.h"
#include "RacingGameMode.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"

ARacingVehiclePawn::ARacingVehiclePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);

    // Reemplazar el componente de movimiento por defecto con el nuestro (Pacejka)
    // Se hace en el constructor del Blueprint derivado:
    // GetVehicleMovementComponent() se debe castear a URacingVehicleMovement

    // Chase camera — vista exterior
    ChaseCameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseCameraArm"));
    ChaseCameraArm->SetupAttachment(GetMesh());
    ChaseCameraArm->TargetArmLength = 600.0f;
    ChaseCameraArm->SocketOffset    = FVector(0.0f, 0.0f, 80.0f);
    ChaseCameraArm->bUsePawnControlRotation = false;
    ChaseCameraArm->bInheritPitch = false;
    ChaseCameraArm->bInheritRoll  = false;
    ChaseCameraArm->bDoCollisionTest = true;
    ChaseCameraArm->ProbeSize = 12.0f;

    ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    ChaseCamera->SetupAttachment(ChaseCameraArm, USpringArmComponent::SocketName);
    ChaseCamera->FieldOfView = 80.0f;
    ChaseCamera->bUsePawnControlRotation = false;

    // Cockpit camera — vista desde el interior
    CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    CockpitCamera->SetupAttachment(GetMesh());
    CockpitCamera->SetRelativeLocation(FVector(20.0f, 0.0f, 90.0f));
    CockpitCamera->FieldOfView = 95.0f;  // FOV más amplio en cockpit para inmersión
    CockpitCamera->SetActive(false);

    // Chase camera activa por defecto
    ChaseCamera->SetActive(true);
}

void ARacingVehiclePawn::BeginPlay()
{
    Super::BeginPlay();

    // Bloquear control hasta que el GameMode inicie la carrera
    SetVehicleEnabled(false);

    // Registrar Input Mapping Context (solo en el cliente local)
    if (IsLocallyControlled())
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            {
                if (RacingMappingContext)
                    Subsystem->AddMappingContext(RacingMappingContext, 0);
            }
        }

        LapStartTimeLocal = FPlatformTime::Seconds();
    }
}

void ARacingVehiclePawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Solo en el servidor: actualizar estado replicado para otros clientes
    if (HasAuthority())
        UpdateReplicatedState();
}

void ARacingVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC) return;

    if (IA_Throttle)   EIC->BindAction(IA_Throttle,   ETriggerEvent::Triggered, this, &ARacingVehiclePawn::Input_Throttle);
    if (IA_Brake)      EIC->BindAction(IA_Brake,       ETriggerEvent::Triggered, this, &ARacingVehiclePawn::Input_Brake);
    if (IA_Steering)   EIC->BindAction(IA_Steering,    ETriggerEvent::Triggered, this, &ARacingVehiclePawn::Input_Steering);
    if (IA_Handbrake)  EIC->BindAction(IA_Handbrake,   ETriggerEvent::Triggered, this, &ARacingVehiclePawn::Input_Handbrake);
    if (IA_GearUp)     EIC->BindAction(IA_GearUp,      ETriggerEvent::Started,   this, &ARacingVehiclePawn::Input_GearUp);
    if (IA_GearDown)   EIC->BindAction(IA_GearDown,    ETriggerEvent::Started,   this, &ARacingVehiclePawn::Input_GearDown);
    if (IA_SwitchCamera) EIC->BindAction(IA_SwitchCamera, ETriggerEvent::Started, this, &ARacingVehiclePawn::Input_SwitchCamera);
    if (IA_ResetVehicle) EIC->BindAction(IA_ResetVehicle, ETriggerEvent::Started, this, &ARacingVehiclePawn::Input_ResetVehicle);
}

void ARacingVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ARacingVehiclePawn, ReplicatedSpeedKmh);
    DOREPLIFETIME(ARacingVehiclePawn, ReplicatedGear);
    DOREPLIFETIME(ARacingVehiclePawn, ReplicatedEngineRPM);
}

void ARacingVehiclePawn::SetVehicleEnabled(bool bEnabled)
{
    bVehicleEnabled = bEnabled;

    if (URacingVehicleMovement* VM = GetRacingMovement())
    {
        VM->SetThrottleInput(0.0f);
        VM->SetBrakeInput(bEnabled ? 0.0f : 1.0f);  // Freno de holding en countdown
    }
}

// ---- Input handlers ----

void ARacingVehiclePawn::Input_Throttle(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetThrottleInput(Value.Get<float>());
}

void ARacingVehiclePawn::Input_Brake(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetBrakeInput(Value.Get<float>());
}

void ARacingVehiclePawn::Input_Steering(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetSteeringInput(Value.Get<float>());
}

void ARacingVehiclePawn::Input_Handbrake(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetHandbrakeInput(Value.Get<float>() > 0.5f);
}

void ARacingVehiclePawn::Input_GearUp(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    // RPC al servidor — cambio de marcha es autoritativo
    Server_GearUp();
}

void ARacingVehiclePawn::Input_GearDown(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    Server_GearDown();
}

void ARacingVehiclePawn::Input_SwitchCamera(const FInputActionValue& Value)
{
    bUsingCockpitCamera = !bUsingCockpitCamera;
    ChaseCamera->SetActive(!bUsingCockpitCamera);
    CockpitCamera->SetActive(bUsingCockpitCamera);
}

void ARacingVehiclePawn::Input_ResetVehicle(const FInputActionValue& Value)
{
    Server_ResetVehicle();
}

// ---- RPCs ----

void ARacingVehiclePawn::Server_GearUp_Implementation()
{
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetTargetGear(VM->GetCurrentGear() + 1, false);
}

void ARacingVehiclePawn::Server_GearDown_Implementation()
{
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetTargetGear(VM->GetCurrentGear() - 1, false);
}

void ARacingVehiclePawn::Server_ResetVehicle_Implementation()
{
    // Voltear el coche de vuelta a la posición vertical en la pista
    FRotator CurrentRot = GetActorRotation();
    SetActorRotation(FRotator(0.0f, CurrentRot.Yaw, 0.0f));

    if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetRootComponent()))
    {
        Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }
}

// ---- Replicación de estado ----

void ARacingVehiclePawn::UpdateReplicatedState()
{
    if (URacingVehicleMovement* VM = GetRacingMovement())
    {
        ReplicatedSpeedKmh  = VM->GetForwardSpeedMPH() * 1.60934f;  // MPH → KM/H
        ReplicatedGear      = VM->GetCurrentGear();
        ReplicatedEngineRPM = VM->GetEngineRotationSpeed();
    }
}

void ARacingVehiclePawn::OnRep_ReplicatedSpeed()
{
    // Actualizar HUD del jugador local cuando llega la velocidad del servidor
    // (para jugadores que observan a otros)
}

// ---- Checkpoints ----

void ARacingVehiclePawn::NotifyCheckpointCrossed(int32 CheckpointIndex)
{
    if (!HasAuthority()) return;

    if (ARacingGameMode* GM = GetWorld()->GetAuthGameMode<ARacingGameMode>())
    {
        // El GameMode registra el checkpoint
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            // GM->RegisterCheckpoint(PC, CheckpointIndex);  // TODO: implementar
        }
    }
}

void ARacingVehiclePawn::NotifyFinishLineCrossed()
{
    if (!HasAuthority()) return;

    if (ARacingGameMode* GM = GetWorld()->GetAuthGameMode<ARacingGameMode>())
    {
        float LapTime = static_cast<float>(FPlatformTime::Seconds() - LapStartTimeLocal);
        GM->OnPlayerCrossedFinishLine(this, LapTime);
    }
}

URacingVehicleMovement* ARacingVehiclePawn::GetRacingMovement() const
{
    return Cast<URacingVehicleMovement>(GetVehicleMovementComponent());
}
