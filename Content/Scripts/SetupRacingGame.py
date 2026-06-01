"""
SetupRacingGame.py — auto-runs at editor startup
Crea/recrea: BP_RacingVehicle + TestTrack con GameMode override.

Circuito: óvalo de ~1.3 km con rectas largas, isla interior y barreras.
El vehículo se spawnea en el PlayerStart por el GameMode al hacer Play.
"""

import unreal

ASSET_BASE = "/Game/SimRacing"
WHEEL_PATH = ASSET_BASE + "/Wheels"
BP_PATH    = ASSET_BASE + "/Blueprints"
MAP_PATH   = ASSET_BASE + "/Maps"

MESH_CANDIDATES = [
    ("/ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar",
     "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_PhysicsAsset",
     "/ChaosModularVehicleExamples/Models/SportsCar/SportsCar_AnimBP"),
    ("/ControlRigModules/Modules/Meshes/SKM_Car_Template", None, None),
]

def log(msg):
    unreal.log(f"[SetupRacing] {msg}")

def asset_exists(p):
    return unreal.EditorAssetLibrary.does_asset_exist(p)

def make_dirs(*paths):
    for p in paths:
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)

# ── Mesh del vehículo ─────────────────────────────────────────────────────────

def find_vehicle_mesh():
    for mesh_path, phys_path, anim_path in MESH_CANDIDATES:
        mesh = unreal.load_asset(mesh_path)
        if mesh:
            log(f"Mesh encontrada: {mesh_path}")
            phys = unreal.load_asset(phys_path) if phys_path else None
            anim = unreal.load_class(None, anim_path + "_C") if anim_path else None
            return mesh, phys, anim
    log("WARNING: No se encontró mesh de vehículo. Asignarla manualmente en BP_RacingVehicle.")
    return None, None, None

# ── Blueprint del vehículo ────────────────────────────────────────────────────
# Los WheelSetups (bones Phys_Wheel_FL/FR/BL/BR) y la config de motor/transmisión
# se configuran directamente en el constructor de ARacingVehiclePawn (C++).
# Este script solo crea el BP si no existe y le asigna la mesh.

def create_vehicle_blueprint():
    """Crea BP_RacingVehicle si no existe. Solo se ejecuta una vez."""
    full = BP_PATH + "/BP_RacingVehicle"
    if asset_exists(full):
        log("BP_RacingVehicle ya existe — no se toca.")
        return None  # None = ya existía, no reconfigurar

    parent = unreal.load_class(None, "/Script/SimRacingGame.RacingVehiclePawn")
    if not parent:
        log("ERROR: RacingVehiclePawn no encontrado. Compila el C++ primero.")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", parent)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_RacingVehicle", BP_PATH, unreal.Blueprint, factory)
    if not bp:
        log("ERROR: no se pudo crear BP_RacingVehicle.")
        return None
    log("BP_RacingVehicle creado.")
    return bp


def configure_vehicle_blueprint(bp, mesh, phys, anim):
    """Asigna mesh y PhysicsAsset al BP recién creado. Las ruedas vienen del C++."""
    if not bp or not bp.generated_class():
        return

    with unreal.ScopedEditorTransaction("Assign mesh to BP_RacingVehicle"):
        cdo = unreal.get_default_object(bp.generated_class())
        if not cdo:
            log("  CDO no disponible — asigna la mesh manualmente en el BP.")
            unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
            return

        mesh_comp = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
        if mesh_comp and mesh:
            for prop in ("SkeletalMeshAsset", "SkeletalMesh"):
                try:
                    mesh_comp.set_editor_property(prop, mesh)
                    log(f"  Mesh asignada ({prop}).")
                    break
                except Exception:
                    pass
            if anim:
                try:
                    mesh_comp.set_editor_property("AnimClass", anim)
                    log("  AnimBP asignado.")
                except Exception as e:
                    log(f"  AnimBP omitido: {e}")

        if mesh_comp and phys:
            try:
                mesh_comp.set_editor_property("PhysicsAsset", phys)
                log("  PhysicsAsset asignado.")
            except Exception:
                pass

    unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
    log("BP_RacingVehicle guardado. Ruedas/motor configurados en C++ (ARacingVehiclePawn).")

