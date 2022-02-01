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

#include "lwip/apps/sntp.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <iotc.h>
#include <iotc_jwt.h>

//event handlers declaration
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state);

//Log tag
static const char *TAG = "Unimot";

//Status LED
#define STATUS_GPIO 02  //GPIO number
static TaskHandle_t xHandleTaskStartupLED = NULL;   //handle for task

/* Wi-Fi events */
const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;

static int retries = 0;  //how many times to retry to connect to AP

static bool isProvisioned = false;  //is device provisioned (does it have AP credentials stored in NVS?).

//private key location in program
extern const uint8_t ec_pv_key_start[] asm("_binary_private_key_pem_start");
extern const uint8_t ec_pv_key_end[] asm("_binary_private_key_pem_end");

//for silencing compiler warnings
#define IOTC_UNUSED(x) void(x)

//device path in Google cloud IoT
#define DEVICE_PATH "projects/%s/locations/%s/registries/%s/devices/%s"
//command subscription path
#define SUBSCRIBE_TOPIC_COMMAND "/devices/%s/commands/#"

//strings for subscription paths
char *subscribe_topic_command, *subscribe_topic_config;

//connection context for iotc
iotc_context_handle_t iotc_context = IOTC_INVALID_CONTEXT_HANDLE;


/**
 * @brief initalizing SNTP - Simple Network Time Protocol to synchronize ESP32 time with google's.
 * 
 */
static void init_sntp(void)
{
    ESP_LOGI(TAG, "Initializing sntp");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.google.com");
    sntp_init();
}

/**
 * @brief waiting for time to be set.
 * 
 */
