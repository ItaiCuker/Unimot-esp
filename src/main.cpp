#include "main.h"

/* Wi-Fi events */
const int WIFI_CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

int retries = 0;  //how many times to retry to connect to AP

bool isProvisioned = false;            //is remote provisioned (does it have AP credentials stored in NVS?).
bool wasConnected = false;          //was remote connected to wifi
bool isConnectedWiFi = false;          //is remote currently connected to AP
bool isConnectedIotc = false;          //is remote connected to

//global json doc
DynamicJsonDocument doc(4096);

String deviceId;
String commandName;
String commandId;

/* Configuration for the provisioning manager */
wifi_prov_mgr_config_t prov_config = {
    .scheme = wifi_prov_scheme_ble, //using ble to provision remote
    .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT //I don't need to use bluetooth in main app after prov
};

/* Initialize provisioning manager with the
     * configuration parameters set above */
#define init_prov ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config))

/**
 * @brief initalizing SNTP - Simple Network Time Protocol to synchronize ESP32 time with google's.
 * 
 */
void init_sntp(void)
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
void obtain_time(void)
{
    init_sntp();
    //wait for time to be set.
    time_t now = 0;
    struct tm timeinfo = {0};
    while (timeinfo.tm_year < (2022 - 1900))
    {
        vTaskDelay(2000 / TICK);  //wait 2 seconds
        ESP_LOGI(TAG, "Waiting for system time to be set...");
        time(&now); //get time
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI(TAG, "Time is set...");
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
 * @brief function to get a json command and proccess it.
 * 
 */
void digestCommand(char *data)
{
    DeserializationError error = deserializeJson(doc, data);
    ESP_LOGE(TAG, "deserializeJson: %s", error.c_str());

    String message = doc["message"];

    if (message.equals("send")) {
        size_t commandLen = doc["commandLen"];
        JsonArray array = doc["command"];
        uint16_t *command = new uint16_t[array.size()];
        copyArray(array, command, commandLen);
        
        sendCommand(command, commandLen);
    }

    if (message.equals("start")) {
        ESP_LOGI(TAG, "learn start");
        status = STATUS_LEARNING;
        deviceId = doc["deviceId"].as<String>();
        commandName = doc["commandName"].as<String>();
        commandId = doc["commandId"].as<String>();
        startRead();
        doc.clear();
        doc["state"] = "start";
        publish_json(doc);
    }

    if (message.equals("stop")) {
        ESP_LOGI(TAG, "learn stop");
        stopRead();
        doc.clear();
        doc["state"] = "";
        status = STATUS_OK;
        publish_json(doc);
    }

    if (message.equals("test")) {
        ESP_LOGI(TAG, "learn test");
        stopRead();
        testCode();
        doc.clear();
        doc["state"] = "tested";
        publish_json(doc);
    }

    if (message.equals("no")) {
        ESP_LOGI(TAG, "learn no");
        startRead();
        doc.clear();
        doc["state"] = "start";
        publish_json(doc);
    }

    if (message.equals("create")) {
        ESP_LOGI(TAG, "learn create");
        doc.clear();
        doc["deviceId"] = deviceId;
        doc["commandId"] = commandId;
        doc["state"] = "create";
        doc["commandName"] = commandName;
        doc["commandLen"] = rawDataLen;
        JsonArray commandArray = doc.createNestedArray("command");
        for (size_t i = 0; i < rawDataLen; i++)
            commandArray.add(rawData[i]);
        
        publish_json(doc);
    }
}

/** 
 * @brief remote got code
 */
void gotCode() {
    ESP_LOGI(TAG, "got code");
    doc.clear();
    doc["state"] = "gotCode";
    publish_json(doc);
}

/**
 * @brief starting provisioning service.
 * 
 */
void start_prov()
{
    ESP_LOGI(TAG, "start_prov()");
    
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,   //using secured communication
        CONFIG_UNIMOT_POP,                    //proof of possesion so only users of Unimot can provision remote.
        CONFIG_GIOT_DEVICE_ID,            //name of remote
        NULL));                  //using BLE so NULL
}

/**
 * @brief init wifi station
 * 
 */
void init_wifi()
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
                break;
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
                ESP_LOGI(TAG, "Provisioning successful %d", isConnectedWiFi);
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
    //if event is got IP (remote is connected to AP)
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        isConnectedWiFi = true;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

        if (wasConnected)
            xTaskCreate(&reconnect_mqtt, "reconnect task", 1024, NULL, 1, NULL);
        else
        /* Signal application to continue execution */
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } 
    //if event is wifi station started
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi Station started.");
        if (isProvisioned)  //on start-up if remote is already provisioned
        {
            wifi_prov_mgr_deinit();
            esp_wifi_connect();
        }
    }
    //if event is remote disconnected from AP but already provisioned
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        isConnectedWiFi = false;
        wasConnected = true;
        //showing disconnected LED sequence
        status = STATUS_CONNECTING;
        
        if (isProvisioned)
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry to connect to the AP");
            //logging reason
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

    else if (event_base == BUTTON_EVENT && event_id == BUTTON_EVENT_LONG)
    {
        //reseting provisioning
        wifi_prov_mgr_reset_provisioning();
        esp_restart();
    }
    
    else if (event_base == BUTTON_EVENT && event_id == BUTTON_EVENT_SHORT)
    {
    }
}

extern "C" void app_main() 
{
    ESP_LOGI(TAG, "Unimot-esp start");
    
    /* Initialize the default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //inits:
    init_status_led();  //blinking led until startup is finished (ESP connected to AP)
    init_button();      
    initSendCode();     
    initReadCode();
    init_nvs();         
    init_wifi();        
    init_prov;          

    /*checking if remote has been provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&isProvisioned));
    ESP_LOGI(TAG, "provisoned? %s", isProvisioned ? "true" : "false");
    if (isProvisioned) {
        status = STATUS_CONNECTING;     
        start_wifi_sta();
    }    
    else {
        status = STATUS_PROV;
        start_prov();
        ESP_LOGI(TAG, "after start_prov()");
    }

    //waiting until connected
    xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    isProvisioned = true;   //we are connected to WiFi so we are provisioned

    //stopping startup LED sequence if hasn't already
    gpio_set_level(GPIO_STATUS, 0);

    //logging time since program start
    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGI(TAG, "connected to Wifi in: %lld microseconds", time_since_boot);
    
    ESP_LOGI(TAG, "starting connection to cloud");
    
    obtain_time();  //waiting to get accurate time

    //creating mqtt task that runs indefinetly
    xTaskCreate(&mqtt_task, "mqtt_task", 8192, NULL, 1, NULL);
}