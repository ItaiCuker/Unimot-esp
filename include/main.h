#ifndef MAIN_H
#define MAIN_H

#include "Arduino.h"

#include "defines.h"

#include "esp_wifi.h"
#include "esp_timer.h"

#include "nvs_flash.h"
#include "driver/gpio.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "ir.h"
#include "btn.h"
#include "mqtt.h"

extern bool wasConnected;
extern DynamicJsonDocument doc;

void stopLearn();
void gotCode();
void digestCommand(char *sub_message);


#endif