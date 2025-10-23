/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body for Dongle Lock STM32 Application
  *                   with Flash-based EEPROM Emulation for Persistent Storage
  ******************************************************************************
  * @attention
  *
  * Dongle Lock - Secure Access Code Storage Device with EEPROM Emulation
  * EEE3095S Embedded Systems II Project
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAX_CODES 3
#define MAX_CODE_LENGTH 50
#define MAX_COMMAND_LENGTH 100

// Flash memory configuration for STM32F446
// Using Sector 7 (128KB) at the end of Flash for EEPROM emulation
#define FLASH_SECTOR_TO_USE    FLASH_SECTOR_7
#define FLASH_STORAGE_ADDRESS  0x08060000  // Start of Sector 7
#define FLASH_MAGIC_NUMBER     0xABCD1234  // Magic number to identify valid data
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
// Structure for persistent storage
typedef struct {
    uint32_t magic;                        // Magic number for validation
    uint8_t code_stored[MAX_CODES];       // Flags indicating stored codes
    char access_codes[MAX_CODES][MAX_CODE_LENGTH];  // The actual codes
    uint32_t checksum;                     // Simple checksum for data integrity
} PersistentStorage_t;

// RAM working copy
PersistentStorage_t storage;

// Communication buffers
char rx_buffer[MAX_COMMAND_LENGTH];
char tx_buffer[MAX_COMMAND_LENGTH];
uint8_t rx_index = 0;
uint8_t command_ready = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void process_command(char* command);
void init_code_storage(void);
uint8_t load_from_eeprom(void);
uint8_t save_to_eeprom(void);
uint32_t calculate_checksum(PersistentStorage_t* data);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Calculate simple checksum for data integrity
  * @param  data: Pointer to storage structure
  * @retval Calculated checksum
  */
uint32_t calculate_checksum(PersistentStorage_t* data) {
    uint32_t sum = 0;
    uint8_t* ptr = (uint8_t*)data;

    // Calculate checksum for all data except the checksum field itself
    for (uint32_t i = 0; i < (sizeof(PersistentStorage_t) - sizeof(uint32_t)); i++) {
        sum += ptr[i];
    }

    return sum;
}

/**
  * @brief  Load codes from Flash (EEPROM emulation)
  * @retval 1 if successful, 0 if no valid data found
  */
uint8_t load_from_eeprom(void) {
    PersistentStorage_t* flash_data = (PersistentStorage_t*)FLASH_STORAGE_ADDRESS;

    // Check magic number
    if (flash_data->magic != FLASH_MAGIC_NUMBER) {
        return 0;
    }

    // Copy data from Flash to RAM
    memcpy(&storage, flash_data, sizeof(PersistentStorage_t));

    // Verify checksum
    uint32_t calculated = calculate_checksum(&storage);
    if (calculated != storage.checksum) {
        init_code_storage();
        return 0;
    }

    return 1;
}

/**
  * @brief  Save codes to Flash (EEPROM emulation)
  * @retval 1 if successful, 0 if failed
  */
uint8_t save_to_eeprom(void) {
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_config;
    uint32_t sector_error = 0;

    // Set magic number and calculate checksum
    storage.magic = FLASH_MAGIC_NUMBER;
    storage.checksum = calculate_checksum(&storage);

    // Unlock Flash
    HAL_FLASH_Unlock();

    // Erase the sector
    erase_config.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_config.Sector = FLASH_SECTOR_TO_USE;
    erase_config.NbSectors = 1;
    erase_config.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7V to 3.6V operation

    status = HAL_FLASHEx_Erase(&erase_config, &sector_error);

    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        return 0;
    }

    // Write data to Flash word by word (32-bit)
    uint32_t* source = (uint32_t*)&storage;
    uint32_t address = FLASH_STORAGE_ADDRESS;
    uint32_t words_to_write = (sizeof(PersistentStorage_t) + 3) / 4; // Round up

    for (uint32_t i = 0; i < words_to_write; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, source[i]);

        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }

        address += 4;
    }

    // Lock Flash
    HAL_FLASH_Lock();

    return 1;
}

/**
  * @brief  Initialize code storage (clear all codes)
  * @retval None
  */
