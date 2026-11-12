param(
    [string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE',
    [string]$BuildWorkspace = (Join-Path $env:TEMP 'servo-repro-build')
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$ide = Join-Path $CubeIdeRoot 'stm32cubeidec.exe'
if (-not (Test-Path -LiteralPath $ide)) { throw "STM32CubeIDE not found: $ide" }
$logDir = Join-Path $repoRoot 'artifacts/ota'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
# Eclipse otherwise tries to lock/write the installation's configuration folder.
$configuration = Join-Path $repoRoot 'artifacts/cube-configuration'
if (-not (Test-Path -LiteralPath (Join-Path $configuration 'config.ini'))) {
    New-Item -ItemType Directory -Force -Path (Join-Path $configuration 'org.eclipse.equinox.simpleconfigurator') | Out-Null
    Copy-Item -LiteralPath (Join-Path $CubeIdeRoot 'configuration/config.ini') -Destination $configuration
    Copy-Item -LiteralPath (Join-Path $CubeIdeRoot 'configuration/org.eclipse.equinox.simpleconfigurator/bundles.info') -Destination (Join-Path $configuration 'org.eclipse.equinox.simpleconfigurator/bundles.info')
}
foreach ($target in @('BLE_ApplicationInstallManager/Release', 'BleServoController/Debug')) {
    $started = [DateTime]::UtcNow
    $buildLog = Join-Path $logDir ($target.Replace('/', '-') + '.log')
    & $ide --launcher.suppressErrors -nosplash -configuration $configuration -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data $BuildWorkspace -import (Join-Path $repoRoot 'STM32CubeIDE') -import (Join-Path $repoRoot 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE') -cleanBuild $target 2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath $buildLog -Pattern 'fatal error:|error:|Build Failed|BUILD FAILED|Cannot run program' -Quiet)) { throw "Build failed: $target (see $buildLog)" }
    $binary = if ($target.StartsWith('BLE_ApplicationInstallManager/')) {
        Join-Path $repoRoot 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE/Release/BLE_ApplicationInstallManager.bin'
    } else { Join-Path $repoRoot 'STM32CubeIDE/Debug/BleServoController.bin' }
    foreach ($artifact in @($binary, [IO.Path]::ChangeExtension($binary, '.elf'))) {
        if (-not (Test-Path -LiteralPath $artifact) -or (Get-Item -LiteralPath $artifact).LastWriteTimeUtc -lt $started.AddSeconds(-2)) {
            throw "Build did not produce a fresh artifact: $artifact"
        }
    }
}
