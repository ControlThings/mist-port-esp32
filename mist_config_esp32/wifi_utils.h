/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   wifi_utils.h
 * Author: jan
 *
 * Created on January 22, 2017, 7:40 PM
 */

#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H


#ifdef __cplusplus
extern "C" {
#endif
    #define MAX_NUM_SSIDS 10

    char **user_wifi_get_ssids();
    int8_t *user_wifi_get_rssis();
    void user_stop_server(void);
    void user_wifi_set_station_mode();
    void user_set_station_config(char *ssid, char *password);
    void user_set_ap_config(char *ssid_in, char *passwrod_in);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_UTILS_H */

