param(
    [string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE',
    [string]$BootloaderHex,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$programmer = Get-ChildItem (Join-Path $CubeIdeRoot 'plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32*/tools/bin/STM32_Programmer_CLI.exe') | Select-Object -First 1 -ExpandProperty FullName
if (-not $programmer) { throw 'STM32CubeProgrammer CLI not found' }
$boot = Join-Path $repoRoot 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE/Release/BLE_ApplicationInstallManager.bin'
if ($BootloaderHex) { $boot = (Resolve-Path -LiteralPath $BootloaderHex).Path }
$app = Join-Path $repoRoot 'STM32CubeIDE/Debug/BleServoController.hex'
foreach ($artifact in @($boot, $app)) {
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Build first; missing $artifact" }
    $file = Get-Item -LiteralPath $artifact
    if ($file.Length -eq 0) { throw "Empty firmware file: $artifact" }
    Write-Host ("{0}`n  Built: {1}  Size: {2} bytes" -f $file.FullName, $file.LastWriteTime, $file.Length)
}
if ($CheckOnly) { Write-Host 'Files and programmer found. No device was programmed.'; return }
if ($BootloaderHex) {
    & $programmer -c port=SWD mode=UR -w $boot -v
} else {
    & $programmer -c port=SWD mode=UR -w $boot 0x08000000 -v
}
if ($LASTEXITCODE -ne 0) { throw 'Boot manager programming/verification failed' }
& $programmer -c port=SWD mode=UR -w $app -v -rst
if ($LASTEXITCODE -ne 0) { throw 'Application programming/verification failed' }
