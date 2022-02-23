#include "btn.h"


/**
 * @brief task to handle button scanning
 */
void TaskButtonScan(void* arg)
{
	uint16_t ticks = 0;

	ESP_LOGI(TAG, "Waiting For Press.");
	
	for (;;) 
	{        
        // Wait here to detect press
		while( gpio_get_level(BUTTON_GPIO) )
		{
			vTaskDelay(125 / TICK);
		}
		
		// Debounce
		vTaskDelay(50 / TICK);

		// Re-Read Button State After Debounce
		if (!gpio_get_level(BUTTON_GPIO)) 
		{
			ESP_LOGI(TAG, "BTN Pressed Down.");
			
			ticks = 0;
		
			// Loop here while pressed until user lets go, or longer that set time
			while ((!gpio_get_level(BUTTON_GPIO)) && (++ticks < LONG_PRESS_IN_SECONDS * 100))
			{
				vTaskDelay(10 / TICK);
			} 

			// Did fall here because user held a long press or let go for a short press
			if (ticks >= LONG_PRESS_IN_SECONDS * 100)
			{
				ESP_LOGI(TAG, "Long Press");
                ESP_ERROR_CHECK(esp_event_post(BUTTON_EVENT, BUTTON_EVENT_LONG, NULL, 0, portMAX_DELAY));
			}
			else
			{
				ESP_LOGI(TAG, "Short Press");
                ESP_ERROR_CHECK(esp_event_post(BUTTON_EVENT, BUTTON_EVENT_SHORT, NULL, 0, portMAX_DELAY));
			}

			// Wait here if they are still holding it
			while(!gpio_get_level(BUTTON_GPIO))
			{
				vTaskDelay(100 / TICK);
			}
			
			ESP_LOGI(TAG, "BTN Released.");
		}
	}
}

void init_button()
{
    /* Configure input and pull up to read button value */
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    //register button events to event handler and start task
    ESP_ERROR_CHECK(esp_event_handler_instance_register(BUTTON_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    if (HandleTaskButtonScan == NULL)
        xTaskCreate(&TaskButtonScan, "button scan", 2048, NULL, 1, &HandleTaskButtonScan);
}