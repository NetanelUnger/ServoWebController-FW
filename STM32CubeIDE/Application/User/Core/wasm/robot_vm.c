#include "robot_vm.h"
#include "wasm_profile.h"
#include "display/image_store.h"
#include "wasm_export.h"
#include "app_freertos.h"
#include "servo/servo_task.h"
#include "IMU/imu.h"
#include "VL53L8CX/vl53l8cx_task.h"
#include <stdio.h>
#include <string.h>
#include "robot_guest.inc"

#define VM_POOL_BYTES 12288U
#define VM_WASM_STACK_BYTES 2048U
#define VM_INSTRUCTIONS_PER_STEP 20000
#define VM_PERIOD_MS 20U
/* All runtime allocations use this pool, never either firmware heap. */
static uint64_t vm_pool[VM_POOL_BYTES / sizeof(uint64_t)];
uint8_t *robot_native_stack_bottom;
static uint8_t imu_snapshot[IMU_RAW_LENGTH];
static uint16_t tof_snapshot[VL53L8CX_ZONE_COUNT];
static uint8_t imu_valid, tof_valid;
static uint32_t snapshot_tick;
typedef enum { VM_IDLE, VM_QUEUED, VM_LOADING, VM_RUNNING,
               VM_DONE, VM_STOPPED, VM_TRAP, VM_ERROR } vm_state_t;
