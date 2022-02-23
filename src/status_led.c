#include "status_led.h"

/**
 * @brief initalizing Status led
 * 
 */
void init_status_led()
{
    /* Configure output */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    io_conf.pin_bit_mask = ((uint64_t)1 << STATUS_GPIO);
    /* Configure the GPIO */
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(STATUS_GPIO, false));
    xTaskCreate(&TaskStartupLED, "startupLED", 512, &status, 1, &HandleTaskStartupLED);
}

/**
 * @brief blinking status LED 3 times
 * 
 * @param pvParameters void
 */
void TaskStartupLED(void *arg)
{
    for(;;)
    {
        status_led_t *mStatus = (status_led_t*)arg;
        //led sequence when provisioning
        while(*mStatus == STATUS_PROV)
        {
            for (size_t i = 0; i < 3; i++)
                BLINK;
            vTaskDelay(1000 / TICK);
        }

        //led sequence when provisioned but wifi disconnected
        while (*mStatus == STATUS_WIFI)
            BLINK;

        while (*mStatus == STATUS_OK)
        {
            vTaskDelay(10 / TICK);
        }
    }
}