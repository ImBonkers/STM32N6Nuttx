/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOQ_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOP_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pins : PQ6 PQ3 PQ4 PQ5
                           PQ2 PQ1 PQ0 PQ7 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_2|GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOQ, &GPIO_InitStruct);

  /*Configure GPIO pins : PH0 PH3 PH6 PH1
                           PH8 PH7 PH2 PH4
                           PH5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_3|GPIO_PIN_6|GPIO_PIN_1
                          |GPIO_PIN_8|GPIO_PIN_7|GPIO_PIN_2|GPIO_PIN_4
                          |GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /*Configure GPIO pins : PC10 PC4 PC11 PC6
                           PC5 PC15 PC9 PC12
                           PC7 PC0 PC14 PC8
                           PC2 PC3 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_4|GPIO_PIN_11|GPIO_PIN_6
                          |GPIO_PIN_5|GPIO_PIN_15|GPIO_PIN_9|GPIO_PIN_12
                          |GPIO_PIN_7|GPIO_PIN_0|GPIO_PIN_14|GPIO_PIN_8
                          |GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : I2C1_SDA_Pin */
  GPIO_InitStruct.Pin = I2C1_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(I2C1_SDA_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PE13 PE15 PE8 PE9
                           PE14 PE12 PE7 PE10
                           PE4 PE11 PE2 PE0
                           PE1 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_9
                          |GPIO_PIN_14|GPIO_PIN_12|GPIO_PIN_7|GPIO_PIN_10
                          |GPIO_PIN_4|GPIO_PIN_11|GPIO_PIN_2|GPIO_PIN_0
                          |GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PD1 PD15 PD4 PD12
                           PD14 PD13 PD0 PD5
                           PD7 PD3 PD8 PD11
                           PD9 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_4|GPIO_PIN_12
                          |GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_5
                          |GPIO_PIN_7|GPIO_PIN_3|GPIO_PIN_8|GPIO_PIN_11
                          |GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : PB14 PB13 PB2 PB9
                           PB8 PB15 PB12 PB1
                           PB4 */
  GPIO_InitStruct.Pin = GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_2|GPIO_PIN_9
                          |GPIO_PIN_8|GPIO_PIN_15|GPIO_PIN_12|GPIO_PIN_1
                          |GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : I2CA_SCL_Pin */
  GPIO_InitStruct.Pin = I2CA_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(I2CA_SCL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PP7 PP6 PP0 PP4
                           PP1 PP15 PP5 PP12
                           PP3 PP2 PP13 PP11
                           PP8 PP14 PP9 PP10 */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_6|GPIO_PIN_0|GPIO_PIN_4
                          |GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_5|GPIO_PIN_12
                          |GPIO_PIN_3|GPIO_PIN_2|GPIO_PIN_13|GPIO_PIN_11
                          |GPIO_PIN_8|GPIO_PIN_14|GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOP, &GPIO_InitStruct);

  /*Configure GPIO pins : PO1 PO2 PO3 PO0
                           PO4 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_0
                          |GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOO, &GPIO_InitStruct);

  /*Configure GPIO pins : PG7 PG6 PG5 PG4
                           PG14 PG3 PG15 PG1
                           PG12 PG2 PG9 PG13
                           PG11 */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_6|GPIO_PIN_5|GPIO_PIN_4
                          |GPIO_PIN_14|GPIO_PIN_3|GPIO_PIN_15|GPIO_PIN_1
                          |GPIO_PIN_12|GPIO_PIN_2|GPIO_PIN_9|GPIO_PIN_13
                          |GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : PF4 PF10 PF6 PF7
                           PF3 PF5 PF15 PF14
                           PF8 PF2 PF9 PF11
                           PF1 PF13 PF0 PF12 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_10|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_3|GPIO_PIN_5|GPIO_PIN_15|GPIO_PIN_14
                          |GPIO_PIN_8|GPIO_PIN_2|GPIO_PIN_9|GPIO_PIN_11
                          |GPIO_PIN_1|GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : PA9 PA6 PA1 PA10
                           PA3 UCPD1_VSENSE_Pin PA2 PA12
                           PA8 PA0 PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_6|GPIO_PIN_1|GPIO_PIN_10
                          |GPIO_PIN_3|UCPD1_VSENSE_Pin|GPIO_PIN_2|GPIO_PIN_12
                          |GPIO_PIN_8|GPIO_PIN_0|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : I2C2_SDA_Pin I2C2_SCL_Pin */
  GPIO_InitStruct.Pin = I2C2_SDA_Pin|I2C2_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
