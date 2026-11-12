param([string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repoRoot 'artifacts/ota'
New-Item -ItemType Directory -Force -Path $output | Out-Null
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -CubeIdeRoot $CubeIdeRoot
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
    $nm = Get-ChildItem -Path (Join-Path $CubeIdeRoot 'plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools*/tools/bin/arm-none-eabi-nm.exe') | Select-Object -First 1 -ExpandProperty FullName
    if (-not $nm) { throw 'ARM nm tool not found' }
    & python (Join-Path $PSScriptRoot 'validate_firmware.py') --nm $nm
    if ($LASTEXITCODE -ne 0) { throw 'Firmware layout validation failed' }
    & python (Join-Path $PSScriptRoot 'ota_package.py') --output $output
    if ($LASTEXITCODE -ne 0) { throw 'OTA packaging failed' }
    $latest = Get-Content -Raw -LiteralPath (Join-Path $output 'latest.json') | ConvertFrom-Json
    Write-Host "`nOTA FILE (select this file in the website):" -ForegroundColor Green
    Write-Host $latest.path
    Write-Host $latest.file_uri
    Write-Host "Version: $($latest.version)  CRC32: $($latest.crc32)  Bytes: $($latest.length)"
    Write-Host 'First installation only: powershell -File tools/flash.ps1'
    exit 0
} catch {
    Write-Error $_
    exit 1
}
