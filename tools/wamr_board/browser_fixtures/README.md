# Uploaded-program fixtures

Open each C source in the Web C page, compile it, and save the `.wasm` output to
`artifacts/wasm_upload/<name>.wasm`. They use that page's virtual `robot.h`.
Run `tools/check_wasm_profile.py --modules artifacts/wasm_upload` for the native
preflight tests, and the explicitly gated hardware harness documented in
`WAMR_BOARD.md`. They exercise persistent data, cached sensors, current-target
motor writes and instruction-budget trapping. None chooses a new motor target.

Initial Chromium output sizes were 138/151/196/185 bytes for sensor/counter/hold/
loop. Exact bytes may vary with source edits; the firmware enforces the ABI.