static const char *const states[] = {
    "IDLE", "QUEUED", "LOADING", "RUNNING", "DONE", "STOPPED", "TRAP", "ERROR"
};
static const char *const programs[] = {
    "MATH", "SENSORS", "LOOP", "OOB", "HOLD", "BAD_CHANNEL", "BAD_PULSE", "FLOAT"
};
static volatile struct {
    vm_state_t state;
    uint32_t mode, stop, stop_tick, stop_ms, file_id;
    uint32_t steps, calls, peak, cleanup, max_step_ms, imu_frame, tof_frame;
    int32_t result;
} vm;
static char last_error[48];

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void capture_sensors(void)
{
    /* Lower priority than both producers. All imports see this step's snapshot;
     * no I2C, waiting, or native pointer from the guest is involved. */
    taskENTER_CRITICAL();
    snapshot_tick = HAL_GetTick();
    memcpy(imu_snapshot, (const void *)g_imu.packet, sizeof(imu_snapshot));
    memcpy(tof_snapshot, (const void *)g_vl53l8cx_runtime.distance_mm, sizeof(tof_snapshot));
    imu_valid = g_imu.running && (imu_snapshot[0] == 1U || imu_snapshot[0] == 2U) && read_le32(imu_snapshot + 4) &&
                snapshot_tick - read_le32(imu_snapshot + 8) < 500U;
    tof_valid = g_vl53l8cx_runtime.ranging_active && g_vl53l8cx_runtime.frames &&
                snapshot_tick - g_vl53l8cx_runtime.last_frame_tick_ms < 500U;
    vm.imu_frame = imu_valid ? read_le32(imu_snapshot + 4) : 0U;
    vm.tof_frame = tof_valid ? g_vl53l8cx_runtime.frames : 0U;
    taskEXIT_CRITICAL();
}
static int32_t native_imu_word(wasm_exec_env_t env, uint32_t index)
{
    (void)env; ++vm.calls;
    return !vm.stop && imu_valid && index < IMU_RAW_LENGTH / 4U ?
           (int32_t)read_le32(imu_snapshot + index * 4U) : 0;
}
static int32_t native_tof_mm(wasm_exec_env_t env, uint32_t index)
{
    (void)env; ++vm.calls;
    return !vm.stop && tof_valid && index < VL53L8CX_ZONE_COUNT &&
           tof_snapshot[index] != VL53L8CX_DISTANCE_INVALID_MM ? tof_snapshot[index] : -1;
}
static int32_t native_millis(wasm_exec_env_t env)
{ (void)env; ++vm.calls; return (int32_t)snapshot_tick; }
static int32_t native_servo_get(wasm_exec_env_t env, uint32_t channel)
{
    (void)env; ++vm.calls;
    if (channel >= SERVO_CHANNEL_COUNT || vm.stop) return -1;
    return servos_controller.servo[channel].current_value;
}
static int32_t native_servo_set(wasm_exec_env_t env, uint32_t channel, uint32_t us)
{
    (void)env; ++vm.calls;
    if (channel >= SERVO_CHANNEL_COUNT || us < 500U || us > 2500U) return -1;
    /* Atomic with STOP/disconnect. servoTask retains I2C ownership, and the
     * servo API retains its connected/demo/timeline checks. No waits here. */
    taskENTER_CRITICAL();
    int32_t result;
    if (vm.stop) result = -2;
    /* HOLD must not restore a stale target if a BLE command preempts its GET. */
    else if (vm.mode == 4U && us != servos_controller.servo[channel].current_value) result = -3;
    else result = (vm.file_id ? servo_SetLocalValue((uint8_t)channel,(uint16_t)us) :
        servo_SetServoImmediateValue((uint8_t)channel, (uint16_t)us)) == HAL_OK ? 0 : -3;
    taskEXIT_CRITICAL();
    return result;
}
static NativeSymbol imports[] = {
    {"imu_word", native_imu_word, "(i)i", NULL},
    {"tof_mm", native_tof_mm, "(i)i", NULL},
    {"millis", native_millis, "()i", NULL},
    {"servo_get_us", native_servo_get, "(i)i", NULL},
    {"servo_set_us", native_servo_set, "(ii)i", NULL}
};
void RobotVM_Init(uint8_t *native_stack_bottom)
{
    robot_native_stack_bottom = native_stack_bottom;
    servo_RegisterStopCallback(RobotVM_RequestStop);
}
void RobotVM_RequestStop(void)
{
    taskENTER_CRITICAL();
    if (vm.state >= VM_QUEUED && vm.state <= VM_RUNNING && !vm.stop) {
        vm.stop_tick = HAL_GetTick();
        vm.stop = 1U;
    }
    taskEXIT_CRITICAL();
}
static const char *submit(uint32_t mode,uint32_t file_id)
{
    taskENTER_CRITICAL();
    if (vm.state >= VM_QUEUED && vm.state <= VM_RUNNING) {
        taskEXIT_CRITICAL();
        return "BUSY";
    }
    memset((void *)&vm, 0, sizeof(vm));
    last_error[0] = 0;
    vm.mode = mode;
    vm.file_id = file_id;
    vm.state = VM_QUEUED;
    taskEXIT_CRITICAL();
    osThreadFlagsSet(wasmTaskHandle, 1U);
    return NULL;
}
const char *RobotVM_Start(const char *name) {
    for(uint32_t mode=0;mode<sizeof(programs)/sizeof(programs[0]);mode++)
        if(!strcmp(name,programs[mode]))return submit(mode,0);
    return "BAD_PROGRAM";
}
const char *RobotVM_StartFile(uint32_t id) {return id?submit(8,id):"BAD_ID";}
void RobotVM_Status(char *out, size_t capacity)
{
    /* Diagnostic counters; the next completed step may update individual fields. */
    snprintf(out, capacity,
        "OK WASM_STATUS %s\nid=%lu n=%lu r=%ld calls=%lu\npool=%lu clean=%lu stack=%lu\n"
        "max_ms=%lu stop_ms=%lu\nimu=%lu tof=%lu\n%s\nEND\n",
        states[vm.state], (unsigned long)vm.file_id, (unsigned long)vm.steps, (long)vm.result,
        (unsigned long)vm.calls, (unsigned long)vm.peak, (unsigned long)vm.cleanup,
        (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)wasmTaskHandle) * sizeof(StackType_t),
        (unsigned long)vm.max_step_ms, (unsigned long)vm.stop_ms,
        (unsigned long)vm.imu_frame, (unsigned long)vm.tof_frame, last_error);
}
static vm_state_t run_program(void)
{
    RuntimeInitArgs init = {0};
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    wasm_exec_env_t exec = NULL;
    uint8_t *bytes = NULL;
    const uint8_t *source=robot_guest;
    uint32_t length=sizeof(robot_guest);
    bool leased=false, motor_owner=false;
    mem_alloc_info_t mem = {0};
    vm_state_t outcome = VM_ERROR;
    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_buf = vm_pool;
    init.mem_alloc_option.pool.heap_size = sizeof(vm_pool);
    init.native_module_name = "robot";
    init.native_symbols = imports;
    init.n_native_symbols = sizeof(imports) / sizeof(imports[0]);
    if (!wasm_runtime_full_init(&init)) {
        strcpy(last_error, "runtime init failed");
        return VM_ERROR;
    }
    /* Classic loader rewrites bytecode: never pass Flash as writable input. */
    if(vm.file_id) {
        if(!ImageStore_AcquireProgram(vm.file_id,&source,&length)) {strcpy(last_error,"FILE_UNAVAILABLE");goto done;}
        leased=true;
        if(!RobotWasm_Validate(source,length)) {strcpy(last_error,"UNSUPPORTED_WASM_PROFILE");goto done;}
    }
    bytes = wasm_runtime_malloc(length);
    if (!bytes) { strcpy(last_error, "module buffer allocation failed"); goto done; }
    memcpy(bytes, source, length);
    if(leased) {ImageStore_ReleaseProgram();leased=false;}
    module = wasm_runtime_load(bytes, length, last_error, sizeof(last_error));
    if (!module || vm.stop) goto done;
    instance = wasm_runtime_instantiate(module, VM_WASM_STACK_BYTES, 0, last_error, sizeof(last_error));
    if (!instance || vm.stop) goto done;
    exec = wasm_runtime_create_exec_env(instance, VM_WASM_STACK_BYTES);
    if (!exec) { strcpy(last_error, "execution stack allocation failed"); goto done; }
    wasm_function_inst_t step = wasm_runtime_lookup_function(instance, "step");
    if (!step) { strcpy(last_error, "missing step"); goto done; }
    if(vm.file_id) {
        taskENTER_CRITICAL();
        if(!vm.stop)motor_owner=servo_BeginLocalControl()==HAL_OK;
        if(motor_owner) {IMU_SetLocalDemand(1);vl53l8cx_SetLocalDemand(1);}
        taskEXIT_CRITICAL();
        if(!motor_owner) {strcpy(last_error,"MOTION_BUSY_OR_DISCONNECTED");goto done;}
    }
    vm.state = VM_RUNNING;
    do {
        if (vm.stop) break;
        capture_sensors();
        uint32_t argv[1] = {vm.file_id ? snapshot_tick : vm.mode};
        uint32_t started = HAL_GetTick();
        wasm_runtime_set_instruction_count_limit(exec, VM_INSTRUCTIONS_PER_STEP);
        bool ok = wasm_runtime_call_wasm(exec, step, 1, argv);
        uint32_t duration = HAL_GetTick() - started;
        if (duration > vm.max_step_ms) vm.max_step_ms = duration;
        ++vm.steps;
        if (!ok) {
            const char *error = wasm_runtime_get_exception(instance);
            snprintf(last_error, sizeof(last_error), "%s", error ? error : "execution failed");
            outcome = VM_TRAP;
            break;
        }
        vm.result = (int32_t)argv[0];
        outcome = VM_DONE;
        if (wasm_runtime_get_mem_alloc_info(&mem)) vm.peak = mem.highmark_size;
        if (vm.mode == 7U && vm.result != 496768) {
            strcpy(last_error, "floating point self-check failed");
            outcome = VM_ERROR;
            break;
        }
        if (!vm.file_id && vm.mode != 1U && vm.mode != 7U) break;
        osDelay(VM_PERIOD_MS);
    } while (!vm.stop);
done:
    if(leased)ImageStore_ReleaseProgram();
    if(motor_owner) {IMU_SetLocalDemand(0);vl53l8cx_SetLocalDemand(0);servo_EndLocalControl();}
    if (wasm_runtime_get_mem_alloc_info(&mem)) vm.peak = mem.highmark_size;
    if (exec) wasm_runtime_destroy_exec_env(exec);
    if (instance) wasm_runtime_deinstantiate(instance);
    if (module) wasm_runtime_unload(module);
    if (bytes) wasm_runtime_free(bytes);
    if (wasm_runtime_get_mem_alloc_info(&mem)) vm.cleanup = mem.total_size - mem.total_free_size;
    wasm_runtime_destroy();
    return outcome;
}
void RobotVM_Task(void *argument)
{
    (void)argument;
    for (;;) {
        osThreadFlagsWait(1U, osFlagsWaitAny, osWaitForever);
        vm.state = VM_LOADING;
        vm_state_t outcome = vm.stop ? VM_STOPPED : run_program();
        taskENTER_CRITICAL();
        if (vm.stop) {
            vm.stop_ms = HAL_GetTick() - vm.stop_tick;
            vm.state = VM_STOPPED;
        } else vm.state = outcome;
        taskEXIT_CRITICAL();
    }
}