void init_code_storage(void) {
    storage.magic = FLASH_MAGIC_NUMBER;

    for (int i = 0; i < MAX_CODES; i++) {
        memset(storage.access_codes[i], 0, MAX_CODE_LENGTH);
        storage.code_stored[i] = 0;
    }

    storage.checksum = calculate_checksum(&storage);
}

/**
  * @brief  Process incoming commands from PC
  * @param  command: The received command string
  * @retval None
  */
void process_command(char* command) {
    char response[MAX_COMMAND_LENGTH];
    int i;

    // Remove ALL whitespace characters (newline, carriage return, spaces)
    for (i = 0; command[i] != '\0'; i++) {
        if (command[i] == '\r' || command[i] == '\n' || command[i] == ' ') {
            command[i] = '\0';
            break;
        }
    }

    // CONNECT command
    if (strcmp(command, "CONNECT") == 0) {
        strcpy(response, "OK\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }

    // GET_CODE_N commands
    else if (strncmp(command, "GET_CODE_", 9) == 0 && strlen(command) == 10) {
        int code_num = command[9] - '1'; // Convert to 0-based index

        if (code_num >= 0 && code_num < MAX_CODES) {
            if (storage.code_stored[code_num]) {
                // Code exists, send it back
                snprintf(response, sizeof(response), "CODE:%s\n", storage.access_codes[code_num]);
            } else {
                // Code doesn't exist
                strcpy(response, "NO_CODE\n");
            }
        } else {
            strcpy(response, "ERROR\n");
        }
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }

    // SET_CODE_N:new_code commands
    else if (strncmp(command, "SET_CODE_", 9) == 0) {
        int code_num = command[9] - '1'; // Convert to 0-based index
        char* colon_pos = strchr(command, ':');

        if (code_num >= 0 && code_num < MAX_CODES && colon_pos != NULL) {
            // Extract the new code
            char* new_code = colon_pos + 1;

            // Store the code in RAM
            strncpy(storage.access_codes[code_num], new_code, MAX_CODE_LENGTH - 1);
            storage.access_codes[code_num][MAX_CODE_LENGTH - 1] = '\0';
            storage.code_stored[code_num] = 1;

            // Save to EEPROM (Flash) for persistence
            if (save_to_eeprom()) {
                strcpy(response, "SAVED\n");
            } else {
                strcpy(response, "ERROR\n");
            }
        } else {
            strcpy(response, "ERROR\n");
        }
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }

    // CLEAR_ALL command - clears all codes and saves to EEPROM
    else if (strcmp(command, "CLEAR_ALL") == 0) {
        init_code_storage();
        if (save_to_eeprom()) {
            strcpy(response, "CLEARED\n");
        } else {
            strcpy(response, "ERROR\n");
        }
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }

    // DISCONNECT command
    else if (strcmp(command, "DISCONNECT") == 0) {
        strcpy(response, "BYE\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }

    // Unknown command
    else {
        strcpy(response, "ERROR\n");
        HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
    }
}
/**
  * @brief  UART receive callback
  * @param  huart: UART handle
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        // Check for end of command (newline)
        if (rx_buffer[rx_index] == '\n' || rx_buffer[rx_index] == '\r') {
            rx_buffer[rx_index] = '\0'; // Null terminate
            command_ready = 1;
            rx_index = 0; // Reset for next command
        } else {
            rx_index++;
            if (rx_index >= MAX_COMMAND_LENGTH - 1) {
                rx_index = 0; // Prevent buffer overflow
            }
        }

        // Continue receiving
        HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_buffer[rx_index], 1);
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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* Try to load codes from EEPROM */
  if (!load_from_eeprom()) {
      /* No valid data found, initialize fresh */
      init_code_storage();
  }

  /* Start UART reception */
  HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_buffer[rx_index], 1);

  /* Send startup message */
  char startup_msg[] = "Dongle Lock Ready (EEPROM Storage)\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t*)startup_msg, strlen(startup_msg), HAL_MAX_DELAY);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Process commands when ready
    if (command_ready) {
        process_command(rx_buffer);
        command_ready = 0;
    }
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

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
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
