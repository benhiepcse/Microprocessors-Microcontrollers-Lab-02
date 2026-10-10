/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DIGIT_COUNT             4U
#define MATRIX_SIZE             8U

/* TIM2 tạo một ngắt mỗi 10 ms */
#define DIGIT_INTERVAL_TICKS    50U
#define DOT_INTERVAL_TICKS      100U
#define ZOOM_INTERVAL_TICKS     50U

#define COLUMN_HOLD_MS          2U

/* Mức điều khiển theo mạch Proteus hiện tại */
#define MATRIX_COLUMN_ON        GPIO_PIN_RESET
#define MATRIX_COLUMN_OFF       GPIO_PIN_SET
#define MATRIX_PIXEL_ON         GPIO_PIN_RESET
#define MATRIX_PIXEL_OFF        GPIO_PIN_SET
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/* SEG0..SEG6 thuộc GPIOB */
static const uint16_t segmentPins[7] = {
    SEG0_Pin, SEG1_Pin, SEG2_Pin, SEG3_Pin,
    SEG4_Pin, SEG5_Pin, SEG6_Pin
};

/* EN0..EN3 thuộc GPIOA */
static const uint16_t digitPins[DIGIT_COUNT] = {
    EN0_Pin, EN1_Pin, EN2_Pin, EN3_Pin
};

/* ENM0..ENM7 thuộc GPIOA */
static const uint16_t columnPins[MATRIX_SIZE] = {
    ENM0_Pin, ENM1_Pin, ENM2_Pin, ENM3_Pin,
    ENM4_Pin, ENM5_Pin, ENM6_Pin, ENM7_Pin
};

/* ROW0..ROW7 thuộc GPIOB */
static const uint16_t rowPins[MATRIX_SIZE] = {
    ROW0_Pin, ROW1_Pin, ROW2_Pin, ROW3_Pin,
    ROW4_Pin, ROW5_Pin, ROW6_Pin, ROW7_Pin
};

/* Các số hiển thị lần lượt */
static const uint8_t displayDigits[DIGIT_COUNT] = {
    1, 2, 3, 0
};

/*
 * Ba kích thước chữ A:
 * Mẫu 0: nhỏ, 3 cột x 4 hàng.
 * Mẫu 1: vừa, 5 cột x 6 hàng.
 * Mẫu 2: lớn, 6 cột x 8 hàng.
 *
 * Mỗi byte chứa tám điểm ảnh của một cột.
 * Bit bằng 1 nghĩa là điểm ảnh cần sáng.
 */
static const uint8_t letterFrames[3][MATRIX_SIZE] = {
    /* A nhỏ */
    {0x00, 0x00, 0x38, 0x14, 0x38, 0x00, 0x00, 0x00},

    /* A vừa */
    {0x00, 0x7C, 0x12, 0x12, 0x12, 0x7C, 0x00, 0x00},

    /* A lớn */
    {0x00, 0xFE, 0x11, 0x11, 0x11, 0x11, 0xFE, 0x00}
};

/* Nhỏ -> vừa -> lớn -> vừa -> lặp lại */
static const uint8_t zoomSequence[4] = {
    0, 1, 2, 1
};

static uint8_t zoomStep = 0;
static uint8_t currentFrame = 0;
static uint8_t activeDigit = 0;
static uint8_t activeColumn = 0;

static uint16_t digitTicks = 0;
static uint16_t dotTicks = 0;
static uint16_t zoomTicks = 0;

/* Ngắt báo thời điểm đổi mẫu, main thực hiện đổi */
static volatile uint8_t zoomPending = 0;

/* Mã đoạn: bit 0..6 tương ứng SEG0..SEG6 */
static uint8_t getSegmentPattern(uint8_t number)
{
    switch (number)
    {
        case 0: return 0x3F;
        case 1: return 0x06;
        case 2: return 0x5B;
        case 3: return 0x4F;
        case 4: return 0x66;
        case 5: return 0x6D;
        case 6: return 0x7D;
        case 7: return 0x07;
        case 8: return 0x7F;
        case 9: return 0x6F;
        default: return 0x00;
    }
}

