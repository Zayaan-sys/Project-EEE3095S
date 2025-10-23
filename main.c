/* EEE3095S Project
Zayaan Lodewyk 
LDWZAY001: Group 11 
*/
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body for Dongle Lock STM32 Application
 * with Flash-based EEPROM Emulation for Persistent Storage
 ******************************************************************************
 * @attention
 *
 * Dongle Lock - Secure Access Code Storage Device with EEPROM Emulation
 * EEE3095S Embedded Systems II Project
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This application is designed to store up to MAX_CODES secure access strings
 * persistently using the internal STM32 Flash memory, emulating EEPROM behavior.
 * Communication and command handling are done via UART (USART1).
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h> // Required for string manipulation functions (memset, memcpy, strcmp, etc.)
#include <stdio.h>  // Required for snprintf for formatted output
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Maximum number of access codes that can be stored
#define MAX_CODES 3
// Maximum length for each individual access code string
#define MAX_CODE_LENGTH 50
// Maximum length for the command buffer received over UART
#define MAX_COMMAND_LENGTH 100

// Flash memory configuration for STM32F446 (EEPROM Emulation)
// Using Sector 7 (128KB) at the end of Flash for persistent storage
#define FLASH_SECTOR_TO_USE     FLASH_SECTOR_7
// Start address of Sector 7 on STM32F446RE (for EEPROM data)
#define FLASH_STORAGE_ADDRESS   0x08060000 
// Magic number used to verify if the Flash data is valid and initialized
#define FLASH_MAGIC_NUMBER      0xABCD1234 
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
// UART handle structure for USART1 communication
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/**
 * @brief Structure defining the data format for persistent storage in Flash.
 * This structure must be consistent between saving and loading operations.
 */
typedef struct {
	uint32_t magic;           // Magic number for validation check
	uint8_t code_stored[MAX_CODES]; // Flags (1=stored, 0=empty) for each code slot
	char access_codes[MAX_CODES][MAX_CODE_LENGTH]; // Array to hold the actual access code strings
	uint32_t checksum;        // Simple checksum for data integrity verification
} PersistentStorage_t;

// RAM working copy of the persistent storage data
PersistentStorage_t storage;

// Communication buffers for UART
char rx_buffer[MAX_COMMAND_LENGTH]; // Buffer to store incoming UART data
char tx_buffer[MAX_COMMAND_LENGTH]; // Buffer for outgoing UART data/responses
uint8_t rx_index = 0;               // Current index in the rx_buffer
uint8_t command_ready = 0;          // Flag set to 1 when a complete command is received
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void process_command(char* command);          // Handles received serial commands
void init_code_storage(void);                 // Initializes/clears the RAM storage structure
uint8_t load_from_eeprom(void);               // Reads and validates data from Flash
uint8_t save_to_eeprom(void);                 // Erases Flash sector and writes data
uint32_t calculate_checksum(PersistentStorage_t* data); // Calculates data integrity checksum
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  Calculate simple additive checksum for data integrity
 * @param  data: Pointer to the PersistentStorage_t structure
 * @retval Calculated checksum (sum of all bytes excluding the checksum field itself)
 */
uint32_t calculate_checksum(PersistentStorage_t* data) {
	uint32_t sum = 0;
	uint8_t* ptr = (uint8_t*)data;

	// Calculate checksum for all data except the last uint32_t (the checksum field itself)
	for (uint32_t i = 0; i < (sizeof(PersistentStorage_t) - sizeof(uint32_t)); i++) {
		sum += ptr[i];
	}

	return sum;
}

/**
 * @brief  Load codes from Flash (EEPROM emulation)
 * @retval 1 if successful (valid data found and checksum matches), 0 if no valid data found
 */
uint8_t load_from_eeprom(void) {
	// Create a pointer to the start of the Flash storage address
	PersistentStorage_t* flash_data = (PersistentStorage_t*)FLASH_STORAGE_ADDRESS;

	// 1. Check magic number to see if the data has been written before
	if (flash_data->magic != FLASH_MAGIC_NUMBER) {
		return 0; // Data not valid (first run or corrupted magic)
	}

	// 2. Copy the stored data from Flash memory to the RAM working copy
	memcpy(&storage, flash_data, sizeof(PersistentStorage_t));

	// 3. Verify checksum for data corruption
	uint32_t calculated = calculate_checksum(&storage);
	if (calculated != storage.checksum) {
		// Checksum mismatch indicates corruption; initialize storage and return failure
		init_code_storage();
		return 0;
	}

	return 1; // Load successful
}

