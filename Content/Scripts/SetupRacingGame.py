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
#   (forward, right, up)     <- Unreal X=Forward, Y=Right, Z=Up
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
            return mesh, phys, anim
    log("WARNING: No vehicle mesh found. Assign SKM_SportsCar manually in Blueprint.")
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
            log("CDO not available yet — mesh/wheel setup skipped.")
            unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
            return

        mesh_comp = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
        if mesh_comp:
            if mesh:
                try:
                    mesh_comp.set_editor_property("SkeletalMeshAsset", mesh)
                except Exception:
                    mesh_comp.set_editor_property("SkeletalMesh", mesh)
                log("  Skeletal mesh assigned.")
            if anim:
                try:
                    mesh_comp.set_editor_property("AnimClass", anim)
                    log("  Anim Blueprint assigned.")
                except Exception as e:
                    log(f"  AnimClass skipped: {e}")

        movement = cdo.get_component_by_class(unreal.ChaosWheeledVehicleMovementComponent)
        if movement:
            wheel_defs = [
                ("", front_cls, WHEEL_OFFSETS["FL"]),
                ("", front_cls, WHEEL_OFFSETS["FR"]),
                ("", rear_cls,  WHEEL_OFFSETS["RL"]),
                ("", rear_cls,  WHEEL_OFFSETS["RR"]),
            ]
            setups = unreal.Array(unreal.ChaosWheelSetup)
            for bone_name, cls, (fx, fy, fz) in wheel_defs:
                ws = unreal.ChaosWheelSetup()
                ws.set_editor_property("WheelClass", cls)
                ws.set_editor_property("BoneName", bone_name)
                ws.set_editor_property("AdditionalOffset", unreal.Vector(fx, fy, fz))
                setups.append(ws)
            movement.set_editor_property("WheelSetups", setups)
            log("  Wheel setups configured (4 wheels, offset-based).")

    unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
    log("BP_RacingVehicle saved.")

# ── create test level ─────────────────────────────────────────────────────────

def _new_level(path):
    """Create a new level at path, trying LevelEditorSubsystem first."""
    try:
        ss = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if ss:
            ss.new_level(path)
            return
    except Exception:
        pass
    unreal.EditorLevelLibrary.new_level(path)

def _set_prop(obj, *names, value):
    """Try multiple property name variants; ignore all failures."""
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            pass
    return False

def _spawn(cls, loc, rot):
    return unreal.EditorLevelLibrary.spawn_actor_from_class(cls, loc, rot)

def create_test_level(bp):
    map_full = MAP_PATH + "/TestTrack"
    temp_map = MAP_PATH + "/_SetupTemp"

    if asset_exists(map_full):
        log("TestTrack exists — deleting stale level and recreating...")
        # Open a throw-away level so TestTrack is no longer the active level
        _new_level(temp_map)
        unreal.EditorAssetLibrary.delete_asset(map_full)
        # Clean up temp
        if asset_exists(temp_map):
            try:
                unreal.EditorAssetLibrary.delete_asset(temp_map)
            except Exception:
                pass

    _new_level(map_full)
    log("TestTrack created.")

    # Sun
    sun = _spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(-50, -100, 0))
    if sun:
        c = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if c:
            _set_prop(c, "Intensity", value=10.0)
            _set_prop(c, "bAtmosphereSunLight", "AtmosphereSunLight", value=True)

    # Sky
    try:
        _spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    except Exception:
        pass
    sky_l = _spawn(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
    if sky_l:
        c = sky_l.get_component_by_class(unreal.SkyLightComponent)
        if c:
            _set_prop(c, "RealTimeCapture", "bRealTimeCapture", value=True)

    # Ground — flat 200m x 200m cube
    ground = _spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -50), unreal.Rotator(0, 0, 0))
    if ground:
        c = ground.get_component_by_class(unreal.StaticMeshComponent)
        if c:
            cube = unreal.load_asset("/Engine/BasicShapes/Cube")
            if cube:
                _set_prop(c, "StaticMesh", value=cube)
        ground.set_actor_scale3d(unreal.Vector(200.0, 200.0, 0.5))

    # Player Start
    _spawn(unreal.PlayerStart, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0))

    # Vehicle
    if bp and bp.generated_class():
        v = _spawn(bp.generated_class(), unreal.Vector(0, 0, 150), unreal.Rotator(0, 0, 0))
        if v:
            log("  Vehicle placed in TestTrack.")

    unreal.EditorLevelLibrary.save_current_level()
    log("TestTrack saved.")

# ── main ─────────────────────────────────────────────────────────────────────

def main():
    log("=" * 50)
    log("SimRacingGame setup starting...")
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
