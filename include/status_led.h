#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "defines.h"

void TaskStartupLED(void *arg);
void init_status_led();


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

void TaskStartupLED(void *status);            //task declaration
static TaskHandle_t HandleTaskStartupLED = NULL;    //handle for task
static status_led_t status = STATUS_OK;                           //variable for task
#define DELAY 150 / TICK  //delay for led blink

//macro for blink
#define BLINK {\
    gpio_set_level(STATUS_GPIO, 1);\
    vTaskDelay(DELAY);\
    gpio_set_level(STATUS_GPIO, 0);\
    vTaskDelay(DELAY);\
}

#endif