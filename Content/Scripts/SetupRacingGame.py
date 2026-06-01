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

# Cuando True, el script borra y recrea BP_RacingVehicle para aplicar
# la configuración correcta de ruedas (nombres de bones + offsets).
# Cambiar a False una vez que el BP esté configurado correctamente.
FORCE_RECONFIGURE_BP = True

# Nombres de bones del esqueleto SportsCar_Skeleton que corresponden a los hubs de rueda.
# Extraídos del asset Sport_Car_Skeleton.uasset (Phys_Wheel_FL/FR/BL/BR).
# OJO: el esqueleto usa "BL/BR" (Back) no "RL/RR" (Rear).
WHEEL_BONE_NAMES = {
    "FL": "Phys_Wheel_FL",
    "FR": "Phys_Wheel_FR",
    "RL": "Phys_Wheel_BL",   # B = Back = trasero izquierdo
    "RR": "Phys_Wheel_BR",   # B = Back = trasero derecho
}

# Offset adicional al bone position (en cm). Con los bones correctos,
# el offset es (0,0,0) — el bone ya está en el centro del hub.
WHEEL_OFFSETS = {
    "FL": (0.0, 0.0, 0.0),
    "FR": (0.0, 0.0, 0.0),
    "RL": (0.0, 0.0, 0.0),
    "RR": (0.0, 0.0, 0.0),
}

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

def _auto_bone_names(mesh):
    """
    Detecta los bones de rueda escaneando el skeleton.
    Usa WHEEL_BONE_NAMES como fallback hardcodeado si la detección falla.
    IMPORTANTE: CanCreateVehicle() devuelve false si BoneName == NAME_None.
    """
    if not mesh:
        log("  _auto_bone_names: sin mesh — usando hardcoded.")
        return WHEEL_BONE_NAMES.copy()

    try:
        sk = mesh.get_skeleton()
        if not sk:
            log("  _auto_bone_names: skeleton no encontrado — usando hardcoded.")
            return WHEEL_BONE_NAMES.copy()

        orig = []
        for i in range(sk.get_num_bones()):
            orig.append(str(sk.get_bone_name(i)))
        lower = [n.lower() for n in orig]
        log(f"  Skeleton: {len(orig)} bones — {', '.join(orig[:12])}{'...' if len(orig)>12 else ''}")

        def pick(*keywords):
            """Primero exact match, luego substring (case-insensitive)."""
            for kw in keywords:
                kl = kw.lower()
                # Exact match
                try:
                    idx = lower.index(kl)
                    return orig[idx]
                except ValueError:
                    pass
            for kw in keywords:
                kl = kw.lower()
                # Substring match
                for i, bl in enumerate(lower):
                    if kl in bl:
                        return orig[i]
            return ""

        names = {
            "FL": pick("phys_wheel_fl", "wheel_fl", "fl_wheel", "wheel_front_left", "front_left"),
            "FR": pick("phys_wheel_fr", "wheel_fr", "fr_wheel", "wheel_front_right", "front_right"),
            # SportsCar usa BL/BR (Back) para trasero; también probamos RL/RR
            "RL": pick("phys_wheel_bl", "wheel_bl", "bl_wheel", "phys_wheel_rl", "wheel_rl", "wheel_rear_left", "rear_left"),
            "RR": pick("phys_wheel_br", "wheel_br", "br_wheel", "phys_wheel_rr", "wheel_rr", "wheel_rear_right", "rear_right"),
        }

        for pos, n in names.items():
            if n:
                log(f"  Bone {pos}: '{n}' ✓")
            else:
                fallback = WHEEL_BONE_NAMES.get(pos, "")
                log(f"  Bone {pos}: NOT FOUND — fallback hardcoded: '{fallback}'")
                names[pos] = fallback

        return names

    except Exception as e:
        log(f"  _auto_bone_names excepción: {e} — usando hardcoded.")
        return WHEEL_BONE_NAMES.copy()


