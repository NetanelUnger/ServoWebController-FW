/* Host fault-injection test of the actual I2C wrapper, with simulated HAL/RTOS.
 * Build as C with a host compiler; no board or motors are required. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define APPLICATION_USER_CORE_I2C_OS_I2C_OS_H_
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
enum { HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY };
typedef struct { uint32_t CR1; } FakePeripheral;
typedef struct {
    FakePeripheral *Instance;
    void *XferISR;
    uint8_t *pBuffPtr;
    uint16_t XferCount;
    unsigned state;
} I2C_HandleTypeDef;
#define I2C_CR1_ANFOFF 0x1000U
#define I2C_CR1_DNF 0xF00U
#define I2C_CR1_DNF_Pos 8
#define I2C_IT_TXI 1
#define I2C_IT_RXI 2
#define I2C_IT_ADDRI 4
#define I2C_IT_NACKI 8
#define I2C_IT_STOPI 16
#define I2C_IT_TCI 32
#define I2C_IT_ERRI 64
#define I2C_MEMADD_SIZE_16BIT 2
#define osWaitForever UINT32_MAX
#define osOK 0
static int i2cMutexHandle;
static unsigned locked, pending, aborting, resets, tick, scenario, delays, acquired;
static I2C_HandleTypeDef *current;
enum { COMPLETE, FOREIGN_CALLBACK, NACK, TIMEOUT_ABORT, TIMEOUT_RESET, START_FAIL };
static unsigned __get_PRIMASK(void) { return 0; }
static void __disable_irq(void) {}
static void __set_PRIMASK(unsigned mask) { (void)mask; }
#define __HAL_I2C_DISABLE_IT(h, bits) ((h)->Instance->CR1 &= ~(bits))
#define __HAL_I2C_DISABLE(h) do { assert(locked); pending=0; } while (0)
static void Error_Handler(void) { assert(!"Unexpected peripheral recovery failure"); }
static uint32_t osKernelGetTickFreq(void) { return 128; } /* Deliberately not 1 kHz. */
static uint32_t osKernelGetTickCount(void) { return tick; }
static int osMutexAcquire(int mutex, uint32_t timeout) {
    (void)mutex; (void)timeout; assert(!locked); locked=1; ++acquired; return osOK;
}
static int osMutexRelease(int mutex);
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h);
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h);
static void osDelay(unsigned ticks) {
    assert(locked); tick+=ticks; ++delays;
    if (scenario==FOREIGN_CALLBACK && delays==1) {
        I2C_HandleTypeDef other={0}; HAL_I2C_MemTxCpltCallback(&other); return;
    }
    if (scenario==TIMEOUT_RESET || (scenario==TIMEOUT_ABORT && !aborting)) return;
    pending=0; current->state=HAL_I2C_STATE_READY;
    if (aborting) HAL_I2C_AbortCpltCallback(current);
    else if (scenario==NACK) HAL_I2C_ErrorCallback(current);
    else HAL_I2C_MemTxCpltCallback(current);
}
static HAL_StatusTypeDef start_transfer(I2C_HandleTypeDef *h, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *data, uint16_t size) {
    (void)address; (void)reg; (void)width;
    assert(locked && !pending); assert(size==64 && data);
    if (scenario==START_FAIL) return HAL_ERROR;
    current=h; pending=1; h->state=HAL_I2C_STATE_BUSY; return HAL_OK;
}
#define HAL_I2C_Mem_Write_IT start_transfer
#define HAL_I2C_Mem_Read_IT start_transfer
static unsigned HAL_I2C_GetState(I2C_HandleTypeDef *h) { return h->state; }
static HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *h, uint16_t addr) {
    (void)h; (void)addr; assert(locked && pending); aborting=1; return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h) {
    assert(locked && !pending && !h->pBuffPtr && !h->XferISR); ++resets; return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) {
    assert(locked); h->state=HAL_I2C_STATE_READY; h->Instance->CR1=0; return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h, uint32_t filter) {
    assert(locked); h->Instance->CR1 |= filter; return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h, uint32_t filter) {
    assert(locked); h->Instance->CR1 |= filter << I2C_CR1_DNF_Pos; return HAL_OK;
}
static HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr,
    uint32_t trials, uint32_t timeout) {
    (void)h; (void)addr; (void)trials; (void)timeout;
    assert(locked && !pending); return HAL_OK;
}
#include "../STM32CubeIDE/Application/User/Core/i2c_os/i2c_os.c"

static int osMutexRelease(int mutex) {
    (void)mutex;
    /* This assertion fails if any branch gives the bus/buffer away too early. */
    assert(locked && !pending && !_i2c_busy && !_i2c_active);
    locked=0; return osOK;
}
int main(void) {
    uint8_t frame[64]={0};
    FakePeripheral peripheral={I2C_CR1_ANFOFF | (3U << I2C_CR1_DNF_Pos)};
    I2C_HandleTypeDef h={&peripheral, NULL, NULL, 0, HAL_I2C_STATE_READY};
    for (unsigned read=0; read<2; ++read) {
        for (scenario=COMPLETE; scenario<=START_FAIL; ++scenario) {
            aborting=resets=delays=acquired=0;
            /* Exercise deadline subtraction over tick rollover too. */
            tick=UINT32_MAX-5;
            HAL_StatusTypeDef result = read ? i2c_readOS(&h, 0x80, 6, 1, frame, 64)
                                           : i2c_writeOS(&h, 0x80, 6, 1, frame, 64);
            assert(acquired==1 && !locked && !pending);
            assert(result == (scenario>=TIMEOUT_ABORT && scenario<=TIMEOUT_RESET ? HAL_TIMEOUT :
                              scenario==NACK || scenario==START_FAIL ? HAL_ERROR : HAL_OK));
            if (scenario==FOREIGN_CALLBACK) assert(delays==2);
            if (scenario==TIMEOUT_ABORT) assert(aborting && !resets && delays==14);
            if (scenario==TIMEOUT_RESET) {
                assert(aborting && resets==1 && delays==16);
                assert(peripheral.CR1==(I2C_CR1_ANFOFF | (3U << I2C_CR1_DNF_Pos)));
            }
        }
    }
    assert(i2c_isDeviceReadyOS(&h, 0x80, 1, 10)==HAL_OK && !locked);
    puts("PASS full-frame mutex ownership, foreign callbacks, NACK, abort/reset before unlock, filter restore, non-1kHz ticks and rollover");
    return 0;
}
