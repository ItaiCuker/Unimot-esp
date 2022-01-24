/**
 * @file provisioning.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2022-00-00
 * 
 * @copyright Copyright (c) 2022
 * 
 */



/* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble, //using ble to provision device
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM //I don't need to use bluetooth in main app after prov
    };