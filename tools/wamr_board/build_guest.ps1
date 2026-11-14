param([Parameter(Mandatory=$true)][string]$Clang, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo 'artifacts/wamr_board'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $Clang --target=wasm32 -O1 -nostdlib -fno-builtin "$PSScriptRoot/guest.c" '-Wl,--no-entry' '-Wl,--strip-all' '-Wl,--export=__heap_base' '-Wl,--export=__data_end' '-Wl,-z,stack-size=2048' '-Wl,--initial-memory=65536' '-Wl,--max-memory=65536' -o "$out/guest.wasm"
if ($LASTEXITCODE) { throw 'Guest compilation failed' }
& $Python "$PSScriptRoot/embed_guest.py" "$out/guest.wasm" "$repo/STM32CubeIDE/Application/User/Core/wasm/robot_guest.inc"
if ($LASTEXITCODE) { throw 'Guest embedding failed' }
