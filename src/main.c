/**
 * @file main.c
 * @author your name (you@domain.com)
 * @brief Main program for Unimot written for esp32
 * @version 0.1
 * @date 2022-00-00
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"

#include "nvs_flash.h"
#include "driver/gpio.h"


#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "main.h"

//Log tag
static const char *TAG = "Unimot";

//Status LED
#define STATUS_GPIO 02
static TaskHandle_t xHandleTaskStartupLED = NULL;

//device name for cloud usage
#define DEVICE_NAME CONFIG_UNIMOT_DEVICE_NAME

//proof of possesion for provisioning
#define POP CONFIG_UNIMOT_POP

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;
static EventGroupHandle_t wifi_event_group;

static int retries = 0;  //how many times to retry to connect to AP

static bool isProvisioned = false;  //is device provisioned (does it have AP credentials stored in NVS?).

/**
 * @brief initalizing Status led
 * 
 */
static void init_status_led()
{
    /* Configure output */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
    };
    io_conf.pin_bit_mask = ((uint64_t)1 << STATUS_GPIO);
    /* Configure the GPIO */
    gpio_config(&io_conf);
    gpio_set_level(STATUS_GPIO, false);
}

/**
 * @brief blinking status LED 3 times
 * 
 * @param pvParameters void
 */
void vTaskStartupLED(void *pvParameters)
{
    const TickType_t delay = 150 / portTICK_PERIOD_MS;
    while(true)
    {
        for (size_t i = 0; i < 3; i++)
        {
            gpio_set_level(STATUS_GPIO, 1);
            vTaskDelay(delay);
            gpio_set_level(STATUS_GPIO, 0);
            vTaskDelay(delay);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief initalizing non volatile storage of program
 * 
 */
void init_nvs()
{
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        //if nvs was corrupted
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

/**
 * @brief initalizing provisioning API
 * 
 */
void init_prov()
{
    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble, //using ble to provision device
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM //I don't need to use bluetooth in main app after prov
    };

    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
}

/**
 * @brief starting provisioning service.
 * 
 */
void start_prov()
{
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,   //using secured communication
        POP,                    //proof of possesion so only users of Unimot can provision device.
        DEVICE_NAME,            //name of device
        NULL));                  //using BLE so NULL
}

/**
 * @brief init wifi station
 * 
 */
void init_wifi_sta()
{
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();

    /* Registering event handler for Wi-Fi, IP and provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

/**
 * @brief start wifi station
 * 
 */
void start_wifi_sta()
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Event handler for catching system events */
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) 
    {

        switch (event_id) 
        {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV:
             {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                retries++;
                if (retries >= 5) {
                    ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    retries = 0;
                }
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                retries = 0;
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    }
    //if event is wifi station started than connect to wifi AP
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if (isProvisioned)
        {
            wifi_prov_mgr_deinit();
            esp_wifi_connect();
        }
    }
    //if event is got IP (device is connected to AP)
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        retries = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } 
    //if event is device disconnected from AP
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (retries < 5)
         {
            esp_wifi_connect();
            retries++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
    }
}

void app_main() 
{
    ESP_LOGI(TAG, "Unimot-esp start");
    init_status_led();      //init status LED
    //blinking led until startup is finished (ESP connected to AP)
    xTaskCreate(&vTaskStartupLED, "startupLED", 512, NULL, 1, &xHandleTaskStartupLED);

    /* Initialize the default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_nvs();         //init storage of program
    init_wifi_sta();    //init wifi station
    init_prov();        //init provisioning API

    /*checking if device has been provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&isProvisioned));
    ESP_LOGI(TAG, "provisoned? %s", isProvisioned ? "true" : "false");
    if (isProvisioned)
        start_wifi_sta();
    else
        start_prov();

    //waiting until connected
    xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    gpio_set_level(STATUS_GPIO, 0);
    if( xHandleTaskStartupLED != NULL )
    {
        vTaskDelete(xHandleTaskStartupLED);
    }
    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "connected to Wifi in: %lld microseconds", time_since_boot);
}