param(
    [string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE',
    [string]$BuildWorkspace = (Join-Path $env:TEMP 'servo-repro-build')
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$ide = Join-Path $CubeIdeRoot 'stm32cubeidec.exe'
foreach ($target in @('BLE_ApplicationInstallManager/Release', 'BleServoController/Debug')) {
    & $ide --launcher.suppressErrors -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data $BuildWorkspace -import (Join-Path $repoRoot 'STM32CubeIDE') -import (Join-Path $repoRoot 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE') -cleanBuild $target
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $target" }
}
