/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

volatile int32_t virtual_X = 0;
volatile int32_t current_X = 0;
volatile int32_t target_X  = 0;
volatile uint8_t e_stop_flag = 0;
volatile uint32_t last_telemetry_tick = 0;
uint16_t x_last_cnt = 0;

volatile int32_t virtual_Y = 0;
volatile int32_t current_Y = 0;
volatile int32_t target_Y  = 0;
uint16_t y_last_cnt = 0;

uint8_t rx_buffer[64];
uint8_t tx_buffer[64];

typedef struct {
    volatile int32_t* current_virtual_pos;
    int32_t target_pos;
    int32_t steps_total;
    int32_t steps_done;
    int32_t accel_steps;
    uint32_t current_delay;
    uint32_t min_delay;
    uint32_t max_delay;
    uint32_t tick_counter;
    uint8_t pulse_active;
    GPIO_TypeDef* step_port;
    uint16_t step_pin;
    GPIO_TypeDef* dir_port;
    uint16_t dir_pin;
} AxisProfile_t;

volatile AxisProfile_t axisX = {&virtual_X, 0, 0, 0, 0, 30, 5, 30, 0, 0, X_STEP_GPIO_Port, X_STEP_Pin, X_DIR_GPIO_Port, X_DIR_Pin};
volatile AxisProfile_t axisY = {&virtual_Y, 0, 0, 0, 0, 30, 5, 30, 0, 0, Y_STEP_GPIO_Port, Y_STEP_Pin, Y_DIR_GPIO_Port, Y_DIR_Pin};


volatile uint8_t uart_rx_restart = 0;
volatile uint8_t cmd_ready = 0;
uint8_t cmd_buffer[64];

typedef enum {
    STATE_IDLE,
    STATE_MOVING,
    STATE_HOMING,
    STATE_VERIFYING,
	STATE_CALIBRATING
} MachineState;

volatile MachineState system_state = STATE_IDLE;
volatile uint32_t verify_timer = 0;
volatile uint8_t correction_attempts = 0;
volatile uint8_t pending_calibration_x = 0;
volatile uint8_t pending_calibration_y = 0;
volatile int32_t calibration_start_steps = 0;


