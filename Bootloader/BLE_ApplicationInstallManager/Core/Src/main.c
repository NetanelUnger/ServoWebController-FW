/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "../../../../Core/Inc/ota_image.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void (*fct_t)(void);

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Magic Keyword address offset, defined by the TAG_OTA_START address offset defined in the generated .map file of BLE Application with OTA  */
#define KEYWORD_ADDRESS_OFFSET              (0x160)

/* Define the NVM size in sector */
#define CFG_NVM_NB_SECTORS                  (2)
/* Define the User Data size in sector */
#define CFG_USER_DATA_NB_SECTORS            (1)
/* Define the start address where the application shall be located */
#define CFG_ACTIVE_SLOT_START_SECTOR_INDEX  (3)

/* Define mapping of OTA messages in SRAM */
#define CFG_OTA_REBOOT_VAL_MSG              (*(uint8_t*)(SRAM1_BASE+0))
#define CFG_OTA_START_SECTOR_IDX_VAL_MSG    (*(uint8_t*)(SRAM1_BASE+1))

/* Size in Page of Download and Active slots */
#define DOWNLOAD_ACTIVE_NB_SECTORS          ((FLASH_SIZE / FLASH_PAGE_SIZE) - CFG_NVM_NB_SECTORS - CFG_USER_DATA_NB_SECTORS)
/* Start address where the User Data shall be located */
#define USER_CFG_SLOT_START_SECTOR_INDEX    ((FLASH_SIZE / FLASH_PAGE_SIZE) - CFG_NVM_NB_SECTORS - CFG_USER_DATA_NB_SECTORS)
/* Size in Page of Download or active slot */
#define APP_SLOT_PAGE_SIZE                  ((DOWNLOAD_ACTIVE_NB_SECTORS - CFG_ACTIVE_SLOT_START_SECTOR_INDEX) / 2)
/* Define the start address where the NEW application shall be downloaded */
#define DOWNLOAD_SLOT_START_SECTOR_INDEX    (APP_SLOT_PAGE_SIZE + CFG_ACTIVE_SLOT_START_SECTOR_INDEX)