static void obtain_time(void)
{
    init_sntp();
    //wait for time to be set.
    time_t now = 0;
    struct tm timeinfo = {0};
    while (timeinfo.tm_year < (2022 - 1900))
    {
        vTaskDelay(2000 / portTICK_PERIOD_MS);  //wait 2 seconds
        ESP_LOGI(TAG, "Waiting for system time to be set...");
        time(&now); //get time
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI(TAG, "Time is set...");
}

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
        CONFIG_UNIMOT_POP,                    //proof of possesion so only users of Unimot can provision device.
        CONFIG_GIOT_DEVICE_ID,            //name of device
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

/**
 * @brief generates a JWT (jason web token) to connect to the cloud
 * 
 * @param dst_jwt_buf  pointer to destination of JWT
 * @param dst_jwt_buf_len length of destination
 * @return iotc_state_t state of JWT generation
 */
iotc_state_t generate_jwt(char* dst_jwt_buf, size_t dst_jwt_buf_len) {
    /* Format the key type descriptors so the client understands
     which type of key is being represented. In this case, a PEM encoded
     byte array of a ES256 key. */
    iotc_crypto_key_data_t iotc_connect_private_key_data;
    iotc_connect_private_key_data.crypto_key_signature_algorithm = IOTC_CRYPTO_KEY_SIGNATURE_ALGORITHM_ES256;
    iotc_connect_private_key_data.crypto_key_union_type = IOTC_CRYPTO_KEY_UNION_TYPE_PEM;
    iotc_connect_private_key_data.crypto_key_union.key_pem.key = (char *) ec_pv_key_start;

    ESP_LOGI(TAG, "pv_key=\n%s\n", iotc_connect_private_key_data.crypto_key_union.key_pem.key);

    /* Generate the client authentication JWT, which will serve as the MQTT
    * password. */
    size_t bytes_written = 0;
    iotc_state_t state = iotc_create_iotcore_jwt(
            CONFIG_GIOT_PROJECT_ID, //my GCP project
            /*jwt_expiration_period_sec=*/3600,
            &iotc_connect_private_key_data,
            dst_jwt_buf,dst_jwt_buf_len, &bytes_written);
    return state;
}

/**
 * @brief starting mqtt connection.
 * 
 */
static void mqtt_task(void *pvParameters)
{
    /* initialize iotc library and create a context to use to connect to the
    * GCP IoT Core Service. */
    const iotc_state_t error_init = iotc_initialize();
    if (error_init != IOTC_STATE_OK)
    {
        ESP_LOGE(TAG, " iotc failed to initialize, error: %d", error_init);
        vTaskDelete(NULL);
    }

    /*  Create a connection context. A context represents a Connection
        on a single socket, and can be used to publish and subscribe
        to numerous topics. */
    iotc_context = iotc_create_context();
    if (IOTC_INVALID_CONTEXT_HANDLE >= iotc_context) {
        ESP_LOGE(TAG, " iotc failed to create context, error: %d", -iotc_context);
        vTaskDelete(NULL);
    }

    /* Generate the client authentication JWT, which will serve as the MQTT
     * password. */
    char jwt[IOTC_JWT_SIZE] = {0};
    iotc_state_t state = generate_jwt(jwt, IOTC_JWT_SIZE);
    
    ESP_LOGI(TAG, "jwt =\n%s\n", jwt);

    if (IOTC_STATE_OK != state) {
        ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
        vTaskDelete(NULL);
    }

    char *device_path = NULL;
    asprintf(&device_path, DEVICE_PATH, CONFIG_GIOT_PROJECT_ID, CONFIG_GIOT_LOCATION, CONFIG_GIOT_REGISTRY_ID, CONFIG_GIOT_DEVICE_ID);
    ESP_LOGI(TAG, "device_path= \n%s\n", device_path);
    iotc_connect(iotc_context, 
                NULL, //username, not used
                jwt, //auth token formated as jwt
                device_path, //device path in GCP project
                150, //wait 2.5 minutes to connect
                600, //every 10 minutes sends packet so won't disconnect
                &on_connection_state_changed);  //event handle for iotc

    free(device_path);
    /* The iotc Client is designed for single threaded devices, 
       by initating iotc_events_process_blocking() 
       we are blocking this task so it will be used as iotc thread*/
    iotc_events_process_blocking();

    iotc_delete_context(iotc_context);

    iotc_shutdown();

    vTaskDelete(NULL);
}

/**
 * @brief event handler of Google cloud IoT core connection status.
 */
void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state)
{
    iotc_connection_data_t *conn_data = (iotc_connection_data_t *)data;

    switch (conn_data->connection_state) 
    {
        /* IOTC_CONNECTION_STATE_OPENED means that the connection has been
       established and the IoTC Client is ready to send/recv messages */
    case IOTC_CONNECTION_STATE_OPENED:
        ESP_LOGI(TAG, "connected to cloud!");

        /* Publish immediately upon connect. 'publish_function' is defined
           in this example file and invokes the IoTC API to publish a
           message. */

        // asprintf(&subscribe_topic_command, SUBSCRIBE_TOPIC_COMMAND, CONFIG_GIOT_DEVICE_ID);
        // ESP_LOGI(TAG, "subscribing to topic: \"%s\"", subscribe_topic_command);
        // iotc_subscribe(in_context_handle, subscribe_topic_command, IOTC_MQTT_QOS_AT_LEAST_ONCE,
        //                &iotc_mqttlogic_subscribe_callback, /*user_data=*/NULL);
        break;

        /* IOTC_CONNECTION_STATE_OPEN_FAILED is set when there was a problem
       when establishing a connection to the server. The reason for the error
       is contained in the 'state' variable. Here we log the error state and
       exit out of the application. */

    /* Publish immediately upon connect. 'publish_function' is defined
       in this example file and invokes the IoTC API to publish a
       message. */
    case IOTC_CONNECTION_STATE_OPEN_FAILED:
        ESP_LOGI(TAG, "ERROR! Connection has failed reason %d", state);

        /* exit it out of the application by stopping the event loop. */
        iotc_events_stop();
        break;

    /* IOTC_CONNECTION_STATE_CLOSED is set when the IoTC Client has been
       disconnected. The disconnection may have been caused by some external
       issue, or user may have requested a disconnection. In order to
       distinguish between those two situation it is advised to check the state
       variable value. If the state == IOTC_STATE_OK then the application has
       requested a disconnection via 'iotc_shutdown_connection'. If the state !=
       IOTC_STATE_OK then the connection has been closed from one side. */
    case IOTC_CONNECTION_STATE_CLOSED:
        free(subscribe_topic_command);
        free(subscribe_topic_config);
        /* When the connection is closed it's better to cancel some of previously
           registered activities. Using cancel function on handler will remove the
           handler from the timed queue which prevents the registered handle to be
           called when there is no connection. */
        

        if (state == IOTC_STATE_OK) {
            /* The connection has been closed intentionally. Therefore, stop
               the event processing loop as there's nothing left to do
               in this example. */
            iotc_events_stop();
        } else {
            ESP_LOGE(TAG, "connection closed - reason %d!", state);
            if (IOTC_STATE_OK != state) {
                ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
                vTaskDelete(NULL);
            }
            /* The disconnection was unforeseen.  Try reconnect to the server
            with previously set configuration, which has been provided
            to this callback in the conn_data structure. */

            char jwt[IOTC_JWT_SIZE] = {0};
            state = generate_jwt(jwt, IOTC_JWT_SIZE);

            iotc_connect(
                in_context_handle, conn_data->username, jwt, conn_data->client_id,
                conn_data->connection_timeout, conn_data->keepalive_timeout,
                &on_connection_state_changed);
        }
        break;

    default:
        ESP_LOGE(TAG, "incorrect connection state value.");
        break;
    }
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
                    ESP_LOGE(TAG, "Failed to connect with provisioned AP, reseting provisioning");
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
    //if event is got IP (device is connected to AP)
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        retries = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } 
    //if event is wifi station started than connect to wifi AP
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi Station started.");
        if (isProvisioned)
        {
            wifi_prov_mgr_deinit();
            esp_wifi_connect();
        }
    }
    //if event is device disconnected from AP but already provisioned
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED && isProvisioned) 
    {
        if (retries < 5) //trying to reconnect 5 times
         {
            esp_wifi_connect();
            retries++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else //couldn't reconnect. TODO: maybe restart provisioning.
        {
            wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
            switch(disconnected->reason)
            {
                case WIFI_REASON_MIC_FAILURE:
                    ESP_LOGE(TAG, "password invalid");
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGE(TAG, "AP not found");
                    break;
            }
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

    isProvisioned = true;   //we are connected to WiFi now

    //stopping startup LED sequence
    if( xHandleTaskStartupLED != NULL)
    {
        vTaskDelete(xHandleTaskStartupLED);
    }
    gpio_set_level(STATUS_GPIO, 0);

    //logging time since program start
    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "connected to Wifi in: %lld microseconds", time_since_boot);
    
    ESP_LOGI(TAG, "starting connection to cloud");
    
    obtain_time();  //waiting to get accurate time

    //creating mqtt task with privlige 1
    xTaskCreate(&mqtt_task, "mqtt_task", 8192, NULL, 1, NULL);
}