# ── Nivel ─────────────────────────────────────────────────────────────────────

def _new_level(path):
    try:
        ss = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if ss:
            ss.new_level(path)
            return
    except Exception:
        pass
    unreal.EditorLevelLibrary.new_level(path)

def _set_prop(obj, *names, value):
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            pass
    return False

def _spawn(cls, loc, rot):
    return unreal.EditorLevelLibrary.spawn_actor_from_class(cls, loc, rot)

def set_game_mode_override():
    gm_class = unreal.load_class(None, "/Script/SimRacingGame.RacingGameMode")
    if not gm_class:
        log("  WARNING: RacingGameMode no encontrado.")
        return
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        ws    = world.get_world_settings()
        ws.set_editor_property("DefaultGameMode", gm_class)
        log("  GameMode override: RacingGameMode ✓")
    except Exception as e:
        log(f"  GameMode override falló: {e}")

# ── Constructor de circuito ───────────────────────────────────────────────────

CUBE = None  # cargado una vez en create_test_level

def _box(cx, cy, cz, sx, sy, sz):
    """Spawnea un cubo estático con centro en (cx,cy,cz) y escala (sx,sy,sz).
    Unidades: cm para posición, metros para escala (1 unidad escala = 100 cm).
    """
    actor = _spawn(unreal.StaticMeshActor,
                   unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0))
    if actor:
        c = actor.get_component_by_class(unreal.StaticMeshComponent)
        if c and CUBE:
            _set_prop(c, "StaticMesh", value=CUBE)
        actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
    return actor

