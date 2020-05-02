#ifndef __TIMER__
#define __TIMER__

typedef void (*timer_callback_ptr)();

void app_timer_init();
void app_timer_start(timer_callback_ptr cb);
void app_timer_stop();

#endif