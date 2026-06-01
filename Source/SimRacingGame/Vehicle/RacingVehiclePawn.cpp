#include "RacingVehiclePawn.h"
#include "RacingVehicleMovement.h"
#include "RacingVehicleWheel.h"
#include "RacingAudio.h"
#include "RacingGameMode.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "WheeledVehiclePawn.h"

ARacingVehiclePawn::ARacingVehiclePawn(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<URacingVehicleMovement>(
          AWheeledVehiclePawn::VehicleMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);

    // Chase camera — vista exterior
    ChaseCameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseCameraArm"));
    ChaseCameraArm->SetupAttachment(GetMesh());
    ChaseCameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));    // ancla en techo del coche
    ChaseCameraArm->TargetArmLength          = 500.0f;
    ChaseCameraArm->SocketOffset             = FVector(0.0f, 0.0f, 40.0f);
    ChaseCameraArm->SetRelativeRotation(FRotator(-15.0f, 0.0f, 0.0f));  // ángulo más natural
    ChaseCameraArm->bUsePawnControlRotation  = false;
    ChaseCameraArm->bInheritPitch            = false;
    ChaseCameraArm->bInheritRoll             = false;
    ChaseCameraArm->bDoCollisionTest         = true;
    ChaseCameraArm->ProbeSize                = 12.0f;
    ChaseCameraArm->bEnableCameraLag         = true;
    ChaseCameraArm->CameraLagSpeed           = 8.0f;
    ChaseCameraArm->bEnableCameraRotationLag = true;
    ChaseCameraArm->CameraRotationLagSpeed   = 5.0f;

    ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    ChaseCamera->SetupAttachment(ChaseCameraArm, USpringArmComponent::SocketName);
    ChaseCamera->FieldOfView              = 75.0f;
    ChaseCamera->bUsePawnControlRotation  = false;
    ChaseCamera->SetActive(true);

    // Audio — síntesis FM procedural (motor + chirrido de neumáticos)
    AudioComp = CreateDefaultSubobject<URacingAudio>(TEXT("EngineAudio"));
    AudioComp->SetupAttachment(GetMesh());
    AudioComp->bAutoActivate = true;

    // ── WheelSetups en C++ ─────────────────────────────────────────────────────
    // CRÍTICO: CanCreateVehicle() devuelve false si BoneName == NAME_None,
    // lo que impide que Chaos cree la simulación del vehículo y el coche no se mueve.
    // Bones extraídos de SportsCar_Skeleton.uasset (Phys_Wheel_FL/FR/BL/BR).
    // Nota: el skeleton usa "BL/BR" (Back) para el eje trasero, no "RL/RR".
    if (UChaosWheeledVehicleMovementComponent* VM =
            Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
    {
        auto MakeWheelSetup = [](TSubclassOf<UChaosVehicleWheel> Cls, const TCHAR* Bone)
        {
            FChaosWheelSetup WS;
            WS.WheelClass = Cls;
            WS.BoneName   = FName(Bone);
            // AdditionalOffset queda en (0,0,0): los bones ya están en el hub de rueda
            return WS;
        };

        VM->WheelSetups = {
            MakeWheelSetup(URacingWheelFront::StaticClass(), TEXT("Phys_Wheel_FL")),
            MakeWheelSetup(URacingWheelFront::StaticClass(), TEXT("Phys_Wheel_FR")),
            MakeWheelSetup(URacingWheelRear::StaticClass(),  TEXT("Phys_Wheel_BL")),
            MakeWheelSetup(URacingWheelRear::StaticClass(),  TEXT("Phys_Wheel_BR")),
        };
    }

    // Cockpit camera — vista desde el piloto
    CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    CockpitCamera->SetupAttachment(GetMesh());
    CockpitCamera->SetRelativeLocation(FVector(20.0f, 0.0f, 120.0f));   // centrado, altura de casco
    CockpitCamera->SetRelativeRotation(FRotator(-5.0f, 0.0f, 0.0f));    // ligera inclinación hacia pista
    CockpitCamera->FieldOfView = 95.0f;
    CockpitCamera->SetActive(false);

    // Intentar cargar assets de input (si ya existen en disco)
    static ConstructorHelpers::FObjectFinder<UInputAction>
        F_Throttle    (TEXT("/Game/SimRacing/Input/IA_Throttle")),
        F_Brake       (TEXT("/Game/SimRacing/Input/IA_Brake")),
        F_Steering    (TEXT("/Game/SimRacing/Input/IA_Steering")),
        F_Handbrake   (TEXT("/Game/SimRacing/Input/IA_Handbrake")),
        F_GearUp      (TEXT("/Game/SimRacing/Input/IA_GearUp")),
        F_GearDown    (TEXT("/Game/SimRacing/Input/IA_GearDown")),
        F_SwitchCam   (TEXT("/Game/SimRacing/Input/IA_SwitchCamera")),
        F_ResetVehicle(TEXT("/Game/SimRacing/Input/IA_ResetVehicle"));
    static ConstructorHelpers::FObjectFinder<UInputMappingContext>
        F_IMC(TEXT("/Game/SimRacing/Input/IMC_Racing"));

    if (F_Throttle.Succeeded())     IA_Throttle     = F_Throttle.Object;
    if (F_Brake.Succeeded())        IA_Brake        = F_Brake.Object;
    if (F_Steering.Succeeded())     IA_Steering     = F_Steering.Object;
    if (F_Handbrake.Succeeded())    IA_Handbrake    = F_Handbrake.Object;
    if (F_GearUp.Succeeded())       IA_GearUp       = F_GearUp.Object;
    if (F_GearDown.Succeeded())     IA_GearDown     = F_GearDown.Object;
    if (F_SwitchCam.Succeeded())    IA_SwitchCamera  = F_SwitchCam.Object;
    if (F_ResetVehicle.Succeeded()) IA_ResetVehicle = F_ResetVehicle.Object;
    if (F_IMC.Succeeded())          RacingMappingContext = F_IMC.Object;
}

