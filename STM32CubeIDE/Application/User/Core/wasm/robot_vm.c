#include "robot_vm.h"
#include "wasm_profile.h"
#include "robot_imports.h"
#include "robot_sensor_math.h"
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
static uint8_t imu_motion_snapshot[IMU_MOTION_LENGTH];
static uint16_t tof_snapshot[VL53L8CX_ZONE_COUNT];
static uint8_t imu_valid, tof_valid;
static uint32_t snapshot_tick;
static uint8_t api_version;
static volatile uint8_t sensor_requests;
static uint32_t tof_frame,tof_age;
static uint16_t filter_min,filter_max;
static RobotTofStats tof_stats;
static bool tof_stats_dirty;
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

/* Readiness of the cached hardware sample is separate from this program's
 * demand. Joining an active sensor can expose that sample immediately. */
static bool imu_available(void) {return !vm.stop && imu_valid && (api_version<2 || (sensor_requests&2U));}
static bool tof_available(void) {return !vm.stop && tof_valid && (api_version<2 || (sensor_requests&1U));}

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
    memcpy(imu_motion_snapshot, (const void *)g_imu.motion, sizeof(imu_motion_snapshot));
    memcpy(tof_snapshot, (const void *)g_vl53l8cx_runtime.distance_mm, sizeof(tof_snapshot));
    imu_valid = g_imu.running && g_imu.motion_valid && (imu_snapshot[0] == 1U || imu_snapshot[0] == 2U) && read_le32(imu_snapshot + 4) &&
                snapshot_tick - read_le32(imu_snapshot + 8) < 500U;
    tof_valid = g_vl53l8cx_runtime.ranging_active && g_vl53l8cx_runtime.frames &&
                snapshot_tick - g_vl53l8cx_runtime.last_frame_tick_ms < 500U;
    tof_frame=g_vl53l8cx_runtime.frames;
    tof_age=snapshot_tick-g_vl53l8cx_runtime.last_frame_tick_ms;
    tof_stats_dirty=true;
    vm.imu_frame = imu_available() ? read_le32(imu_snapshot + 4) : 0U;
    vm.tof_frame = tof_available() ? tof_frame : 0U;
    taskEXIT_CRITICAL();
}
static int32_t native_imu_word(wasm_exec_env_t env, uint32_t index)
{
    (void)env; ++vm.calls;
    return imu_available() && index < IMU_RAW_LENGTH / 4U ?
           (int32_t)read_le32(imu_snapshot + index * 4U) : 0;
}
static int32_t native_tof_mm(wasm_exec_env_t env, uint32_t index)
{
    (void)env; ++vm.calls;
    return RobotTof_Value(tof_snapshot,tof_available(),index,filter_min,filter_max);
}
static int32_t request_sensor(uint32_t enabled,uint8_t bit) {
    if(enabled>1 || !vm.file_id || api_version<2)return -1;
    taskENTER_CRITICAL();
    if(vm.stop) {taskEXIT_CRITICAL();return -2;}
    if(!!(sensor_requests&bit)!=enabled) {
        if(enabled)sensor_requests|=bit;else sensor_requests&=(uint8_t)~bit;
        /* Reuse this call's snapshot; repeated enables never restart sensing
         * or force the program to wait for another frame. Cold/stale data stays
         * unavailable until a later step captures a real, fresh sample. */
        if(bit==1) {tof_stats_dirty=true;vm.tof_frame=tof_available()?tof_frame:0U;}
        else vm.imu_frame=imu_available()?read_le32(imu_snapshot+4):0U;
    }
    if(bit==1)vl53l8cx_SetLocalDemand((uint8_t)enabled);else IMU_SetLocalDemand((uint8_t)enabled);
    taskEXIT_CRITICAL();return 0;
}
static int32_t native_tof_enable(wasm_exec_env_t env,uint32_t enabled) {(void)env;++vm.calls;return request_sensor(enabled,1);}
static int32_t native_imu_enable(wasm_exec_env_t env,uint32_t enabled) {(void)env;++vm.calls;return request_sensor(enabled,2);}
static int32_t native_tof_set_range(wasm_exec_env_t env,uint32_t low,uint32_t high) {
    (void)env;++vm.calls;
    if(low>high || high>65534U)return -1;
    if(vm.stop)return -2;
    filter_min=(uint16_t)low;filter_max=(uint16_t)high;tof_stats_dirty=true;return 0;
}
static int32_t native_tof_at(wasm_exec_env_t env,uint32_t x,uint32_t y) {
    if(x>=8 || y>=8) {(void)env;++vm.calls;return -1;}
    return native_tof_mm(env,y*8+x);
}
static int32_t native_tof_read(wasm_exec_env_t env,uint32_t offset) {
    ++vm.calls;
    wasm_module_inst_t instance=wasm_runtime_get_module_inst(env);
    /* Validate the complete destination before converting the guest offset.
     * The module must provide 64 i32 cells (256 bytes), including on no-data. */
    if(!wasm_runtime_validate_app_addr(instance,offset,64U*sizeof(int32_t)))return -1;
    uint8_t *out=wasm_runtime_addr_app_to_native(instance,offset);
    int32_t count=0;
    for(uint32_t i=0;i<64;i++) {
        int32_t value=RobotTof_Value(tof_snapshot,tof_available(),i,filter_min,filter_max);
        memcpy(out+i*sizeof(value),&value,sizeof(value));
        if(value>=0)++count;
    }
    return count;
}
static int32_t native_tof_ready(wasm_exec_env_t env) {(void)env;++vm.calls;return tof_available();}
static int32_t native_tof_frame_id(wasm_exec_env_t env) {(void)env;++vm.calls;return tof_available()?(int32_t)vm.tof_frame:0;}
static int32_t native_tof_age_ms(wasm_exec_env_t env) {(void)env;++vm.calls;return tof_available()?(int32_t)tof_age:-1;}
static RobotTofStats *stats(void) {
    if(tof_stats_dirty || vm.stop) {
        tof_stats=RobotTof_Reduce(tof_snapshot,tof_available(),filter_min,filter_max);
        tof_stats_dirty=false;
    }
    return &tof_stats;
}
#define TOF_STAT(name,field) static int32_t native_tof_##name(wasm_exec_env_t env) {(void)env;++vm.calls;return stats()->field;}
TOF_STAT(min_mm,minimum)
TOF_STAT(max_mm,maximum)
TOF_STAT(mean_mm,mean)
TOF_STAT(min_zone,min_zone)
TOF_STAT(max_zone,max_zone)
TOF_STAT(count,count)
#undef TOF_STAT
static int32_t native_imu_ready(wasm_exec_env_t env) {
    (void)env;++vm.calls;return imu_available();
}
static int32_t native_imu_frame_id(wasm_exec_env_t env) {(void)env;++vm.calls;return imu_available()?(int32_t)vm.imu_frame:0;}
static int32_t native_imu_age_ms(wasm_exec_env_t env) {(void)env;++vm.calls;return imu_available()?(int32_t)(snapshot_tick-read_le32(imu_snapshot+8)):-1;}
static int32_t native_imu_accel_mg(wasm_exec_env_t env,uint32_t axis) {(void)env;++vm.calls;return RobotImu_Axis(imu_motion_snapshot,imu_available(),axis,false);}
static int32_t native_imu_gyro_mdps(wasm_exec_env_t env,uint32_t axis) {(void)env;++vm.calls;return RobotImu_Axis(imu_motion_snapshot,imu_available(),axis,true);}
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
    #define SIGNATURE_0 "()i"
    #define SIGNATURE_1 "(i)i"
    #define SIGNATURE_2 "(ii)i"
    #define IMPORT(symbol,fn,argc,version) {#symbol,fn,SIGNATURE_##argc,NULL},
    ROBOT_IMPORTS(IMPORT)
    #undef IMPORT
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
        if(vm.file_id) {sensor_requests=0;IMU_SetLocalDemand(0);vl53l8cx_SetLocalDemand(0);}
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
    api_version=1;sensor_requests=0;filter_min=0;filter_max=65534;tof_stats_dirty=true;
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
        if(!RobotWasm_Inspect(source,length,&api_version)) {strcpy(last_error,"UNSUPPORTED_WASM_PROFILE");goto done;}
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
        /* Preserve the original lifecycle for already stored ABI-1 files. */
        if(motor_owner && api_version==1) {IMU_SetLocalDemand(1);vl53l8cx_SetLocalDemand(1);}
        taskEXIT_CRITICAL();
        if(!motor_owner) {strcpy(last_error,"MOTION_BUSY_OR_DISCONNECTED");goto done;}
    }
    if(vm.file_id && api_version>=2) {
        wasm_function_inst_t initialize=wasm_runtime_lookup_function(instance,"init");
        if(initialize && !vm.stop) {
            capture_sensors();uint32_t result[1]={0};
            wasm_runtime_set_instruction_count_limit(exec,VM_INSTRUCTIONS_PER_STEP);
            if(!wasm_runtime_call_wasm(exec,initialize,0,result)) {
                const char *error=wasm_runtime_get_exception(instance);
                snprintf(last_error,sizeof(last_error),"%s",error?error:"init failed");outcome=VM_TRAP;goto done;
            }
            if((int32_t)result[0]!=0) {snprintf(last_error,sizeof(last_error),"INIT_FAILED %ld",(long)(int32_t)result[0]);goto done;}
        }
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
    if(motor_owner) {sensor_requests=0;IMU_SetLocalDemand(0);vl53l8cx_SetLocalDemand(0);servo_EndLocalControl();}
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
