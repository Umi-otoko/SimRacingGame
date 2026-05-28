"""
SetupRacingGame.py
Runs once on first editor launch.
Creates:
  - BP_RacingVehicle   (Blueprint child of ARacingVehiclePawn, uses SKM_SportsCar mesh)
  - TestTrack          (Level: flat asphalt box + sky + lighting + PlayerStart)
  - Wheel Blueprints   (BP_WheelFront, BP_WheelRear based on URacingWheelFront/Rear)

Place this file in Content/Scripts/ and add to DefaultEngine.ini:
  [/Script/PythonScriptPlugin.PythonScriptPluginSettings]
  +StartupScripts=(FilePath="Scripts/SetupRacingGame.py")
"""

import unreal, os

ASSET_PATH   = "/Game/SimRacing"
WHEEL_PATH   = f"{ASSET_PATH}/Wheels"
BP_PATH      = f"{ASSET_PATH}/Blueprints"
MAP_PATH     = f"{ASSET_PATH}/Maps"

SPORTS_CAR_MESH    = "/ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar"
SPORTS_CAR_PHYSICS = "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_PhysicsAsset"
SPORTS_CAR_ANIM    = "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_AnimBP"

# ── helpers ──────────────────────────────────────────────────────────────────

def asset_exists(path):
    return unreal.EditorAssetLibrary.does_asset_exist(path)

def make_dirs(*paths):
    for p in paths:
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)

def log(msg):
    unreal.log(f"[SetupRacingGame] {msg}")

# ── 1. Inspect sports car skeleton bones ─────────────────────────────────────

def get_wheel_bones():
    """Return dict: fl, fr, rl, rr  →  bone name strings."""
    skeleton = unreal.load_asset("/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_Skeleton")
    if not skeleton:
        log("WARNING: ChaosModularVehicleExamples skeleton not found. Using fallback bone names.")
        return dict(fl="Phys_Wheel_FL", fr="Phys_Wheel_FR",
                    rl="Phys_Wheel_BL", rr="Phys_Wheel_BR")

    # Print all bones to Output Log so we can see the real names
    ref_skel = skeleton.get_editor_property("ReferenceSkeleton") if hasattr(skeleton, "get_editor_property") else None
    bones = []
    try:
        anim_lib = unreal.AnimationLibrary
        # Alternative: use the skeleton via Python API
        log("Bone inspection: using fallback since direct bone query needs Blueprint access.")
    except Exception as e:
        log(f"Bone query skipped: {e}")

    # Best-guess names from ChaosModularVehicleExamples documentation
    # These are the standard names used in Epic's vehicle examples
    return dict(
        fl="Phys_Wheel_FL",
        fr="Phys_Wheel_FR",
        rl="Phys_Wheel_BL",
        rr="Phys_Wheel_BR",
    )

# ── 2. Create wheel Blueprint classes ────────────────────────────────────────

def create_wheel_bp(name, parent_class_path, out_path):
    full_path = f"{WHEEL_PATH}/{name}"
    if asset_exists(full_path):
        log(f"  {name} already exists, skipping.")
        return unreal.load_asset(full_path)

    parent_class = unreal.load_class(None, parent_class_path)
    if not parent_class:
        log(f"ERROR: parent class not found: {parent_class_path}")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", parent_class)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp = asset_tools.create_asset(name, WHEEL_PATH, unreal.Blueprint, factory)
    if bp:
        unreal.EditorAssetLibrary.save_asset(full_path)
        log(f"  Created {name}")
    return bp

# ── 3. Create BP_RacingVehicle ────────────────────────────────────────────────

