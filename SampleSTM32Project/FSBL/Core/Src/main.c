/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "stm32n6xx_nucleo_xspi.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUTTX_FLASH_ADDR    0x70020000   /* NuttX location in external flash */
#define NUTTX_RAM_ADDR      0x34000400   /* AXISRAM2 destination (where NuttX is linked) */
#define NUTTX_SIZE          0x160000     /* 1.375MB max — must stay below FSBL at 0x34180400 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void load_and_jump_to_nuttx(void);
static int init_xspi_memory_mapped(void);
static void serial_init(void);
static void serial_puts(const char *s);
static void serial_puthex(uint32_t val);
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
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_RED);
  BSP_LED_Init(LED_GREEN);

  /* Turn on blue LED to indicate FSBL is running */
  BSP_LED_On(LED_BLUE);

  /* Initialize serial for debug output */
  serial_init();
  serial_puts("\r\n\r\n=== FSBL Starting ===\r\n");

  /* Initialize XSPI2 in basic SPI mode for memory-mapped access */
  serial_puts("Initializing XSPI2...\r\n");
  int xspi_result = init_xspi_memory_mapped();
  if (xspi_result != 0)
  {
    serial_puts("XSPI init failed: ");
    serial_puthex(xspi_result);
    serial_puts("\r\n");
    /* XSPI init failed - blink red LED to show error code */
    BSP_LED_Off(LED_BLUE);
    for (int i = 0; i < (-xspi_result); i++) {
      BSP_LED_On(LED_RED);
      HAL_Delay(200);
      BSP_LED_Off(LED_RED);
      HAL_Delay(200);
    }
    BSP_LED_On(LED_RED);
    while(1);
  }
  serial_puts("XSPI init OK\r\n");

  /* Load NuttX from external flash and jump to it */
  load_and_jump_to_nuttx();

  /* Should never reach here - turn on red LED if we do */
  BSP_LED_On(LED_RED);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
