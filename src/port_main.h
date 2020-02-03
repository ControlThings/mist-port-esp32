#pragma once

#include <stdint.h>
/**
 * Initialize the ESP32 Wish and Mist port.
 * @param default_alias The prefix string from which the identity alias ("user name") will default to.
 */
void mist_port_esp32_init(char* default_alias);

/**
 * Worker function of the Mist esp32 port.
 * This function runs the networking function for Mist, and invokes callbacks into Wish core and Mist at required intervals.
 * 
 * This function can and should be called as often as possible to ensure low network latency. It maintains internal timestamps to regulate the periodic callbacks.
 * 
 * \param max_block_time_ms The maximum time, in milliseconds, to block execution in select() while waiting for socket status changes. A value of 0 disables the blocking, and makes the function to return immediately after examining socket status.
 * 
 * \note A typical max_block_time_ms is 100 ms.
 * 
 * \note if this function is run with block time of 0ms, then user must ensure by some other means that the calling process does not consume all CPU time.
 * 
 */
void mist_port_esp32_periodic(unsigned int max_block_time_ms);