/* Define list of reboot reason */
#define REBOOT_ON_FW_APP                    (0x00)
#define REBOOT_ON_APP_INSTALL_MNGR          (0x01)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void BootModeCheck( void );
static uint8_t CheckFwAppValidity( uint8_t page_idx );
void JumpFwApp( void );
static uint8_t DeleteSlot( uint8_t page_idx );
static uint8_t MoveToActiveSlot( uint8_t page_idx );


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Enable PWR clock interface */

  LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_PWR);

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  BootModeCheck();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_1);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_1)
  {
  }

  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE2);
  LL_RCC_HSI_Enable();

   /* Wait till HSI is ready */
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_HSI_SetCalibTrimming(16);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
  }

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAHB5Divider(LL_RCC_AHB5_DIVIDER_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
  LL_RCC_SetAPB7Prescaler(LL_RCC_APB7_DIV_1);
  LL_SetSystemCoreClock(16000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* Overwrite HAL Tick functions to not use sysTick start sysTick */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  return HAL_OK;
}

/**
 * Check the Boot mode request
 * Depending on the result, the CPU may either jump to an existing application in the user flash
 * or keep on running the code to start the OTA loader
 */
static void BootModeCheck( void )
{
  /* A persistent commit survives ALL reset causes, including lost power during
   * erase/copy. SRAM bytes alone must never authorize an installation. */
  if(CheckFwAppValidity(DOWNLOAD_SLOT_START_SECTOR_INDEX)) {
    const uint32_t *commit=(const uint32_t *)OTA_COMMIT_ADDRESS;
    uint32_t length=commit[1],crc=commit[2];
    if(!OTA_ImageValid((const uint8_t *)OTA_ACTIVE_ADDRESS,length) ||
       OTA_Crc32((const uint8_t *)OTA_ACTIVE_ADDRESS,length)!=crc) {
      if(!DeleteSlot(CFG_ACTIVE_SLOT_START_SECTOR_INDEX) ||
         !MoveToActiveSlot(DOWNLOAD_SLOT_START_SECTOR_INDEX)) return;
    }
    if(!OTA_ImageValid((const uint8_t *)OTA_ACTIVE_ADDRESS,length) ||
       OTA_Crc32((const uint8_t *)OTA_ACTIVE_ADDRESS,length)!=crc) return;
    /* Active verified. Erase the commit page first: a power cut during cleanup
     * must never turn a half-erased candidate into an installation request. */
    (void)DeleteSlot(DOWNLOAD_SLOT_START_SECTOR_INDEX);
  }
  if(CheckFwAppValidity(CFG_ACTIVE_SLOT_START_SECTOR_INDEX)) JumpFwApp();
  /* No valid image: preserve evidence and wait for ST-LINK recovery. */
}

static uint8_t CheckFwAppValidity( uint8_t page_idx )
{
  if(page_idx==DOWNLOAD_SLOT_START_SECTOR_INDEX)
    return OTA_CommitValid((const uint8_t *)OTA_DOWNLOAD_ADDRESS,(const uint8_t *)OTA_COMMIT_ADDRESS);
  if(page_idx!=CFG_ACTIVE_SLOT_START_SECTOR_INDEX) return 0;
  uint32_t tag=OTA_Get32((const uint8_t *)(OTA_ACTIVE_ADDRESS+OTA_TAG_OFFSET));
  if(tag<OTA_ACTIVE_ADDRESS+OTA_TAG_OFFSET+4U || tag>OTA_ACTIVE_ADDRESS+OTA_MAX_IMAGE_SIZE-4U) return 0;
  uint32_t size=(tag-OTA_ACTIVE_ADDRESS+4U+15U)&~15U;
  return OTA_ImageValid((const uint8_t *)OTA_ACTIVE_ADDRESS,size);
}

void JumpFwApp( void )
{
  uint32_t stack=*(const uint32_t *)OTA_ACTIVE_ADDRESS;
  uint32_t reset=*(const uint32_t *)(OTA_ACTIVE_ADDRESS+4U);
  SysTick->CTRL=0;SysTick->LOAD=0;SysTick->VAL=0;
  SCB->VTOR=OTA_ACTIVE_ADDRESS;
  __DSB();__ISB();
  /* No C stack access after MSP changes. The application reset handler does not return. */
  __asm volatile("msr msp, %0\n bx %1" : : "r"(stack), "r"(reset) : "memory");
  __builtin_unreachable();
}

static uint8_t DeleteSlot( uint8_t page_idx )
{
  if(page_idx!=CFG_ACTIVE_SLOT_START_SECTOR_INDEX && page_idx!=DOWNLOAD_SLOT_START_SECTOR_INDEX) return 0;
  /* Reverse page order invalidates the tail/commit before any vector changes. */
  for(uint32_t i=APP_SLOT_PAGE_SIZE;i>0;i--) {
    uint32_t page=page_idx+i-1U,error;
    FLASH_EraseInitTypeDef erase={0};
    erase.TypeErase=FLASH_TYPEERASE_PAGES;erase.NbPages=1;
    erase.Page=page & (FLASH_PAGE_NB-1U);
#if defined(FLASH_DBANK_SUPPORT)
    erase.Banks=((FLASH_PAGE_NB & page) ^ (1U & READ_BIT(FLASH->OPTR,FLASH_OPTR_SWAP_BANK_Msk)))?FLASH_BANK_2:FLASH_BANK_1;
#endif
    if(HAL_FLASH_Unlock()!=HAL_OK) return 0;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_StatusTypeDef status=HAL_FLASHEx_Erase(&erase,&error);
    HAL_FLASH_Lock();
    if(status!=HAL_OK) return 0;
  }
  return 1;
}

static uint8_t MoveToActiveSlot( uint8_t page_idx )
{
  if(page_idx!=DOWNLOAD_SLOT_START_SECTOR_INDEX || !CheckFwAppValidity(page_idx)) return 0;
  uint32_t length=*(const uint32_t *)(OTA_COMMIT_ADDRESS+4U);
  for(uint32_t offset=0;offset<length;offset+=16U) {
    if(HAL_FLASH_Unlock()!=HAL_OK) return 0;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    HAL_StatusTypeDef status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
        OTA_ACTIVE_ADDRESS+offset,OTA_DOWNLOAD_ADDRESS+offset);
    HAL_FLASH_Lock();
    if(status!=HAL_OK) return 0;
    for(unsigned w=0;w<4;w++)
      if(*(const uint32_t *)(OTA_ACTIVE_ADDRESS+offset+4U*w)!=
         *(const uint32_t *)(OTA_DOWNLOAD_ADDRESS+offset+4U*w)) return 0;
  }
  return 1;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
