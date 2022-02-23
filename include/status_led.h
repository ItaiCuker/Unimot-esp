#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "defines.h"

#define STATUS_GPIO GPIO_NUM_2

/**
 * @brief enum for app status
 * 
 */
typedef enum
{
    STATUS_OK,
    STATUS_WIFI,
    STATUS_PROV
}status_led_t;

extern status_led_t status;

#define DELAY 150 / TICK  //delay for led blink

//macro for blink
#define BLINK {\
    gpio_set_level(STATUS_GPIO, 1);\
    vTaskDelay(DELAY);\
    gpio_set_level(STATUS_GPIO, 0);\
    vTaskDelay(DELAY);\
}

void TaskStartupLED(void *arg);            //task declaration
void init_status_led();

#endif