def create_vehicle_blueprint():
    full = BP_PATH + "/BP_RacingVehicle"

    if asset_exists(full):
        if FORCE_RECONFIGURE_BP:
            # Borrar y recrear para que la configuración de ruedas se aplique limpiamente.
            # Esto es seguro porque el nivel se recrea en create_test_level().
            log("BP_RacingVehicle existe — borrando para reconfigurar (FORCE_RECONFIGURE_BP=True)...")
            unreal.EditorAssetLibrary.delete_asset(full)
            log("  BP_RacingVehicle borrado.")
        else:
            # Modo conservador: no tocar CDO post-Live Coding (puede crashear con array OOB).
            log("BP_RacingVehicle ya existe — omitiendo (FORCE_RECONFIGURE_BP=False).")
            return None

    parent = unreal.load_class(None, "/Script/SimRacingGame.RacingVehiclePawn")
    if not parent:
        log("ERROR: RacingVehiclePawn no encontrado. ¿Compiló el C++?")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", parent)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_RacingVehicle", BP_PATH, unreal.Blueprint, factory)
    if not bp:
        log("ERROR: no se pudo crear BP_RacingVehicle")
        return None
    log("BP_RacingVehicle creado.")
    return bp


def configure_vehicle_blueprint(bp, mesh, phys, anim):
    if not bp or not bp.generated_class():
        return

    front_cls = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelFront")
    rear_cls  = unreal.load_class(None, "/Script/SimRacingGame.RacingWheelRear")
    if not front_cls or not rear_cls:
        log("ERROR: clases de rueda no encontradas.")
        return

    # Auto-detectar bones de rueda desde el skeleton
    bone_names = _auto_bone_names(mesh)

    with unreal.ScopedEditorTransaction("Configure BP_RacingVehicle"):
        cdo = unreal.get_default_object(bp.generated_class())
        if not cdo:
            log("CDO no disponible aún.")
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

        movement = cdo.get_component_by_class(unreal.ChaosWheeledVehicleMovementComponent)
        if movement:
            # Cada WheelSetup DEBE tener BoneName != "" o CanCreateVehicle() devuelve false
            # y el vehículo físico nunca se crea (el coche se ve pero no se mueve).
            wheel_defs = [
                (front_cls, "FL"),
                (front_cls, "FR"),
                (rear_cls,  "RL"),
                (rear_cls,  "RR"),
            ]
            setups = unreal.Array(unreal.ChaosWheelSetup)
            for cls, pos in wheel_defs:
                ws    = unreal.ChaosWheelSetup()
                bname = bone_names[pos]
                ox, oy, oz = WHEEL_OFFSETS[pos]
                ws.set_editor_property("WheelClass", cls)
                ws.set_editor_property("BoneName",   bname)
                ws.set_editor_property("AdditionalOffset", unreal.Vector(ox, oy, oz))
                setups.append(ws)
                log(f"  WheelSetup {pos}: bone='{bname}' offset=({ox},{oy},{oz})")
            movement.set_editor_property("WheelSetups", setups)

            try:
                engine = movement.get_editor_property("EngineSetup")
                engine.set_editor_property("MaxTorque", 600.0)
                engine.set_editor_property("MaxRPM", 7000.0)
                engine.set_editor_property("EngineIdleRPM", 1200.0)
                movement.set_editor_property("EngineSetup", engine)
                log("  Motor: 600 Nm / 7000 RPM.")
            except Exception as e:
                log(f"  Motor config omitida: {e}")

            try:
                trans = movement.get_editor_property("TransmissionSetup")
                trans.set_editor_property("bUseAutomaticGears", True)
                trans.set_editor_property("FinalRatio", 3.5)
                movement.set_editor_property("TransmissionSetup", trans)
                log("  Transmisión automática, ratio 3.5.")
            except Exception as e:
                log(f"  Transmisión config omitida: {e}")

            log("  Ruedas configuradas con bones correctos.")

    unreal.EditorAssetLibrary.save_asset(BP_PATH + "/BP_RacingVehicle")
    log("BP_RacingVehicle guardado.")

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
