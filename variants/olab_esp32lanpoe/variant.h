#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define I2C_SDA 2 // I2C pins for this board
#define I2C_SCL 16

// #define LED_PIN 25   // If defined we will blink this LED
#define BUTTON_PIN 0 // If defined, this will be used for user button presses

#define USE_RF95
#define LORA_DIO0 34
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC
#define LORA_RESET RADIOLIB_NC

#define LORA_SCK 33
#define LORA_MISO 35
#define LORA_MOSI 32
#define LORA_CS 5

// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
// #define ADC_MULTIPLIER 2

// #define BATTERY_PIN 39 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// #define ADC_CHANNEL ADC1_GPIO39_CHANNEL