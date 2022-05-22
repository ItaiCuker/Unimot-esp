/**
 * @file mqtt.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2022-02-24
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "mqtt.h"

//strings for subscription paths
char *subscribe_topic_command, *publish_topic;

iotc_connection_data_t *conn_data;

iotc_context_handle_t iotc_context = IOTC_INVALID_CONTEXT_HANDLE;

/**
 * @brief publish json to mqtt topic
 * 
 * @param doc json to publish
 */
void publish_json(DynamicJsonDocument doc)
{
    String publish_message;
    serializeJson(doc,  publish_message);
    ESP_LOGI(TAG, "json: %s", publish_message.c_str());

    iotc_publish(iotc_context, publish_topic, publish_message.c_str(), IOTC_MQTT_QOS_AT_LEAST_ONCE, NULL, NULL);
}

/**
 * @brief callback when a command is sent to remote
 * 
 */
void iotc_mqttlogic_subscribe_callback(iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type,const iotc_sub_call_params_t *const params, iotc_state_t state,void *user_data)
{
    (void)(in_context_handle);
    (void)(call_type);
    (void)(state);
    (void)(user_data);
    if (params != NULL && params->message.topic != NULL) {
        ESP_LOGI(TAG, "Subscription Topic: %s", params->message.topic);
        char *sub_message = (char *)malloc(params->message.temporary_payload_data_length + 1);
        if (sub_message == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory");
            return;
        }
        memcpy(sub_message, params->message.temporary_payload_data, params->message.temporary_payload_data_length);
        sub_message[params->message.temporary_payload_data_length] = '\0';
        ESP_LOGI(TAG, "Message Payload: %s\n %s == %s", sub_message, subscribe_topic_command, params->message.topic);
        //if command is for this remote
        if (strcmp(subscribe_topic_command, params->message.topic)) {  
            //digest command
            digestCommand(sub_message);
        }
        free(sub_message);
    }
}

/**
 * @brief generates a JWT (Jason Web Token)
 * 
 * @param dst_jwt_buf  pointer to destination of JWT
 * @param dst_jwt_buf_len length of destination
 * @return iotc_state_t state of JWT generation
 */
iotc_state_t generate_jwt(char* dst_jwt_buf, size_t dst_jwt_buf_len, uint32_t exp) {
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
            /*jwt_expiration_period_sec=*/exp,
            &iotc_connect_private_key_data,
            dst_jwt_buf,dst_jwt_buf_len, &bytes_written);
    return state;
}

/**
 * @brief starting mqtt connection.
 * 
 */
void mqtt_task(void *pvParameters)
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
    iotc_state_t state = generate_jwt(jwt, IOTC_JWT_SIZE, 86400);   //expiration time 24 hours

    if (IOTC_STATE_OK != state) {
        ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
        vTaskDelete(NULL);
    }

    //publish topic init
    asprintf(&publish_topic, PUBLISH_TOPIC, CONFIG_GIOT_DEVICE_ID);

    char *device_path = NULL;
    asprintf(&device_path, DEVICE_PATH, CONFIG_GIOT_PROJECT_ID, CONFIG_GIOT_LOCATION, CONFIG_GIOT_REGISTRY_ID, CONFIG_GIOT_DEVICE_ID);
    ESP_LOGI(TAG, "device_path= \n%s\n", device_path);
    iotc_connect(iotc_context, 
                NULL, //username, not used
                jwt, //auth token formated as jwt
                device_path, //device path in GCP project
                150, //connection timeout 2.5 minutes
                600, //keepalive packet every 10 minutes
                &on_connection_state_changed);  //event handler for iotc

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
    conn_data = (iotc_connection_data_t *)data;

    switch (conn_data->connection_state) 
    {
    case IOTC_CONNECTION_STATE_OPENED:
        ESP_LOGI(TAG, "connected to cloud!");

        //publish upon connect
        status = STATUS_OK;
        doc.clear();
        doc["state"] = "";
        publish_json(doc);

        asprintf(&subscribe_topic_command, SUBSCRIBE_TOPIC_COMMAND, CONFIG_GIOT_DEVICE_ID);
        asprintf(&publish_topic, PUBLISH_TOPIC, CONFIG_GIOT_DEVICE_ID);
        ESP_LOGI(TAG, "subscribing to topic: \"%s\"", subscribe_topic_command);
        iotc_subscribe(in_context_handle, subscribe_topic_command, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                       &iotc_mqttlogic_subscribe_callback, /*user_data=*/NULL);
        break;

    case IOTC_CONNECTION_STATE_OPEN_FAILED:
        ESP_LOGI(TAG, "ERROR! Connection has failed reason %d", state);
        // try reconnect
        reconnect_mqtt(NULL);
        break;

    case IOTC_CONNECTION_STATE_CLOSED:

        free(subscribe_topic_command);
        free(publish_topic);
        
        ESP_LOGE(TAG, "connection closed - reason %d!", state);

        //retry connect if connected to AP
        if (status == STATUS_OK || !wasConnected)
        {
            reconnect_mqtt(NULL);
        }
        break;

    default:
        ESP_LOGE(TAG, "incorrect connection state value.");
        break;
    }
}

/**
 * @brief reconnect to mqtt
 * in 1 case this is used as task
 * @param pvParameters unused
 */
void reconnect_mqtt(void *pvParameters) {
    char jwt[IOTC_JWT_SIZE] = {0};
    generate_jwt(jwt, IOTC_JWT_SIZE, 30);

    iotc_connect(
        iotc_context, conn_data->username, jwt, conn_data->client_id,
        conn_data->connection_timeout, conn_data->keepalive_timeout,
        &on_connection_state_changed);
    return;
}