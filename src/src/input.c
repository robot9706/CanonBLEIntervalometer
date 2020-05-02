#include "input.h"

#include "esp_log.h"
#include "esp_gatt_defs.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "config.h"
#include "main.h"

#define BUTTON_TIME_MIN 30000
#define BUTTON_TIME_MAX 780000

static xQueueHandle gpio_evt_queue = NULL;

static bool input_states[3] = {true};
static uint64_t buttonHILO;

// How to rotary encoder?? help
static uint64_t rotary1HILO;
static uint64_t rotary2HILO;

static uint64_t rotary1LOHI;
static uint64_t rotary2LOHI;

static void check_rotary()
{
	if (rotary2HILO < rotary1HILO && rotary2HILO < rotary2LOHI && rotary2HILO < rotary1LOHI &&
		rotary1HILO < rotary2LOHI && rotary1HILO < rotary1LOHI &&
		rotary2LOHI < rotary1LOHI)
	{
		main_input(INPUT_RIGHT);
	}
	else if (rotary1HILO < rotary2HILO && rotary1HILO < rotary1LOHI && rotary1HILO < rotary2LOHI &&
		rotary2HILO < rotary1LOHI && rotary2HILO < rotary2LOHI &&
		rotary1LOHI < rotary2LOHI)
	{
		main_input(INPUT_LEFT);
	}
}

static void gpio_task(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            int input = 0;
            switch (io_num)
            {
            case BUTTON:
                input = 0;
                break;
            case ROTARY1:
                input = 1;
                break;
            case ROTARY2:
                input = 2;
                break;
            }

            bool pin_state = gpio_get_level(io_num);
            if (input_states[input] != pin_state) // Pin changed?
            {
                switch (io_num)
                {
                case BUTTON:
                    if (!pin_state && input_states[input]) //HI->LO
                    {
                        buttonHILO = esp_timer_get_time();
                    }
                    else if (pin_state && !input_states[input]) //LO->HI
                    {
                        uint64_t dif = esp_timer_get_time() - buttonHILO;
                        if (dif >= BUTTON_TIME_MIN && dif <= BUTTON_TIME_MAX)
                        {
                            main_input(INPUT_BUTTON);
                        }
                    }
                    break;
                case ROTARY1:
                    if (!pin_state && input_states[input]) //HI->LO
                    {
                        rotary1HILO = esp_timer_get_time();
                    }
                    else if (pin_state && !input_states[input]) //LO->HI
                    {
                        rotary1LOHI = esp_timer_get_time();
                    }

                    check_rotary();
                    break;
                case ROTARY2:
                    if (!pin_state && input_states[input]) //HI->LO
                    {
                        rotary2HILO = esp_timer_get_time();
                    }
                    else if (pin_state && !input_states[input]) //LO->HI
                    {
                        rotary2LOHI = esp_timer_get_time();
                    }

                    check_rotary();
                    break;
                }

                input_states[input] = pin_state;
            }
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void input_init()
{
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = ((uint64_t)1 << BUTTON) | ((uint64_t)1 << ROTARY1) | ((uint64_t)1 << ROTARY2);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_set_intr_type(BUTTON, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ROTARY1, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ROTARY2, GPIO_INTR_ANYEDGE);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON, gpio_isr_handler, (void *)BUTTON);
    gpio_isr_handler_add(ROTARY1, gpio_isr_handler, (void *)ROTARY1);
    gpio_isr_handler_add(ROTARY2, gpio_isr_handler, (void *)ROTARY2);
}
