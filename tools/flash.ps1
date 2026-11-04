param([string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$programmer = Get-ChildItem (Join-Path $CubeIdeRoot 'plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32*/tools/bin/STM32_Programmer_CLI.exe') | Select-Object -First 1 -ExpandProperty FullName
if (-not $programmer) { throw 'STM32CubeProgrammer CLI not found' }
$boot = Join-Path $repoRoot 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE/Release/BLE_ApplicationInstallManager.bin'
$app = Join-Path $repoRoot 'STM32CubeIDE/Debug/BleServoController.hex'
foreach ($artifact in @($boot, $app)) {
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Build first; missing $artifact" }
}
& $programmer -c port=SWD mode=UR -w $boot 0x08000000 -v
if ($LASTEXITCODE -ne 0) { throw 'Boot manager programming/verification failed' }
& $programmer -c port=SWD mode=UR -w $app -v -rst
if ($LASTEXITCODE -ne 0) { throw 'Application programming/verification failed' }