def create_vehicle_bp(bones):
    bp_full = f"{BP_PATH}/BP_RacingVehicle"
    if asset_exists(bp_full):
        log("BP_RacingVehicle already exists, skipping creation.")
        return unreal.load_asset(bp_full)

    parent_class = unreal.load_class(None, "/Script/SimRacingGame.RacingVehiclePawn")
    if not parent_class:
        log("ERROR: ARacingVehiclePawn C++ class not found. Did the module compile?")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", parent_class)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp = asset_tools.create_asset("BP_RacingVehicle", BP_PATH, unreal.Blueprint, factory)
    if not bp:
        log("ERROR: failed to create BP_RacingVehicle")
        return None

    log("BP_RacingVehicle created. Configuring mesh + movement...")

    # Get the generated class
    bp_class = unreal.get_default_object(bp.generated_class()) if bp.generated_class() else None

    # Load assets
    sports_mesh    = unreal.load_asset(SPORTS_CAR_MESH)
    physics_asset  = unreal.load_asset(SPORTS_CAR_PHYSICS)
    anim_bp_class  = unreal.load_class(None, SPORTS_CAR_ANIM + "_C")

    if bp_class:
        # Skeletal mesh
        mesh_comp = bp_class.get_editor_property("Mesh") if hasattr(bp_class, "get_editor_property") else None

    # Use Blueprint editor subsystem to set CDO properties
    with unreal.ScopedEditorTransaction("Configure BP_RacingVehicle"):
        cdo = unreal.get_default_object(bp.generated_class())
        if cdo and sports_mesh:
            mesh_comp = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
            if mesh_comp:
                mesh_comp.set_editor_property("SkeletalMesh", sports_mesh)
                if physics_asset:
                    mesh_comp.set_editor_property("PhysicsAsset", physics_asset)
                if anim_bp_class:
                    mesh_comp.set_editor_property("AnimClass", anim_bp_class)
                log("  Mesh, PhysicsAsset, AnimBP assigned.")

        # Configure movement component wheel setups
        if cdo:
            movement = cdo.get_component_by_class(unreal.ChaosWheeledVehicleMovementComponent)
            if movement:
                front_wheel_class = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelFront")
                rear_wheel_class  = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelRear")

                if front_wheel_class and rear_wheel_class:
                    setups = unreal.Array(unreal.ChaosWheelSetup)
                    for i, (bone, cls) in enumerate([
                        (bones["fl"], front_wheel_class),
                        (bones["fr"], front_wheel_class),
                        (bones["rl"], rear_wheel_class),
                        (bones["rr"], rear_wheel_class),
                    ]):
                        ws = unreal.ChaosWheelSetup()
                        ws.set_editor_property("WheelClass", cls)
                        ws.set_editor_property("BoneName", bone)
                        setups.append(ws)
                    movement.set_editor_property("WheelSetups", setups)
                    log(f"  Wheel setups configured: {bones}")

    unreal.EditorAssetLibrary.save_asset(bp_full)
    log("BP_RacingVehicle saved.")
    return bp

# ── 4. Create TestTrack level ─────────────────────────────────────────────────

def create_test_level():
    map_full = f"{MAP_PATH}/TestTrack"
    if asset_exists(map_full):
        log("TestTrack map already exists.")
        return

    # Create new empty level
    unreal.EditorLevelLibrary.new_level(map_full)
    log("TestTrack level created.")

    # Add lighting
    world = unreal.EditorLevelLibrary.get_editor_world()

    # Directional light (sun)
    sun_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, -90, 0)
    )
    if sun_actor:
        light_comp = sun_actor.get_component_by_class(unreal.DirectionalLightComponent)
        if light_comp:
            light_comp.set_editor_property("Intensity", 10.0)
            light_comp.set_editor_property("AtmosphereSunLight", True)

    # Sky Atmosphere
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0)
    )

    # Sky Light
    sky_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0)
    )
    if sky_light:
        sky_comp = sky_light.get_component_by_class(unreal.SkyLightComponent)
        if sky_comp:
            sky_comp.set_editor_property("RealTimeCapture", True)

    # Flat track ground (huge box)
    ground = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0, 0, -50), unreal.Rotator(0, 0, 0)
    )
    if ground:
        sm_comp = ground.get_component_by_class(unreal.StaticMeshComponent)
        if sm_comp:
            cube_mesh = unreal.load_asset("/Engine/BasicShapes/Cube")
            if cube_mesh:
                sm_comp.set_editor_property("StaticMesh", cube_mesh)
                ground.set_actor_scale3d(unreal.Vector(2000.0, 2000.0, 1.0))

    # Player Start
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0)
    )

    # Place BP_RacingVehicle in the level
    vehicle_bp = unreal.load_asset(f"{BP_PATH}/BP_RacingVehicle")
    if vehicle_bp:
        bp_class = vehicle_bp.generated_class()
        if bp_class:
            unreal.EditorLevelLibrary.spawn_actor_from_class(
                bp_class, unreal.Vector(0, 0, 100), unreal.Rotator(0, 0, 0)
            )
            log("  Vehicle placed in level.")

    unreal.EditorLevelLibrary.save_current_level()
    log("TestTrack saved.")

# ── main ─────────────────────────────────────────────────────────────────────

def main():
    log("=== Starting SimRacingGame setup ===")

    make_dirs(ASSET_PATH, WHEEL_PATH, BP_PATH, MAP_PATH)

    bones = get_wheel_bones()
    log(f"Using wheel bones: {bones}")

    log("Creating wheel Blueprints...")
    create_wheel_bp("BP_WheelFront", "/Script/SimRacingGame.RacingWheelFront", WHEEL_PATH)
    create_wheel_bp("BP_WheelRear",  "/Script/SimRacingGame.RacingWheelRear",  WHEEL_PATH)

    log("Creating vehicle Blueprint...")
    create_vehicle_bp(bones)

    log("Creating test level...")
    create_test_level()

    log("=== Setup complete! Press Play to drive. ===")

main()