volatile int32_t test_x_MAX = 0;
volatile int32_t test_x_MIN = 0;
volatile int32_t test_y_MAX = 0;
volatile int32_t test_y_MIN = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void ProcessAxis(volatile AxisProfile_t* axis) {
    if (axis->steps_done >= axis->steps_total) return;


    if (!axis->pulse_active && axis->tick_counter == 0) {
        if (axis->target_pos > *(axis->current_virtual_pos)) {
            HAL_GPIO_WritePin(axis->dir_port, axis->dir_pin, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(axis->dir_port, axis->dir_pin, GPIO_PIN_SET);
        }
    }
    axis->tick_counter++;

    if (!axis->pulse_active && axis->tick_counter >= axis->current_delay) {
    	axis->tick_counter = 0;

        HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
        axis->pulse_active = 1;
        axis->steps_done++;


        if (axis->target_pos > *(axis->current_virtual_pos)) {
            (*(axis->current_virtual_pos))++;
        } else {
            (*(axis->current_virtual_pos))--;
        }

        // Rampa
        int32_t steps_left = axis->steps_total - axis->steps_done;
        if (axis->steps_done < axis->accel_steps) {
            if (axis->current_delay > axis->min_delay) axis->current_delay--;
        } else if (steps_left <= axis->accel_steps) {
            if (axis->current_delay < axis->max_delay) axis->current_delay++;
        }
    }

    if (axis->pulse_active && axis->tick_counter >= 1) {
        HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_SET);
        axis->pulse_active = 0;
    }
}
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

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
  HAL_TIM_Base_Start_IT(&htim6);

  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
  __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if (cmd_ready) {
		  cmd_ready = 0;
		  if (cmd_buffer[0] == 'S') {
			  __disable_irq();
	          e_stop_flag = 1;
	          virtual_X = current_X; target_X = current_X;
	          virtual_Y = current_Y; target_Y = current_Y;
	          system_state = STATE_IDLE;
	          correction_attempts = 0;
	          __enable_irq();
		  }
		  else if (cmd_buffer[0] == 'M') {
			  if (e_stop_flag == 0) {
				  long parsed_X = 0, parsed_Y = 0;
				  if (sscanf((char*)cmd_buffer, "M:%ld:%ld", &parsed_X, &parsed_Y) == 2) {
					  __disable_irq();
					  target_X = parsed_X;
	                  axisX.steps_done = axisX.steps_total;
	                  axisX.target_pos = target_X;
	                  axisX.steps_total = abs(axisX.target_pos - (*axisX.current_virtual_pos));
	                  axisX.steps_done = 0;
	                  axisX.current_delay = axisX.max_delay;
	                  axisX.accel_steps = axisX.steps_total / 3;
	                  if (axisX.accel_steps > 400) axisX.accel_steps = 400;
	                  axisX.tick_counter = 0;
	                  axisX.pulse_active = 0;

	                  target_Y = parsed_Y;
	                  axisY.steps_done = axisY.steps_total;
	                  axisY.target_pos = target_Y;
	                  axisY.steps_total = abs(axisY.target_pos - (*axisY.current_virtual_pos));
	                  axisY.steps_done = 0;
	                  axisY.current_delay = axisY.max_delay;
	                  axisY.accel_steps = axisY.steps_total / 3;
	                  if (axisY.accel_steps > 400) axisY.accel_steps = 400;
	                  axisY.tick_counter = 0;
	                  axisY.pulse_active = 0;
                      system_state = STATE_MOVING;
                      correction_attempts = 0;
                      __enable_irq();
	                    }
	                }
	            }
	            else if (cmd_buffer[0] == 'H') {
	                if (e_stop_flag == 0) {
	                    __disable_irq();

	                    // Homing X
	                    target_X = -99999;
	                    axisX.steps_done = axisX.steps_total;
	                    axisX.target_pos = -99999;
	                    axisX.steps_total = 99999;
	                    axisX.steps_done = 0;
	                    axisX.current_delay = axisX.max_delay;
	                    axisX.accel_steps = 200;
	                    axisX.tick_counter = 0; axisX.pulse_active = 0;

	                    // Homing Y
	                    target_Y = -99999;
	                    axisY.steps_done = axisY.steps_total;
	                    axisY.target_pos = -99999;
	                    axisY.steps_total = 99999;
	                    axisY.steps_done = 0;
	                    axisY.current_delay = axisY.max_delay;
	                    axisY.accel_steps = 200;
	                    axisY.tick_counter = 0; axisY.pulse_active = 0;

	                    system_state = STATE_HOMING;
	                    correction_attempts = 0;
	                    __enable_irq();
	                }
	            }
	            else if (cmd_buffer[0] == 'C') {
	                if (e_stop_flag == 0) {
	                    __disable_irq();
	                    pending_calibration_x = 1;
	                    pending_calibration_y = 1;


	                    target_X = -99999; axisX.steps_done = axisX.steps_total; axisX.target_pos = -99999;
	                    axisX.steps_total = 99999; axisX.steps_done = 0; axisX.current_delay = axisX.max_delay;
	                    axisX.accel_steps = 200; axisX.tick_counter = 0; axisX.pulse_active = 0;

	                    target_Y = -99999; axisY.steps_done = axisY.steps_total; axisY.target_pos = -99999;
	                    axisY.steps_total = 99999; axisY.steps_done = 0; axisY.current_delay = axisY.max_delay;
	                    axisY.accel_steps = 200; axisY.tick_counter = 0; axisY.pulse_active = 0;

	                    system_state = STATE_HOMING;
	                    correction_attempts = 0;
	                    __enable_irq();
	                }
	            }
	            else if (cmd_buffer[0] == 'R') {
	                __disable_irq();
	                e_stop_flag = 0;
	                target_X  = current_X; virtual_X = current_X; axisX.target_pos = current_X; axisX.steps_done = axisX.steps_total;
	                target_Y  = current_Y; virtual_Y = current_Y; axisY.target_pos = current_Y; axisY.steps_done = axisY.steps_total;

	                system_state = STATE_IDLE;
	                correction_attempts = 0;
	                __enable_irq();
	            }
	        }

	        // --- MASZYNA STANÓW: ZAKOŃCZENIE RUCHU I KOREKTA ---

	        if (system_state == STATE_MOVING && axisX.steps_done >= axisX.steps_total && axisY.steps_done >= axisY.steps_total) {
	            system_state = STATE_VERIFYING;
	            verify_timer = HAL_GetTick();
	        }

	        if (system_state == STATE_VERIFYING && (HAL_GetTick() - verify_timer > 100)) {


	            int32_t expected_encoder_x = (target_X * 16249) / 21798;
	            int32_t error_x = expected_encoder_x - current_X;


	            int32_t expected_encoder_y = (target_Y * 50608) / 8452;
	            int32_t error_y = expected_encoder_y - current_Y;

	            if (abs(error_x) > 5 || abs(error_y) > 5) {
	                if (correction_attempts < 3) {
	                    correction_attempts++;
	                    __disable_irq();


	                    if (abs(error_x) > 5) {
	                        int32_t steps_to_correct_x = (error_x * 21798) / 16249;
	                        target_X = target_X + steps_to_correct_x;
	                        axisX.target_pos = target_X;
	                        axisX.steps_total = abs(steps_to_correct_x);
	                        axisX.steps_done = 0;
	                        axisX.current_delay = axisX.max_delay;
	                        axisX.accel_steps = axisX.steps_total / 2;
	                        axisX.tick_counter = 0; axisX.pulse_active = 0;
	                    }


//	                    if (abs(error_y) > 5) {
//	                        int32_t steps_to_correct_y = error_y;
//	                        target_Y = target_Y + steps_to_correct_y;
//	                        axisY.target_pos = target_Y;
//	                        axisY.steps_total = abs(steps_to_correct_y);
//	                        axisY.steps_done = 0;
//	                        axisY.current_delay = axisY.max_delay;
//	                        axisY.accel_steps = axisY.steps_total / 2;
//	                        axisY.tick_counter = 0; axisY.pulse_active = 0;
//	                    }

	                    system_state = STATE_MOVING;
	                    __enable_irq();
	                } else {
	                    system_state = STATE_IDLE;
	                    if (huart2.gState == HAL_UART_STATE_READY) {
	                        sprintf((char*)tx_buffer, "ERR:Limit korekt przekroczony\n");
	                        HAL_UART_Transmit_DMA(&huart2, tx_buffer, strlen((char*)tx_buffer));
	                    }
	                }
	            } else {
	                system_state = STATE_IDLE;
	                correction_attempts = 0;
	                if (huart2.gState == HAL_UART_STATE_READY) {
	                    sprintf((char*)tx_buffer, "OK:%ld:%ld\n", current_X, current_Y);
	                    HAL_UART_Transmit_DMA(&huart2, tx_buffer, strlen((char*)tx_buffer));
	                }
	            }
	        }


	        if (HAL_GetTick() - last_telemetry_tick >= 50) {
	            last_telemetry_tick = HAL_GetTick();


	            uint16_t raw_x = (uint16_t)TIM2->CNT;
	            current_X += (int16_t)(raw_x - x_last_cnt);
	            x_last_cnt = raw_x;

	            uint16_t raw_y = (uint16_t)TIM3->CNT;
	            current_Y += (int16_t)(raw_y - y_last_cnt);
	            y_last_cnt = raw_y;


	            if (huart2.gState == HAL_UART_STATE_READY) {
	                sprintf((char*)tx_buffer, "P:%ld:%ld\n", current_X, current_Y);
	                HAL_UART_Transmit_DMA(&huart2, tx_buffer, strlen((char*)tx_buffer));
	            }
	        }


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
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_TIM2
                              |RCC_PERIPHCLK_TIM34;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.Tim2ClockSelection = RCC_TIM2CLK_HCLK;
  PeriphClkInit.Tim34ClockSelection = RCC_TIM34CLK_HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 71;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 99;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, X_STEP_Pin|Y_STEP_Pin|X_DIR_Pin|Y_DIR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : B1_Pin X_MAX_Pin X_MIN_Pin Y_MIN_Pin
                           Y_MAX_Pin */
  GPIO_InitStruct.Pin = B1_Pin|X_MAX_Pin|X_MIN_Pin|Y_MIN_Pin
                          |Y_MAX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : X_STEP_Pin Y_STEP_Pin X_DIR_Pin Y_DIR_Pin */
  GPIO_InitStruct.Pin = X_STEP_Pin|Y_STEP_Pin|X_DIR_Pin|Y_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */


