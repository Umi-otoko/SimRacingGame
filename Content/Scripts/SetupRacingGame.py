"""
SetupRacingGame.py  — auto-runs at editor startup
Creates: BP_RacingVehicle + TestTrack level.

Mesh priority:
  1. /ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar
  2. /ControlRigModules/Modules/Meshes/SKM_Car_Template
  3. No mesh assigned (user must set it manually in Blueprint)

Wheels use AdditionalOffset (absolute) instead of bone names,
so they work regardless of skeleton bone naming.
"""

import unreal

ASSET_BASE  = "/Game/SimRacing"
WHEEL_PATH  = ASSET_BASE + "/Wheels"
BP_PATH     = ASSET_BASE + "/Blueprints"
MAP_PATH    = ASSET_BASE + "/Maps"

MESH_CANDIDATES = [
    ("/ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar",
     "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_PhysicsAsset",
     "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_AnimBP"),
    ("/ControlRigModules/Modules/Meshes/SKM_Car_Template", None, None),
]

# Wheel offsets relative to vehicle root (cm):
#   (forward, right, up)     ← Unreal X=Forward, Y=Right, Z=Up
WHEEL_OFFSETS = {
    "FL": (130.0, -75.0, 0.0),
    "FR": (130.0,  75.0, 0.0),
    "RL": (-130.0, -75.0, 0.0),
    "RR": (-130.0,  75.0, 0.0),
}

def log(msg):
    unreal.log(f"[SetupRacing] {msg}")

def asset_exists(p):
    return unreal.EditorAssetLibrary.does_asset_exist(p)

def make_dirs(*paths):
    for p in paths:
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)

# ── find best available vehicle mesh ─────────────────────────────────────────

def find_vehicle_mesh():
    for mesh_path, phys_path, anim_path in MESH_CANDIDATES:
        mesh = unreal.load_asset(mesh_path)
        if mesh:
            log(f"Using mesh: {mesh_path}")
            phys = unreal.load_asset(phys_path) if phys_path else None
            anim = unreal.load_class(None, anim_path + "_C") if anim_path else None
            # Print all bone names so developer can see them
            if hasattr(mesh, "get_skeleton"):
                skel = mesh.get_skeleton()
                if skel:
                    try:
                        log("Bone names on this skeleton:")
                        for i in range(1000):
                            try:
                                bn = skel.find_bone_index(unreal.Name(str(i)))
                            except:
                                break
                    except Exception as e:
                        log(f"  (bone enumeration skipped: {e})")
            return mesh, phys, anim
    log("WARNING: No vehicle mesh found. Blueprint will have no mesh — assign SKM_SportsCar manually.")
    return None, None, None

# ── create Blueprint ──────────────────────────────────────────────────────────

def create_vehicle_blueprint():
    full = BP_PATH + "/BP_RacingVehicle"
    if asset_exists(full):
        log("BP_RacingVehicle already exists.")
        return unreal.load_asset(full)

    parent = unreal.load_class(None, "/Script/SimRacingGame.RacingVehiclePawn")
    if not parent:
        log("ERROR: RacingVehiclePawn C++ class not found!")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", parent)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp = asset_tools.create_asset("BP_RacingVehicle", BP_PATH, unreal.Blueprint, factory)
    if not bp:
        log("ERROR: could not create BP_RacingVehicle")
        return None
    log("BP_RacingVehicle created.")
    return bp

