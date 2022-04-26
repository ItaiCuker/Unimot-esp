#ifndef MQTT_H
#define MQTT_H

#include "defines.h"

#include <iotc.h>
#include <iotc_jwt.h>

#include "lwip/apps/sntp.h"
#include "lwip/err.h"
#include "lwip/sys.h"

//device path in Google cloud IoT
#define DEVICE_PATH "projects/%s/locations/%s/registries/%s/devices/%s"
//command subscription path
#define SUBSCRIBE_TOPIC_COMMAND "/devices/%s/commands/#"
//publish path
#define PUBLISH_TOPIC "/devices/%s/events"

extern void publish_data();
void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state);
void mqtt_task(void *pvParameters);
#endif