/**
 * Copyright (C) 2020, ControlThings Oy Ab
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * @license Apache-2.0
 */
/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdint.h>
#include <string.h>
#include "wifi_utils.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "wifi-utils";

#if 0
#if 1
    wifi_config_t wifi_config = {
        .sta =
        {
#if 1
            .ssid = "Buffalo-G-12DA",
            .password = "19025995",
#else
            .ssid = "TP-LINK_FD2C",
            .password = "8B8sAi3Cm",
#endif
        },
    };
#endif
#endif

void user_set_station_config(char *ssid_in, char *password_in) {
    wifi_config_t new_wifi_config;
    memset(&new_wifi_config, 0, sizeof (wifi_config_t));
    
    memcpy(new_wifi_config.sta.ssid, ssid_in, strlen(ssid_in));
    memcpy(new_wifi_config.sta.password, password_in, strlen(password_in));

    ESP_LOGI(TAG, "There is no saved config!");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &new_wifi_config));

}

void user_set_ap_config(char *ssid_in, char *password_in) {
    wifi_config_t ap_config = {
        .ap = {
            .authmode = WIFI_AUTH_OPEN,
            .channel = 3,
            .max_connection = 4,
        },
    };
    memcpy(ap_config.ap.ssid, ssid_in, 32);
    if (password_in) {
        memcpy(ap_config.ap.password, password_in, 64);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_LOGI(TAG, "Will move to AP mode, ssid %s password %s", ap_config.ap.ssid, ap_config.ap.password); 
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config) );
}