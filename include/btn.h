#ifndef BTN_H
#define BTN_H

#include "defines.h"

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

/* button event declarations */
typedef enum
{
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG
}button_event_t;

#define LONG_PRESS_IN_SECONDS 3

void TaskButtonScan(void* arg);
void init_button();

#endif
