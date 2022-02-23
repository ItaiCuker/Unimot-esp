#ifndef BTN_H
#define BTN_H

#include "defines.h"

void TaskButtonScan(void* arg);
void init_button();

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);    //button event base

/* button event declarations */
typedef enum
{
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG
}button_event_t;

#define BUTTON_GPIO GPIO_NUM_12
static TaskHandle_t HandleTaskButtonScan = NULL;
#define LONG_PRESS_IN_SECONDS 3

#endif