/* USER CODE BEGIN CLK 1 */
/* USER CODE END CLK 1 */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the System Power Supply
  */
  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }

  /* SMPS overdrive: set PB12 HIGH before switching to VOS SCALE0.
   * Required for 800 MHz CPU operation.
   */

  __HAL_RCC_GPIOB_CLK_ENABLE();
  {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
  }

  /** Configure the main internal regulator output voltage — SCALE0 for 800 MHz
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable HSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Get current CPU/System buses clocks configuration and if necessary switch
 to intermediate HSI clock to ensure target clock can be set
  */
  HAL_RCC_GetClockConfig(&RCC_ClkInitStruct);
  if ((RCC_ClkInitStruct.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
     (RCC_ClkInitStruct.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
    RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
    {
      /* Initialization Error */
      Error_Handler();
    }
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL1.PLLM = 2;
  RCC_OscInitStruct.PLL1.PLLN = 25;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_CPUCLK|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_PCLK5
                              |RCC_CLOCKTYPE_PCLK4;
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 1;   /* 800/1 = 800 MHz */
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 2;   /* 800/2 = 400 MHz */
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 3;   /* 800/3 = 267 MHz */
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 2;  /* 800/2 = 400 MHz */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief  Initialize serial port for debug output
  */
static void serial_init(void)
{
  COM_InitTypeDef com_init;
  com_init.BaudRate = 115200;
  com_init.WordLength = COM_WORDLENGTH_8B;
  com_init.StopBits = COM_STOPBITS_1;
  com_init.Parity = COM_PARITY_NONE;
  com_init.HwFlowCtl = COM_HWCONTROL_NONE;
  BSP_COM_Init(COM1, &com_init);
}

/**
  * @brief  Send string to serial port
  */
static void serial_puts(const char *s)
{
  HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)s, strlen(s), 100);
}

/**
  * @brief  Send hex value to serial port
  */
static void serial_puthex(uint32_t val)
{
  char hex[11];
  sprintf(hex, "0x%08lX", (unsigned long)val);
  serial_puts(hex);
}

/**
  * @brief  Initialize XSPI2 in basic SPI mode with memory-mapped access
  * @retval 0 on success, -1 on failure
  */
static int init_xspi_memory_mapped(void)
{
  XSPI_HandleTypeDef hxspi;
  XSPI_RegularCmdTypeDef sCommand;
  XSPI_MemoryMappedTypeDef sMemMappedCfg;

  /* Configure XSPI2 */
  hxspi.Instance = XSPI2;
  hxspi.Init.FifoThresholdByte = 4;
  hxspi.Init.MemorySize = 25;  /* 64MB = 2^26, so MemorySize = 26-1 = 25 */
  hxspi.Init.ChipSelectHighTimeCycle = 1;
  hxspi.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi.Init.ClockPrescaler = 7;  /* Divide by 8 for safe slow speed */
  hxspi.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
  hxspi.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;

  if (HAL_XSPI_Init(&hxspi) != HAL_OK)
  {
    return -1;
  }

  /* Reset flash to SPI mode (boot ROM left it in Octal DTR mode) */
  /* Send Reset Enable (0x66) in OPI DTR mode */
  sCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
  sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.Instruction = 0x6600;  /* Reset Enable in OPI DTR (command repeated) */
  sCommand.AddressMode = HAL_XSPI_ADDRESS_NONE;
  sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.DataMode = HAL_XSPI_DATA_NONE;
  sCommand.DummyCycles = 0;
  sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;
  HAL_XSPI_Command(&hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);

  /* Send Reset (0x99) in OPI DTR mode */
  sCommand.Instruction = 0x9900;  /* Reset in OPI DTR */
  HAL_XSPI_Command(&hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);

  /* Wait for reset to complete (tRST max = 30us, use 1ms to be safe) */
  HAL_Delay(1);

  /* Configure READ command for memory-mapped mode (Fast Read 0x0B) */
  sCommand.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
  sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
  sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  sCommand.Instruction = 0x0B;  /* Fast Read command */
  sCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
  sCommand.AddressWidth = HAL_XSPI_ADDRESS_24_BITS;
  sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
  sCommand.Address = 0;
  sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.DataMode = HAL_XSPI_DATA_1_LINE;
  sCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
  sCommand.DataLength = 0;
  sCommand.DummyCycles = 8;  /* Fast Read requires 8 dummy cycles */
  sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

  if (HAL_XSPI_Command(&hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return -2;
  }

  /* Configure WRITE command for memory-mapped mode (Page Program 0x02) */
  sCommand.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
  sCommand.Instruction = 0x02;  /* Page Program command */
  sCommand.DummyCycles = 0;     /* No dummy cycles for write */

  if (HAL_XSPI_Command(&hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return -4;
  }

  /* Enable memory-mapped mode */
  sMemMappedCfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
  sMemMappedCfg.TimeoutPeriodClock = 0;

  if (HAL_XSPI_MemoryMapped(&hxspi, &sMemMappedCfg) != HAL_OK)
  {
    return -3;
  }

  return 0;
}

/**
  * @brief  Load NuttX from external flash to RAM and jump to it
  * @retval None (should not return)
  */
static void load_and_jump_to_nuttx(void)
{
  uint32_t *src = (uint32_t *)NUTTX_FLASH_ADDR;
  uint32_t *dst = (uint32_t *)NUTTX_RAM_ADDR;
  uint32_t initial_sp;
  uint32_t reset_handler;
  void (*nuttx_reset)(void);

  serial_puts("Loading NuttX from ");
  serial_puthex(NUTTX_FLASH_ADDR);
  serial_puts("\r\n");

  /* Debug: turn off blue to show we entered this function */
  BSP_LED_Off(LED_BLUE);
  HAL_Delay(200);

  /* Validate NuttX image - check for valid stack pointer and reset vector */
  serial_puts("Reading vector table...\r\n");
  initial_sp = src[0];
  reset_handler = src[1];

  serial_puts("  SP: ");
  serial_puthex(initial_sp);
  serial_puts("\r\n  Reset: ");
  serial_puthex(reset_handler);
  serial_puts("\r\n");

  /* Debug: blink green to show we read from flash */
  BSP_LED_On(LED_GREEN);
  HAL_Delay(100);
  BSP_LED_Off(LED_GREEN);
  HAL_Delay(100);

  /* Stack pointer should be in AXISRAM range (0x34000000-0x34200000) */
  /* PX4+NPU uses up to 2MB SRAM */
  if ((initial_sp < 0x34000000) || (initial_sp > 0x34200000))
  {
    serial_puts("ERROR: Invalid SP!\r\n");
    /* Invalid stack pointer - turn on red LED and halt */
    BSP_LED_On(LED_RED);
    return;
  }
  serial_puts("SP valid\r\n");

  /* Debug: blink green twice to show SP is valid */
  for (int i = 0; i < 2; i++) {
    BSP_LED_On(LED_GREEN);
    HAL_Delay(100);
    BSP_LED_Off(LED_GREEN);
    HAL_Delay(100);
  }

  /* Reset handler should be in AXISRAM2 range where NuttX will run */
  if ((reset_handler < 0x34000000) || (reset_handler > 0x34180000))
  {
    serial_puts("ERROR: Invalid reset handler!\r\n");
    /* Invalid reset handler - turn on red and green LEDs and halt */
    BSP_LED_On(LED_RED);
    BSP_LED_On(LED_GREEN);
    return;
  }
  serial_puts("Reset handler valid\r\n");

  /* Debug: blink green 3 times to show reset handler is valid, about to copy */
  for (int i = 0; i < 3; i++) {
    BSP_LED_On(LED_GREEN);
    HAL_Delay(100);
    BSP_LED_Off(LED_GREEN);
    HAL_Delay(100);
  }

  /* Copy NuttX image from external flash to AXISRAM2 */
  serial_puts("Copying NuttX to RAM...\r\n");
  memcpy(dst, src, NUTTX_SIZE);

  /* Verify copy by checking first words in RAM */
  serial_puts("Verifying copy...\r\n");
  serial_puts("  RAM[0] (SP):    ");
  serial_puthex(dst[0]);
  serial_puts("\r\n  RAM[1] (Reset): ");
  serial_puthex(dst[1]);
  serial_puts("\r\n  RAM[2]:         ");
  serial_puthex(dst[2]);
  serial_puts("\r\n  RAM[3]:         ");
  serial_puthex(dst[3]);
  serial_puts("\r\n");

  if (dst[0] != initial_sp || dst[1] != reset_handler)
  {
    serial_puts("ERROR: Copy verification failed!\r\n");
    BSP_LED_On(LED_RED);
    return;
  }
  serial_puts("Copy verified OK\r\n");
  serial_puts("Jumping to NuttX...\r\n");
  HAL_Delay(10);  /* Let UART finish transmitting */

  /* Data synchronization barrier */
  __DSB();

  /* Instruction synchronization barrier */
  __ISB();

  /* Turn on green LED to indicate copy complete, about to jump */
  BSP_LED_On(LED_GREEN);

  /* Disable interrupts before jump */
  __disable_irq();

  /* Set the vector table to NuttX location */
  SCB->VTOR = NUTTX_RAM_ADDR;

  /* Clear MSPLIM and PSPLIM - critical for ARMv8-M!
   * The boot ROM sets these limits, and setting MSP outside the limit
   * will cause an immediate stack overflow fault. */
  __set_MSPLIM(0);
  __set_PSPLIM(0);

  /* Set the main stack pointer */
  __set_MSP(initial_sp);

  /* Get the NuttX reset handler address */
  nuttx_reset = (void (*)(void))reset_handler;

  /* Data synchronization barrier */
  __DSB();

  /* Instruction synchronization barrier */
  __ISB();

  /* Jump to NuttX */
  nuttx_reset();

  /* Should never reach here */
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
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