def build_circuit():
    """
    Circuito oval de ~1.3 km.

    Coordenadas (vista desde arriba, eje Z hacia arriba):
      X = derecha, Y = arriba, Z = arriba

    Layout:
      Recta delantera (sur):  Y =     0, X de -35000 a 35000
      Recta trasera  (norte): Y = 35000, X de -35000 a 35000
      Conector este:          X = 41000, Y de -7000  a 42000
      Conector oeste:         X =-41000, Y de -7000  a 42000

      Ancho de pista: ~130m (13000 cm) en todas las secciones.
      Isla interior:  620m × 200m, ligeramente elevada.
    """

    log("  Construyendo circuito...")

    FLOOR_Z   = -75.0   # centro del suelo base
    TRACK_Z   = -25.0   # centro de las losas de pista (ligeramente más alto)
    ISLAND_Z  =  50.0   # centro de la isla interior (1.5m sobre la pista)
    BARRIER_Z =  75.0   # centro de las barreras (base en -50, top en 200 ≈ 2.5m)

    # ── Suelo exterior ──────────────────────────────────────────────────────
    # 1400m × 900m, 50cm de grosor
    _box(0, 17500, FLOOR_Z, 1400, 900, 0.5)

    # ── Superficies de la pista (ligeramente elevadas sobre el suelo) ───────
    # Recta delantera: 700m largo, 120m ancho
    _box(0, 0, TRACK_Z, 700, 120, 0.5)
    # Recta trasera
    _box(0, 35000, TRACK_Z, 700, 120, 0.5)
    # Conector este: 120m ancho, 490m largo
    _box(41000, 17500, TRACK_Z, 120, 490, 0.5)
    # Conector oeste
    _box(-41000, 17500, TRACK_Z, 120, 490, 0.5)
    # Parches en esquinas (rellena la transición curva→recta)
    _box(41000,  0,     TRACK_Z, 120, 140, 0.5)
    _box(41000,  35000, TRACK_Z, 120, 140, 0.5)
    _box(-41000, 0,     TRACK_Z, 120, 140, 0.5)
    _box(-41000, 35000, TRACK_Z, 120, 140, 0.5)

    # ── Isla interior ───────────────────────────────────────────────────────
    # 620m × 190m, ligeramente elevada (2m sobre pista)
    _box(0, 17500, ISLAND_Z, 620, 190, 2.0)

    # ── Barreras exteriores ─────────────────────────────────────────────────
    # Sur exterior: Y=-8000, longitud 1020m (cubre todo el ancho)
    _box(0,      -8000, BARRIER_Z, 1020, 2, 2.5)
    # Norte exterior: Y=43000
    _box(0,      43000, BARRIER_Z, 1020, 2, 2.5)
    # Este exterior: X=50000, cubre Y de -8000 a 43000 = 510m
    _box(50000,  17500, BARRIER_Z, 2, 510, 2.5)
    # Oeste exterior: X=-50000
    _box(-50000, 17500, BARRIER_Z, 2, 510, 2.5)

    # ── Barreras interiores (borde de la isla) ──────────────────────────────
    # Sur interior: Y=8000
    _box(0,      8000,  BARRIER_Z, 620, 2, 2.5)
    # Norte interior: Y=27000
    _box(0,      27000, BARRIER_Z, 620, 2, 2.5)
    # Este interior: X=32000, Y de 8000 a 27000 = 190m
    _box(32000,  17500, BARRIER_Z, 2, 190, 2.5)
    # Oeste interior: X=-32000
    _box(-32000, 17500, BARRIER_Z, 2, 190, 2.5)

    # ── Pórtico de salida/meta ──────────────────────────────────────────────
    # Posición: X=-30000 (300m desde el inicio, en la recta delantera)
    GANTRY_X   = -28000.0
    PILLAR_Z   = 350.0     # centro del pilar (7m de alto, base en -50 → top en 650)
    PILLAR_H   = 7.0       # metros
    _box(GANTRY_X, -7500, PILLAR_Z, 2, 2, PILLAR_H)   # pilar sur
    _box(GANTRY_X,  7500, PILLAR_Z, 2, 2, PILLAR_H)   # pilar norte
    _box(GANTRY_X,  0,    650,      2, 160, 2)         # viga horizontal

    # ── Luz solar ───────────────────────────────────────────────────────────
    sun = _spawn(unreal.DirectionalLight,
                 unreal.Vector(0, 0, 10000), unreal.Rotator(-50, -100, 0))
    if sun:
        c = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if c:
            _set_prop(c, "Intensity", value=10.0)
            _set_prop(c, "bAtmosphereSunLight", "AtmosphereSunLight", value=True)

    # ── Atmósfera ───────────────────────────────────────────────────────────
    try:
        _spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    except Exception:
        pass

    sky = _spawn(unreal.SkyLight, unreal.Vector(0, 0, 5000), unreal.Rotator(0, 0, 0))
    if sky:
        c = sky.get_component_by_class(unreal.SkyLightComponent)
        if c:
            _set_prop(c, "RealTimeCapture", "bRealTimeCapture", value=True)

    log("  Circuito construido.")

def _set_box_extent(box, x, y, z):
    """Asigna la semi-extensión (half-extent) de un UBoxComponent."""
    v = unreal.Vector(x, y, z)
    try:
        box.set_box_extent(v, False)
        return
    except Exception:
        pass
    try:
        box.set_editor_property("BoxExtent", v)
    except Exception as e:
        log(f"    WARNING: set_box_extent falló: {e}")