/**
 * @brief  Save codes from RAM working copy to Flash (EEPROM emulation)
 * @retval 1 if successful, 0 if failed (erase or write error)
 */
uint8_t save_to_eeprom(void) {
	HAL_StatusTypeDef status;
	FLASH_EraseInitTypeDef erase_config;
	uint32_t sector_error = 0;

	// 1. Prepare data for saving
	storage.magic = FLASH_MAGIC_NUMBER;
	storage.checksum = calculate_checksum(&storage); // Recalculate checksum before saving

	// 2. Unlock Flash programming
	HAL_FLASH_Unlock();

	// 3. Configure sector erase settings
	erase_config.TypeErase = FLASH_TYPEERASE_SECTORS;
	erase_config.Sector = FLASH_SECTOR_TO_USE;
	erase_config.NbSectors = 1;
	erase_config.VoltageRange = FLASH_VOLTAGE_RANGE_3; // Standard voltage range

	// 4. Erase the Flash sector
	status = HAL_FLASHEx_Erase(&erase_config, &sector_error);

	if (status != HAL_OK) {
		HAL_FLASH_Lock();
		return 0; // Erase failed
	}

	// 5. Write data to Flash word by word (32-bit programming)
	uint32_t* source = (uint32_t*)&storage;
	uint32_t address = FLASH_STORAGE_ADDRESS;
	// Calculate number of 32-bit words to write (rounds up using integer math)
	uint32_t words_to_write = (sizeof(PersistentStorage_t) + 3) / 4; 

	for (uint32_t i = 0; i < words_to_write; i++) {
		// Write the current 32-bit word
		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, source[i]);

		if (status != HAL_OK) {
			HAL_FLASH_Lock();
			return 0; // Program failed
		}

		address += 4; // Move to the next 32-bit address
	}

	// 6. Lock Flash programming
	HAL_FLASH_Lock();

	return 1; // Save successful
}

/**
 * @brief  Initialize code storage by clearing all codes and setting the magic number.
 * This resets the RAM copy to a default empty state.
 * @retval None
 */
void init_code_storage(void) {
	storage.magic = FLASH_MAGIC_NUMBER;

	for (int i = 0; i < MAX_CODES; i++) {
		// Clear the code string buffer
		memset(storage.access_codes[i], 0, MAX_CODE_LENGTH); 
		// Mark the slot as empty
		storage.code_stored[i] = 0;                         
	}

	// Calculate checksum for the newly initialized (empty) state
	storage.checksum = calculate_checksum(&storage);
}

/**
 * @brief  Process incoming commands received over UART
 * @param  command: The received null-terminated command string
 * @retval None
 */
