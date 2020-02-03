#pragma once

/**
 * @file port_log.h
 * @author Jan Nyman
 * @brief Definition of port layer debug console print-out macros, using the ESP32 log definitions
 *
 * @note These macro definitions only work with GCC, because of the fact that PORT_LOGERR(tag, msg) can be called, omitting the third argument!
 */



#include "esp_log.h"
#include "esp_err.h"

/** Log definition for error print-outs */
#define PORT_LOGERR(tag, msg, ...) ESP_LOGE(tag, msg, ##__VA_ARGS__)

/** Log definition for informational print-outs */
#define PORT_LOGINFO(tag, msg, ...) ESP_LOGI(tag, msg, ##__VA_ARGS__)

/** Log definition for warninig print-outs */
#define PORT_LOGWARN(tag, msg, ...) ESP_LOGW(tag, msg, ##__VA_ARGS__)

#define PORT_ABORT() ESP_ERROR_CHECK(ESP_FAIL)