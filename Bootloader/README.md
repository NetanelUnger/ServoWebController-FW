# ST application install manager

Source: STMicroelectronics/STM32CubeWBA, exact revision in
`../dependencies.lock.json`, from
`Projects/NUCLEO-WBA55CG/Applications/BLE/BLE_ApplicationInstallManager`.
The official NUCLEO-WBA55CG manager uses the WBA5 flash layout and does not
initialize board-specific GPIOs. It is integrated with the STM32WBA5MMG
application and validated on the connected target via a normal reset.

| Image | Address | Reserved size |
| --- | --- | --- |
| Boot manager | `0x08000000` | `0x6000` (24 KiB) |
| Servo application | `0x08006000` | 488 KiB active slot |

The manager checks the application pointer at offset `0x160` and the completion
word `0x94448A29`. It then jumps to the application's vector table. Application
startup derives VTOR from its linker symbols. Keep these contracts synchronized.

Run `../tools/build.ps1` to build the manager's Release configuration and the
application's Debug configuration with STM32CubeIDE 2.2.0. Both builds use the
vendored HAL/CMSIS files in this repository. Other upstream IDE projects are
retained as reference and have not been adapted or tested.

Run `../tools/flash.ps1` from PowerShell to program and verify both built images,
then reset. The script does not perform a mass erase or change option bytes.
Routine application-only flashing must preserve sectors 0-2.

`Binary/BLE_ApplicationInstallManager_reference.bin` is ST's upstream reference
binary. The flash script instead uses the locally built Release binary, so it
matches the vendored source and HAL. Generated Debug/Release files are ignored.

This restores the OTA-oriented boot structure; end-to-end OTA transfer and
recovery behavior have not been tested. See the upstream README and ST license
inside the project directory.
