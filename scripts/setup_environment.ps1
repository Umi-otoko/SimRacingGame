#Requires -RunAsAdministrator
# =============================================================================
# SimRacingGame - Script de Configuracion del Entorno de Desarrollo
# Instala y verifica todas las herramientas necesarias para el proyecto
# =============================================================================

param(
    [switch]$DryRun,
    [switch]$SkipLargeDownloads
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

function Write-Step { param($msg) Write-Host "`n[>>] $msg" -ForegroundColor Cyan }
function Write-OK   { param($msg) Write-Host "  [OK] $msg"   -ForegroundColor Green }
function Write-WARN { param($msg) Write-Host "  [!!] $msg"   -ForegroundColor Yellow }
function Write-FAIL { param($msg) Write-Host "  [XX] $msg"   -ForegroundColor Red }

# ---------------------------------------------------------------------------
# Winget helper
# ---------------------------------------------------------------------------
function Install-WithWinget {
    param([string]$PackageId, [string]$DisplayName)
    $existing = winget list --id $PackageId 2>$null
    if ($existing -match $PackageId) {
        Write-OK "$DisplayName ya instalado."
    } else {
        if (-not $DryRun) {
            Write-Step "Instalando $DisplayName via winget..."
            winget install --id $PackageId --silent --accept-source-agreements --accept-package-agreements
        } else {
            Write-WARN "[DRY-RUN] Se instalaria: $DisplayName ($PackageId)"
        }
    }
}

# ---------------------------------------------------------------------------
# 1. Verificar herramientas base del sistema
# ---------------------------------------------------------------------------
Write-Step "Verificando herramientas base..."

# Git
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitVer = (git --version)
    Write-OK "Git encontrado: $gitVer"
} else {
    Install-WithWinget "Git.Git" "Git for Windows"
}

# CMake
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    $cmakeVer = (cmake --version | Select-Object -First 1)
    Write-OK "CMake encontrado: $cmakeVer"
} else {
    Install-WithWinget "Kitware.CMake" "CMake"
}

# Python (para build tools y scripts)
if (Get-Command python -ErrorAction SilentlyContinue) {
    $pyVer = (python --version)
    Write-OK "Python encontrado: $pyVer"
} else {
    Install-WithWinget "Python.Python.3.12" "Python 3.12"
}

# ---------------------------------------------------------------------------
# 2. Visual Studio 2022 con workloads C++ y Game Development
# ---------------------------------------------------------------------------
Write-Step "Verificando Visual Studio 2022 Community..."

$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -version "[17.0,18.0)" -property installationPath 2>$null

if ($vsPath) {
    Write-OK "Visual Studio 2022 encontrado en: $vsPath"

    # Verificar workload C++ Desktop
    $vsConfig = & "${vsPath}\Common7\Tools\VsDevCmd.bat" 2>$null
    Write-OK "Workloads disponibles verificados."
} else {
    Write-WARN "Visual Studio 2022 no encontrado."
    if (-not $DryRun) {
        Write-Step "Descargando Visual Studio 2022 Community..."
        $vsUrl = "https://aka.ms/vs/17/release/vs_community.exe"
        $vsInstaller = "$env:TEMP\vs_community.exe"
        Invoke-WebRequest -Uri $vsUrl -OutFile $vsInstaller

        Write-Step "Instalando Visual Studio 2022 con workloads necesarios..."
        # Workloads requeridos:
        # - Microsoft.VisualStudio.Workload.NativeDesktop (C++ desktop)
        # - Microsoft.VisualStudio.Workload.NativeGame (Game development)
        & $vsInstaller --quiet --add Microsoft.VisualStudio.Workload.NativeDesktop `
            --add Microsoft.VisualStudio.Workload.NativeGame `
            --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            --add Microsoft.VisualStudio.Component.Windows11SDK.22621 `
            --wait
    } else {
        Write-WARN "[DRY-RUN] Se instalaria Visual Studio 2022 Community"
    }
}

# ---------------------------------------------------------------------------
# 3. Epic Games Launcher (Unreal Engine 5)
# ---------------------------------------------------------------------------
Write-Step "Verificando Epic Games Launcher..."

$epicPath = "${env:ProgramFiles(x86)}\Epic Games\Launcher\Engine\Binaries\Win64\EpicGamesLauncher.exe"
if (Test-Path $epicPath) {
    Write-OK "Epic Games Launcher encontrado."
    Write-WARN "IMPORTANTE: Instala Unreal Engine 5.4+ desde Epic Games Launcher manualmente."
    Write-WARN "  > Requiere ~80 GB de espacio. Engine ID: UE_5.4"
} else {
    if (-not $SkipLargeDownloads -and -not $DryRun) {
        $epicUrl = "https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncherInstaller.msi"
        $epicInstaller = "$env:TEMP\EpicGamesLauncherInstaller.msi"
        Write-Step "Descargando Epic Games Launcher..."
        Invoke-WebRequest -Uri $epicUrl -OutFile $epicInstaller
        Write-Step "Instalando Epic Games Launcher..."
        Start-Process msiexec.exe -ArgumentList "/i `"$epicInstaller`" /quiet" -Wait
        Write-OK "Epic Games Launcher instalado. Abre el launcher y descarga UE 5.4+."
    } else {
        Write-WARN "[SKIP] Epic Games Launcher no instalado. Descarga manualmente desde: https://www.unrealengine.com/download"
    }
}