void ARacingVehiclePawn::BeginPlay()
{
    Super::BeginPlay();
    SetVehicleEnabled(false);  // Se habilita cuando el GameMode arranca la carrera
    LapStartTimeLocal = FPlatformTime::Seconds();

    // Seguro de respaldo: si el GameMode no llama a SetVehicleEnabled(true)
    // (countdown = 3s) habilitamos el vehículo a los 5s automáticamente.
    // Esto garantiza que el coche SIEMPRE sea controlable en PIE / demos.
    GetWorldTimerManager().SetTimer(
        EnableFallbackTimer, this,
        &ARacingVehiclePawn::TryAutoEnable,
        5.0f, false);
}

void ARacingVehiclePawn::TryAutoEnable()
{
    if (!bVehicleEnabled)
    {
        SetVehicleEnabled(true);
        UE_LOG(LogTemp, Warning,
            TEXT("[RacingVehiclePawn] TryAutoEnable: el GameMode no habilitó el vehículo — "
                 "habilitando por fallback. Verifica que RacingGameMode esté activo en el nivel."));
    }
}

void ARacingVehiclePawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (HasAuthority())
        UpdateReplicatedState();

    // Audio solo en el cliente local (el servidor no tiene salida de audio)
    if (IsLocallyControlled() && AudioComp)
    {
        if (URacingVehicleMovement* VM = GetRacingMovement())
        {
            AudioComp->SetRPM(VM->GetEngineRotationSpeed());
            AudioComp->SetThrottle(CurrentThrottle);

            float MaxSlip = 0.0f;
            for (int32 i = 0; i < 4; ++i)
                MaxSlip = FMath::Max(MaxSlip, FMath::Abs(VM->GetTireLateralSlip(i)));
            AudioComp->SetTireSlip(MaxSlip);
        }
    }
}

void ARacingVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Cargar assets en runtime como fallback (si ConstructorHelpers no los encontró)
    const TCHAR* P = TEXT("/Game/SimRacing/Input/");
    auto Load = [&](UInputAction*& Ptr, const TCHAR* Name)
    {
        if (!Ptr) Ptr = LoadObject<UInputAction>(nullptr, *(FString(P) + Name));
    };
    Load(IA_Throttle,     TEXT("IA_Throttle"));
    Load(IA_Brake,        TEXT("IA_Brake"));
    Load(IA_Steering,     TEXT("IA_Steering"));
    Load(IA_Handbrake,    TEXT("IA_Handbrake"));
    Load(IA_GearUp,       TEXT("IA_GearUp"));
    Load(IA_GearDown,     TEXT("IA_GearDown"));
    Load(IA_SwitchCamera, TEXT("IA_SwitchCamera"));
    Load(IA_ResetVehicle, TEXT("IA_ResetVehicle"));
    if (!RacingMappingContext)
        RacingMappingContext = LoadObject<UInputMappingContext>(nullptr,
            TEXT("/Game/SimRacing/Input/IMC_Racing"));

    // Garantizar que todos los InputAction existen aunque no haya assets en disco.
    // Sin esto el IMC runtime nunca se crea y el coche no responde a ninguna tecla.
    auto EnsureAction = [&](UInputAction*& Ptr, EInputActionValueType VT)
    {
        if (!Ptr)
        {
            Ptr = NewObject<UInputAction>(this, NAME_None, RF_Transient);
            Ptr->ValueType = VT;
        }
    };
    EnsureAction(IA_Throttle,     EInputActionValueType::Axis1D);
    EnsureAction(IA_Brake,        EInputActionValueType::Axis1D);
    EnsureAction(IA_Steering,     EInputActionValueType::Axis1D);
    EnsureAction(IA_Handbrake,    EInputActionValueType::Axis1D);
    EnsureAction(IA_GearUp,       EInputActionValueType::Boolean);
    EnsureAction(IA_GearDown,     EInputActionValueType::Boolean);
    EnsureAction(IA_SwitchCamera, EInputActionValueType::Boolean);
    EnsureAction(IA_ResetVehicle, EInputActionValueType::Boolean);

    // Si no existe IMC en disco, crear uno en runtime con bindings WASD hardcodeados
    if (!RacingMappingContext)
    {
        RacingMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Racing_Runtime"));
        if (IA_Throttle)     RacingMappingContext->MapKey(IA_Throttle,    EKeys::W);
        if (IA_Throttle)     RacingMappingContext->MapKey(IA_Throttle,    EKeys::Gamepad_RightTriggerAxis);
        if (IA_Brake)        RacingMappingContext->MapKey(IA_Brake,       EKeys::S);
        if (IA_Brake)        RacingMappingContext->MapKey(IA_Brake,       EKeys::Gamepad_LeftTriggerAxis);
        if (IA_Handbrake)    RacingMappingContext->MapKey(IA_Handbrake,   EKeys::SpaceBar);
        if (IA_GearUp)       RacingMappingContext->MapKey(IA_GearUp,      EKeys::E);
        if (IA_GearDown)     RacingMappingContext->MapKey(IA_GearDown,    EKeys::Q);
        if (IA_SwitchCamera) RacingMappingContext->MapKey(IA_SwitchCamera,EKeys::V);
        if (IA_ResetVehicle) RacingMappingContext->MapKey(IA_ResetVehicle,EKeys::R);
        if (IA_Steering)
        {
            RacingMappingContext->MapKey(IA_Steering, EKeys::D);
            FEnhancedActionKeyMapping& AMap = RacingMappingContext->MapKey(IA_Steering, EKeys::A);
            AMap.Modifiers.Add(NewObject<UInputModifierNegate>(RacingMappingContext));
            RacingMappingContext->MapKey(IA_Steering, EKeys::Gamepad_LeftX);
        }
        UE_LOG(LogTemp, Log, TEXT("[RacingVehiclePawn] IMC creado en runtime con bindings WASD."));
    }

    // Registrar el IMC en el subsistema de Enhanced Input
    if (RacingMappingContext)
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (UEnhancedInputLocalPlayerSubsystem* Sub =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            {
                Sub->AddMappingContext(RacingMappingContext, 0);
            }
        }
    }

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC) return;

    if (IA_Throttle)     EIC->BindAction(IA_Throttle,     ETriggerEvent::Triggered,  this, &ARacingVehiclePawn::Input_Throttle);
    if (IA_Throttle)     EIC->BindAction(IA_Throttle,     ETriggerEvent::Completed,  this, &ARacingVehiclePawn::Input_Throttle);
    if (IA_Brake)        EIC->BindAction(IA_Brake,        ETriggerEvent::Triggered,  this, &ARacingVehiclePawn::Input_Brake);
    if (IA_Brake)        EIC->BindAction(IA_Brake,        ETriggerEvent::Completed,  this, &ARacingVehiclePawn::Input_Brake);
    if (IA_Steering)     EIC->BindAction(IA_Steering,     ETriggerEvent::Triggered,  this, &ARacingVehiclePawn::Input_Steering);
    if (IA_Steering)     EIC->BindAction(IA_Steering,     ETriggerEvent::Completed,  this, &ARacingVehiclePawn::Input_Steering);
    if (IA_Handbrake)    EIC->BindAction(IA_Handbrake,    ETriggerEvent::Triggered,  this, &ARacingVehiclePawn::Input_Handbrake);
    if (IA_Handbrake)    EIC->BindAction(IA_Handbrake,    ETriggerEvent::Completed,  this, &ARacingVehiclePawn::Input_Handbrake);
    if (IA_GearUp)       EIC->BindAction(IA_GearUp,       ETriggerEvent::Started,    this, &ARacingVehiclePawn::Input_GearUp);
    if (IA_GearDown)     EIC->BindAction(IA_GearDown,     ETriggerEvent::Started,    this, &ARacingVehiclePawn::Input_GearDown);
    if (IA_SwitchCamera) EIC->BindAction(IA_SwitchCamera, ETriggerEvent::Started,    this, &ARacingVehiclePawn::Input_SwitchCamera);
    if (IA_ResetVehicle) EIC->BindAction(IA_ResetVehicle, ETriggerEvent::Started,    this, &ARacingVehiclePawn::Input_ResetVehicle);
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
    CurrentThrottle = 0.0f;

    if (bEnabled)
    {
        // Resetear el cronómetro de vuelta exactamente cuando se da el "GO"
        LapStartTimeLocal = FPlatformTime::Seconds();
    }

    if (URacingVehicleMovement* VM = GetRacingMovement())
    {
        VM->SetThrottleInput(0.0f);
        VM->SetBrakeInput(bEnabled ? 0.0f : 1.0f);
    }
}