/* LOW làm đoạn LED sáng */
static void display7SEG(uint8_t number)
{
    uint8_t pattern = getSegmentPattern(number);

    for (uint8_t segment = 0; segment < 7U; segment++)
    {
        GPIO_PinState level = GPIO_PIN_SET;

        if ((pattern & (1U << segment)) != 0U)
        {
            level = GPIO_PIN_RESET;
        }

        HAL_GPIO_WritePin(GPIOB, segmentPins[segment], level);
    }
}

/* Chuyển sang vị trí LED 7 đoạn tiếp theo */
static void selectNextDigit(void)
{
    HAL_GPIO_WritePin(
        GPIOA, digitPins[activeDigit], GPIO_PIN_SET
    );

    activeDigit++;
    if (activeDigit >= DIGIT_COUNT)
    {
        activeDigit = 0;
    }

    display7SEG(displayDigits[activeDigit]);

    HAL_GPIO_WritePin(
        GPIOA, digitPins[activeDigit], GPIO_PIN_RESET
    );
}

/* Hiển thị một cột của mẫu chữ A hiện tại */
static void displayMatrixColumn(uint8_t column)
{
    /* Tắt cột cũ trước khi thay dữ liệu hàng */
    HAL_GPIO_WritePin(
        GPIOA, columnPins[activeColumn], MATRIX_COLUMN_OFF
    );

    uint8_t pattern = letterFrames[currentFrame][column];

    for (uint8_t row = 0; row < MATRIX_SIZE; row++)
    {
        GPIO_PinState level = MATRIX_PIXEL_OFF;

        if ((pattern & (1U << row)) != 0U)
        {
            level = MATRIX_PIXEL_ON;
        }

        HAL_GPIO_WritePin(GPIOB, rowPins[row], level);
    }

    /* Bật cột mới sau khi dữ liệu đã sẵn sàng */
    HAL_GPIO_WritePin(
        GPIOA, columnPins[column], MATRIX_COLUMN_ON
    );

    activeColumn = column;
}

void updateLEDMatrix(int index)
{
    if (index < 0 || index >= (int)MATRIX_SIZE)
    {
        return;
    }

    displayMatrixColumn((uint8_t)index);
}

/* Đổi kích thước theo chuỗi nhỏ -> vừa -> lớn -> vừa */
static void advanceZoomFrame(void)
{
    zoomStep++;
    if (zoomStep >= 4U)
    {
        zoomStep = 0;
    }

    currentFrame = zoomSequence[zoomStep];
}