def _place_checkpoints():
    """
    Spawnea 4 ARacingCheckpoint + 1 ARacingFinishLine en el circuito.

    El circuito va en sentido horario visto desde arriba:
      Recta delantera: dirección +X (este)
      Conector este:   dirección +Y (norte)
      Recta trasera:   dirección -X (oeste)
      Conector oeste:  dirección -Y (sur)

    Checkpoints colocados a mitad de cada sección.
    Finish line debajo del pórtico (X=-28000).
    """
    cp_cls = unreal.load_class(None, "/Script/SimRacingGame.RacingCheckpoint")
    fl_cls = unreal.load_class(None, "/Script/SimRacingGame.RacingFinishLine")

    if not cp_cls or not fl_cls:
        log("  WARNING: RacingCheckpoint o RacingFinishLine no encontrados.")
        log("           Compila el C++ primero, luego vuelve a ejecutar este script.")
        return

    # (index, cx, cy, cz,  ext_x, ext_y, ext_z)
    # ext = semi-extensión del BoxComponent (half-extent en cm)
    # Rectas alineadas con X: el coche viaja en X → trigger delgado en X, ancho en Y
    # Conectores alineados con Y: el coche viaja en Y → trigger delgado en Y, ancho en X
    checkpoint_data = [
        (0,  10000,     0, 200,  150, 6500, 400),  # recta delantera (mitad este)
        (1,  41000, 17500, 200, 6500,  150, 400),  # conector este
        (2,      0, 35000, 200,  150, 6500, 400),  # recta trasera (centro)
        (3, -41000, 17500, 200, 6500,  150, 400),  # conector oeste
    ]

    for idx, cx, cy, cz, ex, ey, ez in checkpoint_data:
        cp = _spawn(cp_cls, unreal.Vector(cx, cy, cz), unreal.Rotator(0, 0, 0))
        if cp:
            try:
                cp.set_editor_property("CheckpointIndex", idx)
            except Exception as e:
                log(f"    WARNING: no se pudo asignar CheckpointIndex: {e}")
            box = cp.get_component_by_class(unreal.BoxComponent)
            if box:
                _set_box_extent(box, ex, ey, ez)
            log(f"  CP{idx} → ({cx}, {cy}, {cz})  ext ({ex}, {ey}, {ez})")
        else:
            log(f"  ERROR: no se pudo spawnear CP{idx}")

    # Finish line debajo del pórtico (mismo X que los pilares)
    GANTRY_X = -28000.0
    fl = _spawn(fl_cls, unreal.Vector(GANTRY_X, 0, 200), unreal.Rotator(0, 0, 0))
    if fl:
        box = fl.get_component_by_class(unreal.BoxComponent)
        if box:
            _set_box_extent(box, 150, 6500, 400)
        log(f"  FinishLine → ({GANTRY_X}, 0, 200)  ext (150, 6500, 400)")
    else:
        log("  ERROR: no se pudo spawnear FinishLine")

    log("  Checkpoints colocados: 4 CPs + 1 FinishLine.")


def create_test_level():
    global CUBE
    map_full = MAP_PATH + "/TestTrack"

    # new_level crea o SOBREESCRIBE el nivel en esa ruta de forma segura
    # (no deletear el mapa actual — eso puede crashear con array OOB en el editor)
    _new_level(map_full)
    log("TestTrack creado/recreado.")

    set_game_mode_override()

    # Cargar mesh base para todos los bloques
    CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
    if not CUBE:
        log("WARNING: no se pudo cargar /Engine/BasicShapes/Cube — el circuito no tendrá meshes.")

    build_circuit()

    # PlayerStart en el inicio de la recta delantera
    # X=-33000 (extremo oeste de la recta, 330m del centro), Y=0, Z=200 (2m sobre pista)
    _spawn(unreal.PlayerStart, unreal.Vector(-33000, 0, 200), unreal.Rotator(0, 0, 0))
    log("  PlayerStart en (-330m, 0, 2m), orientado hacia +X.")

    _place_checkpoints()

    unreal.EditorLevelLibrary.save_current_level()
    log("TestTrack guardado.")

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    log("=" * 55)
    log("SimRacingGame — Setup iniciando...")
    log("=" * 55)

    make_dirs(ASSET_BASE, WHEEL_PATH, BP_PATH, MAP_PATH)

    mesh, phys, anim = find_vehicle_mesh()
    bp = create_vehicle_blueprint()
    if bp is not None:
        # bp es None cuando el blueprint ya existía (evita crash CDO post-recompile)
        configure_vehicle_blueprint(bp, mesh, phys, anim)
    create_test_level()

    log("=" * 55)
    log("LISTO. Presiona Play para manejar.")
    log("Controles: W/S = gas/freno  |  A/D = volante")
    log("           Espacio = freno mano  |  E/Q = cambio")
    log("           V = cambiar cámara    |  R = resetear")
    log("Circuito: ~1.3 km, 130m de ancho, con barreras.")
    log("=" * 55)

main()