// ── Input handlers ────────────────────────────────────────────────────────────

void ARacingVehiclePawn::Input_Throttle(const FInputActionValue& Value)
{
    if (!bVehicleEnabled) return;
    CurrentThrottle = Value.Get<float>();
    if (URacingVehicleMovement* VM = GetRacingMovement())
        VM->SetThrottleInput(CurrentThrottle);
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

// ── RPCs ──────────────────────────────────────────────────────────────────────

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
    FRotator Rot = GetActorRotation();
    SetActorRotation(FRotator(0.0f, Rot.Yaw, 0.0f));
    if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetRootComponent()))
    {
        Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    }
}

// ── Replicación ───────────────────────────────────────────────────────────────

void ARacingVehiclePawn::UpdateReplicatedState()
{
    if (URacingVehicleMovement* VM = GetRacingMovement())
    {
        ReplicatedSpeedKmh  = VM->GetForwardSpeedMPH() * 1.60934f;
        ReplicatedGear      = VM->GetCurrentGear();
        ReplicatedEngineRPM = VM->GetEngineRotationSpeed();
    }
}

void ARacingVehiclePawn::OnRep_ReplicatedSpeed()
{
    // Actualizar HUD cuando llega velocidad del servidor (para espectadores)
}

// ── Checkpoints / Meta ────────────────────────────────────────────────────────

void ARacingVehiclePawn::NotifyCheckpointCrossed(int32 CheckpointIndex)
{
    if (!HasAuthority()) return;
    if (ARacingGameMode* GM = GetWorld()->GetAuthGameMode<ARacingGameMode>())
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            GM->RegisterCheckpointCrossed(PC, CheckpointIndex);
        }
    }
}

void ARacingVehiclePawn::NotifyFinishLineCrossed()
{
    if (!HasAuthority()) return;
    if (ARacingGameMode* GM = GetWorld()->GetAuthGameMode<ARacingGameMode>())
    {
        const double Now = FPlatformTime::Seconds();
        const float LapTime = static_cast<float>(Now - LapStartTimeLocal);
        LapStartTimeLocal = Now;   // Reiniciar para la siguiente vuelta
        GM->OnPlayerCrossedFinishLine(this, LapTime);
    }
}

URacingVehicleMovement* ARacingVehiclePawn::GetRacingMovement() const
{
    return Cast<URacingVehicleMovement>(GetVehicleMovementComponent());
}
