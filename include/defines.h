#ifndef DEFINES_H
#define DEFINES_H

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <iotc.h>
#include <iotc_jwt.h>

//private key location in program
extern const uint8_t ec_pv_key_start[] asm("_binary_private_key_pem_start");
extern const uint8_t ec_pv_key_end[] asm("_binary_private_key_pem_end");

//event handlers declaration
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state);

//Log tag
static const char *TAG = "Unimot";

//tick period of esp32
#define TICK portTICK_PERIOD_MS

#endif