/* Thiết lập trạng thái ban đầu */
static void initializeDisplays(void)
{
    activeDigit = 0;
    activeColumn = 0;
    zoomStep = 0;
    currentFrame = zoomSequence[zoomStep];

    digitTicks = 0;
    dotTicks = 0;
    zoomTicks = 0;
    zoomPending = 0;

    /* Tắt toàn bộ vị trí LED 7 đoạn */
    for (uint8_t digit = 0; digit < DIGIT_COUNT; digit++)
    {
        HAL_GPIO_WritePin(
            GPIOA, digitPins[digit], GPIO_PIN_SET
        );
    }

    /* Tắt toàn bộ cột ma trận */
    for (uint8_t column = 0; column < MATRIX_SIZE; column++)
    {
        HAL_GPIO_WritePin(
            GPIOA, columnPins[column], MATRIX_COLUMN_OFF
        );
    }

    /* Tắt dữ liệu điểm ảnh */
    for (uint8_t row = 0; row < MATRIX_SIZE; row++)
    {
        HAL_GPIO_WritePin(
            GPIOB, rowPins[row], MATRIX_PIXEL_OFF
        );
    }

    /* D2/D3 ban đầu tắt */
    HAL_GPIO_WritePin(
        DOT_GPIO_Port, DOT_Pin, GPIO_PIN_SET
    );

    /* D1 luôn sáng với cách mắc LED từ nguồn về PA5 */
    HAL_GPIO_WritePin(
        Led_Red_GPIO_Port, Led_Red_Pin, GPIO_PIN_RESET
    );

    /* Bật số đầu tiên */
    display7SEG(displayDigits[activeDigit]);

    HAL_GPIO_WritePin(
        GPIOA, digitPins[activeDigit], GPIO_PIN_RESET
    );
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
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  initializeDisplays();

  if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
  {
      Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	    for (uint8_t column = 0; column < MATRIX_SIZE; column++)
	    {
	    	updateLEDMatrix((int)column);
	        HAL_Delay(COLUMN_HOLD_MS);
	    }

	    /* Tắt cột cuối trước khi chuyển mẫu */
	    HAL_GPIO_WritePin(
	        GPIOA, columnPins[activeColumn], MATRIX_COLUMN_OFF
	    );

	    if (zoomPending != 0U)
	    {
	        zoomPending = 0;
	        advanceZoomFrame();
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
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
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, DOT_Pin|Led_Red_Pin|EN0_Pin|EN1_Pin
                          |EN2_Pin|EN3_Pin|ENM0_Pin|ENM1_Pin
                          |ENM2_Pin|ENM3_Pin|ENM4_Pin|ENM5_Pin
                          |ENM6_Pin|ENM7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SEG0_Pin|SEG1_Pin|SEG2_Pin|SEG3_Pin
                          |SEG4_Pin|SEG5_Pin|SEG6_Pin|ROW0_Pin
                          |ROW1_Pin|ROW2_Pin|ROW3_Pin|ROW4_Pin
                          |ROW5_Pin|ROW6_Pin|ROW7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DOT_Pin Led_Red_Pin EN0_Pin EN1_Pin
                           EN2_Pin EN3_Pin ENM0_Pin ENM1_Pin
                           ENM2_Pin ENM3_Pin ENM4_Pin ENM5_Pin
                           ENM6_Pin ENM7_Pin */
  GPIO_InitStruct.Pin = DOT_Pin|Led_Red_Pin|EN0_Pin|EN1_Pin
                          |EN2_Pin|EN3_Pin|ENM0_Pin|ENM1_Pin
                          |ENM2_Pin|ENM3_Pin|ENM4_Pin|ENM5_Pin
                          |ENM6_Pin|ENM7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SEG0_Pin SEG1_Pin SEG2_Pin SEG3_Pin
                           SEG4_Pin SEG5_Pin SEG6_Pin ROW0_Pin
                           ROW1_Pin ROW2_Pin ROW3_Pin ROW4_Pin
                           ROW5_Pin ROW6_Pin ROW7_Pin */
  GPIO_InitStruct.Pin = SEG0_Pin|SEG1_Pin|SEG2_Pin|SEG3_Pin
                          |SEG4_Pin|SEG5_Pin|SEG6_Pin|ROW0_Pin
                          |ROW1_Pin|ROW2_Pin|ROW3_Pin|ROW4_Pin
                          |ROW5_Pin|ROW6_Pin|ROW7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
    {
        return;
    }

    /* Đổi vị trí LED 7 đoạn mỗi 500 ms */
    digitTicks++;
    if (digitTicks >= DIGIT_INTERVAL_TICKS)
    {
        digitTicks = 0;
        selectNextDigit();
    }

    /* D2/D3 đảo trạng thái mỗi 1000 ms */
    dotTicks++;
    if (dotTicks >= DOT_INTERVAL_TICKS)
    {
        dotTicks = 0;
        HAL_GPIO_TogglePin(DOT_GPIO_Port, DOT_Pin);
    }

    /* Đổi kích thước chữ A mỗi 500 ms */
    zoomTicks++;
    if (zoomTicks >= ZOOM_INTERVAL_TICKS)
    {
        zoomTicks = 0;
        zoomPending = 1;
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

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
