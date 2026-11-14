param([string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE', [string]$CompilerDirectory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Join-Path $repo 'artifacts/wamr/source'
$build = Join-Path $repo 'artifacts/wamr_board/build'
$revision = 'ffc22889948b1f063d7258d34d4e636ee0914925'
$gcc = if ($CompilerDirectory) { Join-Path $CompilerDirectory 'arm-none-eabi-gcc.exe' }
else { (Get-ChildItem "$CubeIdeRoot/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-gcc.exe" | Select-Object -First 1).FullName }
if (-not $gcc) { throw 'ARM GCC not found' }
if (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
    throw "Prepare pinned WAMR checkout at $source as described in WAMR_BOARD.md"
}
$actual = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -or $actual -ne $revision) { throw "WAMR must be pinned to $revision" }
$changes = & git -C $source status --porcelain --untracked-files=no
if ($LASTEXITCODE -or $changes) { throw 'WAMR checkout must be unmodified' }
& cmake -S "$repo/tools/wamr_board" -B $build -G Ninja '-DCMAKE_SYSTEM_NAME=Generic' '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY' "-DCMAKE_C_COMPILER=$gcc" "-DCMAKE_ASM_COMPILER=$gcc" "-DWAMR_ROOT_DIR=$($source.Replace('\','/'))" "-DFW_ROOT=$($repo.Replace('\','/'))"
if ($LASTEXITCODE) { throw 'WAMR configuration failed' }
& cmake --build $build -j 8
if ($LASTEXITCODE) { throw 'WAMR compilation failed' }