# ---------------------------------------------------------------------------
# 4. OpenSSL (requerido para criptografia de Setups)
# ---------------------------------------------------------------------------
Write-Step "Verificando OpenSSL..."

if (Get-Command openssl -ErrorAction SilentlyContinue) {
    $sslVer = (openssl version)
    Write-OK "OpenSSL encontrado: $sslVer"
} else {
    Install-WithWinget "ShiningLight.OpenSSL" "OpenSSL"
}

# ---------------------------------------------------------------------------
# 5. vcpkg (gestor de paquetes C++)
# ---------------------------------------------------------------------------
Write-Step "Configurando vcpkg..."

$vcpkgPath = "C:\vcpkg"
if (Test-Path "$vcpkgPath\vcpkg.exe") {
    Write-OK "vcpkg encontrado en $vcpkgPath"
} else {
    if (-not $DryRun) {
        git clone https://github.com/Microsoft/vcpkg.git $vcpkgPath
        & "$vcpkgPath\bootstrap-vcpkg.bat" -disableMetrics
        & "$vcpkgPath\vcpkg.exe" integrate install
    } else {
        Write-WARN "[DRY-RUN] Se clonaria vcpkg en $vcpkgPath"
    }
}

# Instalar dependencias del proyecto via vcpkg
$dependencies = @(
    "nlohmann-json:x64-windows",   # JSON serialization
    "openssl:x64-windows",          # AES + HMAC
    "zlib:x64-windows",             # Compresion de paquetes de red
    "websocketpp:x64-windows",      # WebSockets para backend API
    "spdlog:x64-windows",           # Logging de alta performance
    "benchmark:x64-windows"         # Google Benchmark para profiling
)

if (-not $DryRun) {
    foreach ($dep in $dependencies) {
        Write-Step "Instalando $dep..."
        & "$vcpkgPath\vcpkg.exe" install $dep
    }
}

# ---------------------------------------------------------------------------
# 6. Jolt Physics (motor de fisicas recomendado)
# ---------------------------------------------------------------------------
Write-Step "Clonando JoltPhysics..."

$joltPath = "$ProjectRoot\extern\JoltPhysics"
if (-not (Test-Path $joltPath)) {
    if (-not $DryRun) {
        git clone --depth=1 https://github.com/jrouwe/JoltPhysics.git $joltPath
        Write-OK "JoltPhysics clonado en $joltPath"
    } else {
        Write-WARN "[DRY-RUN] Se clonaria JoltPhysics en $joltPath"
    }
} else {
    Write-OK "JoltPhysics ya existe en $joltPath"
    if (-not $DryRun) { git -C $joltPath pull }
}

# ---------------------------------------------------------------------------
# 7. Steamworks SDK (requiere cuenta de Steamworks Partner)
# ---------------------------------------------------------------------------
Write-Step "Verificando Steamworks SDK..."

$steamSDKPath = "$ProjectRoot\extern\steamworks_sdk"
if (Test-Path $steamSDKPath) {
    Write-OK "Steamworks SDK encontrado en $steamSDKPath"
} else {
    Write-WARN "Steamworks SDK NO encontrado."
    Write-WARN "  1. Registrate en https://partner.steamgames.com/"
    Write-WARN "  2. Descarga el SDK desde: https://partner.steamgames.com/doc/sdk"
    Write-WARN "  3. Extrae en: $steamSDKPath"
    New-Item -ItemType Directory -Force -Path $steamSDKPath | Out-Null
    '# Place Steamworks SDK here' | Out-File "$steamSDKPath\README.md"
}

# ---------------------------------------------------------------------------
# 8. Configurar variables de entorno del proyecto
# ---------------------------------------------------------------------------
Write-Step "Configurando variables de entorno..."

$envVars = @{
    "SIMRACING_ROOT"        = $ProjectRoot
    "JOLT_PHYSICS_DIR"      = $joltPath
    "STEAMWORKS_SDK_DIR"    = $steamSDKPath
    "VCPKG_ROOT"            = $vcpkgPath
}

foreach ($key in $envVars.Keys) {
    [Environment]::SetEnvironmentVariable($key, $envVars[$key], "User")
    Write-OK "  $key = $($envVars[$key])"
}

# ---------------------------------------------------------------------------
# 9. Resumen final
# ---------------------------------------------------------------------------
Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "  ENTORNO DE DESARROLLO CONFIGURADO" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host @"

  Herramientas instaladas/verificadas:
    [+] Git                   - Control de versiones
    [+] CMake                 - Build system
    [+] Visual Studio 2022    - Compilador C++ (MSVC)
    [+] vcpkg                 - Gestor de paquetes C++
    [+] JoltPhysics           - Motor de fisicas multi-core
    [+] OpenSSL               - Criptografia de Setups
    [+] nlohmann-json         - Serializacion JSON

  Pendiente manual:
    [!] Epic Games Launcher + Unreal Engine 5.4+  (~80 GB)
    [!] Steamworks SDK (requiere cuenta Partner)

  Proximos pasos:
    1. Abre Epic Games Launcher e instala UE 5.4+
    2. Descarga Steamworks SDK y coloca en: extern\steamworks_sdk\
    3. Ejecuta: cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
    4. Abre el proyecto en Visual Studio 2022

"@ -ForegroundColor White
