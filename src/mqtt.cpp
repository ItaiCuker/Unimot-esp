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
char *subscribe_topic_command, *subscribe_topic_config;

iotc_context_handle_t iotc_context = IOTC_INVALID_CONTEXT_HANDLE;

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
        if (strcmp("/devices/test_device/commands/", params->message.topic)) {
            int value;
            sscanf(sub_message, "%d", &value);
            ESP_LOGI(TAG, "value: %d", value);
            if (value == 1) {
                ESP_ERROR_CHECK(gpio_set_level(GPIO_STATUS, true));
            } else if (value == 0) {
                ESP_ERROR_CHECK(gpio_set_level(GPIO_STATUS, false));
            }
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

        asprintf(&subscribe_topic_command, SUBSCRIBE_TOPIC_COMMAND, CONFIG_GIOT_DEVICE_ID);
        ESP_LOGI(TAG, "subscribing to topic: \"%s\"", subscribe_topic_command);
        iotc_subscribe(in_context_handle, subscribe_topic_command, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                       &iotc_mqttlogic_subscribe_callback, /*user_data=*/NULL);
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
            state = generate_jwt(jwt, IOTC_JWT_SIZE, 30);

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