void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);

        if (Size > 0 && Size < sizeof(rx_buffer)) {
            rx_buffer[Size] = '\0';
            memcpy(cmd_buffer, rx_buffer, Size + 1);
            cmd_ready = 1;
        }


        memset(rx_buffer, 0, sizeof(rx_buffer));
        HAL_UART_DMAStop(&huart2);
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, sizeof(rx_buffer));
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        if (e_stop_flag) return;
        ProcessAxis(&axisX);
        ProcessAxis(&axisY);
    }
}


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{

    // --- LEWA KRAŃCÓWKA (MIN) X ---
    if (GPIO_Pin == X_MIN_Pin) {
    	test_x_MIN++;
        if (system_state == STATE_CALIBRATING) return;

        TIM2->CNT = 0; current_X = 0; virtual_X = 0; target_X = 0;
        axisX.target_pos = 0; axisX.steps_done = axisX.steps_total; x_last_cnt = 0;


        e_stop_flag = 0;

        if (pending_calibration_x == 1) {
            pending_calibration_x = 0;
            target_X = 99999; axisX.target_pos = 99999; axisX.steps_total = 99999; axisX.steps_done = 0;
            axisX.current_delay = axisX.max_delay; axisX.accel_steps = 200; axisX.tick_counter = 0; axisX.pulse_active = 0;
            system_state = STATE_CALIBRATING;
        } else {

            if (axisY.steps_done >= axisY.steps_total) system_state = STATE_IDLE;
        }
    }
    // --- PRAWA KRAŃCÓWKA (MAX) X LUB PRZYCISK ---
    else if (GPIO_Pin == X_MAX_Pin || GPIO_Pin == B1_Pin) {
    	test_x_MAX++;
        axisX.steps_done = axisX.steps_total;

        if (system_state == STATE_CALIBRATING && GPIO_Pin == X_MAX_Pin) {
            int32_t motor_steps_taken = virtual_X;
            if (huart2.gState == HAL_UART_STATE_READY) {
                sprintf((char*)tx_buffer, "CAL_X:%ld:%ld\n", motor_steps_taken, current_X);
                HAL_UART_Transmit_DMA(&huart2, tx_buffer, strlen((char*)tx_buffer));
            }
            if (axisY.steps_done >= axisY.steps_total) system_state = STATE_IDLE;
        } else {
            e_stop_flag = 1; target_X = current_X; virtual_X = current_X;
            system_state = STATE_IDLE; correction_attempts = 0;
        }
    }



    // --- LEWA KRAŃCÓWKA (MIN) Y ---
    else if (GPIO_Pin == Y_MIN_Pin) {
    	test_y_MIN++;
        if (system_state == STATE_CALIBRATING) return;

        TIM3->CNT = 0; current_Y = 0; virtual_Y = 0; target_Y = 0;
        axisY.target_pos = 0; axisY.steps_done = axisY.steps_total; y_last_cnt = 0;
        e_stop_flag = 0;

        if (pending_calibration_y == 1) {
            pending_calibration_y = 0;
            target_Y = 99999; axisY.target_pos = 99999; axisY.steps_total = 99999; axisY.steps_done = 0;
            axisY.current_delay = axisY.max_delay; axisY.accel_steps = 200; axisY.tick_counter = 0; axisY.pulse_active = 0;
            system_state = STATE_CALIBRATING;
        } else {
            if (axisX.steps_done >= axisX.steps_total) system_state = STATE_IDLE;
        }
    }
    // --- PRAWA KRAŃCÓWKA (MAX) Y ---
    else if (GPIO_Pin == Y_MAX_Pin) {
    	test_y_MAX++;
        axisY.steps_done = axisY.steps_total;

        if (system_state == STATE_CALIBRATING) {
            int32_t motor_steps_taken = virtual_Y;
            if (huart2.gState == HAL_UART_STATE_READY) {
                sprintf((char*)tx_buffer, "CAL_Y:%ld:%ld\n", motor_steps_taken, current_Y);
                HAL_UART_Transmit_DMA(&huart2, tx_buffer, strlen((char*)tx_buffer));
            }
            if (axisX.steps_done >= axisX.steps_total) system_state = STATE_IDLE;
        } else {
            e_stop_flag = 1; target_Y = current_Y; virtual_Y = current_Y;
            system_state = STATE_IDLE; correction_attempts = 0;
        }
    }
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