void process_command(char* command) {
	char response[MAX_COMMAND_LENGTH];
	int i;

	// Strip trailing whitespace/control characters (Newline/CR) and spaces 
	// from the command string to ensure clean parsing.
	for (i = 0; command[i] != '\0'; i++) {
		if (command[i] == '\r' || command[i] == '\n' || command[i] == ' ') {
			command[i] = '\0';
			break;
		}
	}

	// CONNECT command: Simple handshaking
	if (strcmp(command, "CONNECT") == 0) {
		strcpy(response, "OK\n");
		HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
	}

	// GET_CODE_N commands (e.g., GET_CODE_1)
	else if (strncmp(command, "GET_CODE_", 9) == 0 && strlen(command) == 10) {
		// Extract the code number (1-based) and convert to 0-based index
		int code_num = command[9] - '1'; 

		if (code_num >= 0 && code_num < MAX_CODES) {
			if (storage.code_stored[code_num]) {
				// Code exists: format and send the code back
				snprintf(response, sizeof(response), "CODE:%s\n", storage.access_codes[code_num]);
			} else {
				// Code slot is empty
				strcpy(response, "NO_CODE\n");
			}
		} else {
			// Invalid code index
			strcpy(response, "ERROR\n");
		}
		HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
	}

	// SET_CODE_N:new_code commands (e.g., SET_CODE_1:mysecret)
	else if (strncmp(command, "SET_CODE_", 9) == 0) {
		// Extract the code number (1-based) and convert to 0-based index
		int code_num = command[9] - '1'; 
		// Find the delimiter ':'
		char* colon_pos = strchr(command, ':');

		// Check for valid index and if the colon delimiter was found
		if (code_num >= 0 && code_num < MAX_CODES && colon_pos != NULL) {
			// Pointer to the start of the new code string
			char* new_code = colon_pos + 1;

			// Store the code in RAM working copy
			// Use strncpy and null-terminate defensively to prevent buffer overflow
			strncpy(storage.access_codes[code_num], new_code, MAX_CODE_LENGTH - 1);
			storage.access_codes[code_num][MAX_CODE_LENGTH - 1] = '\0';
			storage.code_stored[code_num] = 1; // Mark the slot as used

			// Save the updated storage structure to persistent Flash memory
			if (save_to_eeprom()) {
				strcpy(response, "SAVED\n");
			} else {
				strcpy(response, "ERROR\n"); // Flash write failed
			}
		} else {
			strcpy(response, "ERROR\n"); // Command syntax error
		}
		HAL_UART_Transmit(&huart1, (uint8_t*)response, strlen(response), HAL_MAX_DELAY);
	}

	// CLEAR_ALL command - clears all codes and saves the empty state to EEPROM
	else if (strcmp(command, "CLEAR_ALL") == 0) {
		init_code_storage(); // Clear RAM copy
		if (save_to_eeprom()) {
			strcpy(response, "CLEARED\n");
		} else {
			strcpy(response, "ERROR\n"); // Flash write failed
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
 * @brief  UART receive complete callback (Interrupt Service Routine)
 * @param  huart: UART handle structure pointer
 * @retval None
 *
 * This function handles byte-by-byte reception and command assembly.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		// Check for end of command (newline or carriage return)
		if (rx_buffer[rx_index] == '\n' || rx_buffer[rx_index] == '\r') {
			rx_buffer[rx_index] = '\0'; // Null terminate the received command
			command_ready = 1;          // Set flag to signal main loop to process command
			rx_index = 0;               // Reset index for the next command
		} else {
			rx_index++; // Increment index for the next received byte
			if (rx_index >= MAX_COMMAND_LENGTH - 1) {
				rx_index = 0; // Prevent buffer overflow by resetting (discarding current command)
			}
		}

		// Re-enable the interrupt for the next single byte reception
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

	/* Initialize all configured peripherals (GPIO, UART, etc.) */
	MX_GPIO_Init();
	MX_USART1_UART_Init();
	/* USER CODE BEGIN 2 */

	/* Try to load codes from EEPROM (Flash). This checks the magic number and checksum. */
	if (!load_from_eeprom()) {
		/* If no valid data was found (first boot or corruption), initialize fresh */
		init_code_storage();
		// Note: We don't save the fresh state here, only when explicitly commanded by the PC.
	}

	/* Start the asynchronous, interrupt-driven UART reception for the first byte */
	HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_buffer[rx_index], 1);

	/* Send startup message over UART to signal readiness */
	char startup_msg[] = "Dongle Lock Ready (EEPROM Storage)\r\n";
	HAL_UART_Transmit(&huart1, (uint8_t*)startup_msg, strlen(startup_msg), HAL_MAX_DELAY);

	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
		// Main application loop: checks for the command_ready flag
		if (command_ready) {
			process_command(rx_buffer); // Process the fully received command
			command_ready = 0;          // Reset the flag for the next command
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
	// Initializes the RCC Oscillators and Clocks.
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/** Configure the main internal regulator output voltage
	 */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 * Uses HSI (High-Speed Internal) oscillator as the clock source.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE; // PLL is disabled
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 * System Clock (SYSCLK) is sourced from HSI.
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
	huart1.Init.Mode = UART_MODE_TX_RX;      // Enable both Transmit and Receive modes
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE; // No hardware flow control
	huart1.Init.OverSampling = UART_OVERSAMPLING_16; // 16x oversampling for better noise immunity
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
	// Enables the clock for GPIO Port A (where USART1 pins typically are)
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
	__disable_irq(); // Disable global interrupts
	while (1)
	{
		// Infinite loop in case of unrecoverable error
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 * where the assert_param error has occurred.
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
