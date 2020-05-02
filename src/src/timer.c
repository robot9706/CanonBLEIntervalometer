#include "timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define TAG "TIMER"

static TimerHandle_t timer;
static timer_callback_ptr callback = NULL;

static void timer_callback()
{
    if (callback != NULL)
    {
        callback();
    }
}

void app_timer_init()
{
    timer = xTimerCreate("SEC", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, timer_callback);
}

void app_timer_start(timer_callback_ptr cb)
{
    callback = cb;

    xTimerStart(timer, 0);
}

void app_timer_stop()
{
    xTimerStop(timer, 0);
    callback = NULL;
}