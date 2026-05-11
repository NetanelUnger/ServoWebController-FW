# FW Agent Entry Point

This firmware repo also has the older `agent.md` file, which remains the full top-level firmware map. Read it first when starting broad FW work.

For quicker local context, use these scoped notes:

- `Core/agents.md`: STM32Cube startup, FreeRTOS objects, queues, tasks, and generated-code boundaries.
- `STM32CubeIDE/Application/User/Core/agents.md`: hand-written command, servo, PCA9685, shared-I2C, and VL53L5CX application modules.
- `STM32_WPAN/App/agents.md`: BLE services, NUS transport, TOF characteristic, advertising, and connection glue.

Keep FW protocol changes synchronized with the Web app under `../ServoWebController-Web`.

Do not build this firmware repo with generated makefiles during agent work. Unless explicitly asked to build, limit changes to source/code edits and report any build verification as not run.