def configure_vehicle_blueprint(bp, mesh, phys, anim):
    if not bp or not bp.generated_class():
        return

    front_cls = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelFront")
    rear_cls  = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelRear")
    if not front_cls or not rear_cls:
        log("ERROR: Wheel C++ classes not found. Did the module compile?")
        return

    with unreal.ScopedEditorTransaction("Configure BP_RacingVehicle"):
        cdo = unreal.get_default_object(bp.generated_class())
        if not cdo:
            log("CDO not available yet — mesh/wheel setup skipped (set manually in Blueprint).")
            unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
            return

        # Mesh
        mesh_comp = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
        if mesh_comp:
            if mesh:
                mesh_comp.set_editor_property("SkeletalMesh", mesh)
                log("  Skeletal mesh assigned.")
            if phys:
                mesh_comp.set_editor_property("PhysicsAsset", phys)
                log("  Physics asset assigned.")
            if anim:
                mesh_comp.set_editor_property("AnimClass", anim)
                log("  Anim Blueprint assigned.")

        # Wheels via AdditionalOffset (bone-name independent)
        movement = cdo.get_component_by_class(unreal.ChaosWheeledVehicleMovementComponent)
        if movement:
            wheel_defs = [
                ("",  front_cls, WHEEL_OFFSETS["FL"]),
                ("",  front_cls, WHEEL_OFFSETS["FR"]),
                ("",  rear_cls,  WHEEL_OFFSETS["RL"]),
                ("",  rear_cls,  WHEEL_OFFSETS["RR"]),
            ]
            setups = unreal.Array(unreal.ChaosWheelSetup)
            for bone_name, cls, (fx, fy, fz) in wheel_defs:
                ws = unreal.ChaosWheelSetup()
                ws.set_editor_property("WheelClass", cls)
                ws.set_editor_property("BoneName", bone_name)
                ws.set_editor_property("AdditionalOffset",
                                       unreal.Vector(fx, fy, fz))
                setups.append(ws)
            movement.set_editor_property("WheelSetups", setups)
            log("  Wheel setups configured (4 wheels, offset-based).")

    unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
    log("BP_RacingVehicle saved.")

# ── create test level ─────────────────────────────────────────────────────────

def create_test_level(bp):
    map_full = MAP_PATH + "/TestTrack"
    if asset_exists(map_full):
        log("TestTrack already exists.")
        return

    unreal.EditorLevelLibrary.new_level(map_full)
    log("TestTrack created.")

    # Sun
    sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight,
        unreal.Vector(0, 0, 1000),
        unreal.Rotator(-50, -100, 0)
    )
    if sun:
        c = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if c:
            c.set_editor_property("Intensity", 10.0)
            c.set_editor_property("AtmosphereSunLight", True)

    # Sky
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0,0,0), unreal.Rotator(0,0,0))
    sky_l = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0,0,500), unreal.Rotator(0,0,0))
    if sky_l:
        c = sky_l.get_component_by_class(unreal.SkyLightComponent)
        if c:
            c.set_editor_property("RealTimeCapture", True)

    # Flat track — scaled cube
    ground = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0,0,-50), unreal.Rotator(0,0,0))
    if ground:
        c = ground.get_component_by_class(unreal.StaticMeshComponent)
        if c:
            cube = unreal.load_asset("/Engine/BasicShapes/Cube")
            if cube:
                c.set_editor_property("StaticMesh", cube)
        ground.set_actor_scale3d(unreal.Vector(200.0, 200.0, 0.5))

    # Player Start
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0,0,200), unreal.Rotator(0,0,0))

    # Vehicle
    if bp and bp.generated_class():
        v = unreal.EditorLevelLibrary.spawn_actor_from_class(
            bp.generated_class(),
            unreal.Vector(0, 0, 150),
            unreal.Rotator(0, 0, 0)
        )
        if v:
            log("  Vehicle placed in TestTrack.")

    unreal.EditorLevelLibrary.save_current_level()
    log("TestTrack saved.")

# ── main ─────────────────────────────────────────────────────────────────────

def main():
    log("=" * 50)
    log("SimRacingGame first-time setup starting...")
    log("=" * 50)

    make_dirs(ASSET_BASE, WHEEL_PATH, BP_PATH, MAP_PATH)

    mesh, phys, anim = find_vehicle_mesh()

    bp = create_vehicle_blueprint()
    configure_vehicle_blueprint(bp, mesh, phys, anim)
    create_test_level(bp)

    log("=" * 50)
    log("DONE. Open TestTrack and press Play to drive!")
    log("Controls: WASD=drive, Space=handbrake, V=camera, R=reset")
    log("=" * 50)

main()
