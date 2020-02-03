#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "button_gpio.h"
#include "reent.h" /* _gettimeofday_r */
#include "wifi_control.h"
#include "time_helper.h"
#include "esp_log.h"
#include "led_gpio.h"


static enum blink_state blink_state = BLINK_WAITING;
static enum led_active_state led_active_state;

void led_gpio_set_state(enum blink_state new_state) {
    blink_state = new_state;
}

static int blink_gpio = 0;

static void set_led(bool on) {
    if (led_active_state == LED_ON_WHEN_PIN_LOW) {
        gpio_set_level(blink_gpio, on?0:1);
    }
    else {
        gpio_set_level(blink_gpio, on?1:0);
    }
}

static void led_gpio_task(void *pvParameter)
{
    /* Configure the IOMUX register for pad BLINK_GPIO (some pads are
       muxed to GPIO on reset already, but some default to other
       functions and need to be switched to GPIO. Consult the
       Technical Reference for a list of pads and their default
       functions.)
    */
    gpio_pad_select_gpio(blink_gpio);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(blink_gpio, GPIO_MODE_OUTPUT);
    bool normal_state = false;
    while(1) {
        int delay = 0;
        switch (blink_state) {
            case BLINK_COMMISSIONING:
                 /* Blinking (slow) */
                delay = 1333;
                set_led(false);
                vTaskDelay(delay / portTICK_PERIOD_MS);
                set_led(true);
                 vTaskDelay(delay / portTICK_PERIOD_MS);
                break;
            case BLINK_WAITING:
                /* Blinking (single blip, long pause) */
                delay = 2000;
                set_led(false);
                vTaskDelay(delay / portTICK_PERIOD_MS);
                
                if (blink_state != BLINK_WAITING) { /* Check again, because we might have changed state during the task delay! */
                    break;
                }
                for (int i = 0; i < 1; i++) {
                    set_led(true);
                    delay = 100;
                    vTaskDelay(delay / portTICK_PERIOD_MS);
                    set_led(false);
                    vTaskDelay(delay / portTICK_PERIOD_MS);
                }
                break;
            case BLINK_JOINING:
                delay = 100;
                /* Blinking fast */
                set_led(false);
                vTaskDelay(delay / portTICK_PERIOD_MS);
                set_led(true);
                vTaskDelay(delay / portTICK_PERIOD_MS);
                break;
            case BLINK_NETWORK_OK :
                delay = 0;
                /* constant ON */
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                set_led(true);
                break;
            case BLINK_DARK:
                delay = 0;
                /* constant OFF */
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                set_led(false);
                break;
            case BLINK_STANDALONE_AP:
                /* Blinking (double blip, long pause) */
                delay = 2000;
                normal_state = wifi_ap_get_num_stations_connected() == 0?true:false;

                /* Blink off (output low) */
                set_led(!normal_state);
                vTaskDelay(delay / portTICK_PERIOD_MS);
                /* Blink on (output high) */
                for (int i = 0; i < 2; i++) {
                    set_led(normal_state);
                    delay = 100;
                    vTaskDelay(delay / portTICK_PERIOD_MS);
                    set_led(!normal_state);
                    vTaskDelay(delay / portTICK_PERIOD_MS);
                }
                
                break;
        }

  
    }
}

void led_gpio_start_task(int gpio_num,  enum led_active_state active_state, int task_priority) {
    blink_gpio = gpio_num;
    led_active_state = active_state;

    xTaskCreate(&led_gpio_task, "blink_task", 512, NULL, task_priority, NULL);
}