#ifndef DEFINES_H
#define DEFINES_H

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "ArduinoJson.h"

#include "status_led.h"

//private key location in program
extern const uint8_t ec_pv_key_start[] asm("_binary_private_key_pem_start");
extern const uint8_t ec_pv_key_end[] asm("_binary_private_key_pem_end");

//Log tag
#define TAG "Unimot"

//tick period of esp32
#define TICK portTICK_PERIOD_MS

//event handlers declaration
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

//gpios
#define GPIO_BTN GPIO_NUM_13
#define GPIO_STATUS GPIO_NUM_2
#define GPIO_RX GPIO_NUM_12
#define GPIO_TX GPIO_NUM_14

#endif