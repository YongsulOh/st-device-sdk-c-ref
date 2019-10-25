/* ***************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
//#include "platform_stdlib.h"
#include "device_control.h"
#include "PinNames.h"
#include <gpio_api.h>
#include "gpio_irq_api.h"
#include "gpio_irq_ex_api.h"
#include "timer_api.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

gpio_t gpio_ctrl_noti;
gpio_t gpio_ctrl_zero;
gpio_t gpio_ctrl_r;
gpio_t gpio_ctrl_g;
gpio_t gpio_ctrl_b;
gpio_t gpio_ctrl_button;

gtimer_t level_timer;
static xQueueHandle button_event_queue = NULL;

static float calculate_rgb(float v1, float v2, float vh)
{
	if (vh < 0) vh += 1;
	if (vh > 1) vh -= 1;

	if ((6 * vh) < 1)
		return (v1 + (v2 - v1) * 6 * vh);

	if ((2 * vh) < 1)
		return v2;

	if ((3 * vh) < 2)
		return (v1 + (v2 - v1) * ((2.0f / 3) - vh) * 6);

	return v1;
}

/* SmartThings manage color by using Hue-Saturation format,
   If you use LED by using RGB color format, you need to change color format */
void update_rgb_from_hsl(double hue, double saturation, int level,
		int *red, int *green, int *blue)
{
	if (saturation == 0)
	{
		*red = *green = *blue = 255;
		return;
	}

	float v1, v2;
	float h = ((float) hue) / 100;
	float s = ((float) saturation) / 100;
	float l = ((float) level) / 100;

	if (l < 0.5) {
		v2 = l * (1 + s);
	} else {
		v2 = l + s - l * s;
	}

	v1 = 2 * l - v2;

	*red   = (int)(255 * calculate_rgb(v1, v2, h + (1.0f / 3)));
	*green = (int)(255 * calculate_rgb(v1, v2, h));
	*blue  = (int)(255 * calculate_rgb(v1, v2, h - (1.0f / 3)));
}

void button_isr_handler(void *arg)
{
	static uint32_t last_time_ms = 0;
	uint32_t now_ms = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

	/* check debounce time to ignore small ripple of currunt */
	if (now_ms - last_time_ms > BUTTON_DEBOUNCE_TIME_MS) {
		last_time_ms = now_ms;
		xQueueSendFromISR(button_event_queue, &now_ms, NULL);
	}
}

bool get_button_event(int* button_event_type, int* button_event_count)
{
	static uint32_t press_count = 0;
	uint32_t button_time_ms = 0;
	uint32_t now_ms = 0;
	uint32_t gpio_value = 0;

	now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	if (xQueueReceive(button_event_queue, &button_time_ms, 0)) {
		vTaskDelay(BUTTON_DEBOUNCE_TIME_MS / portTICK_PERIOD_MS);
		gpio_value = gpio_read(&gpio_ctrl_button);
		if (gpio_value == BUTTON_GPIO_PRESSED){
			*button_event_type = BUTTON_SHORT_PRESS;
			*button_event_count = 1;
			return true;
		}
	}
	return false;
}

void led_blink(int gpio, int delay, int count)
{
	for (int i = 0; i < count; i++) {
		gpio_write(&gpio_ctrl_noti, 0);
		vTaskDelay(delay / portTICK_PERIOD_MS);
		gpio_write(&gpio_ctrl_noti, 1);
		vTaskDelay(delay / portTICK_PERIOD_MS);
	}
}

void change_led_state(int noti_led_mode)
{
	static uint32_t led_last_time_ms = 0;

	uint32_t now_ms = 0;
	uint32_t gpio_level = 0;

	now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	switch (noti_led_mode)
	{
		case LED_ANIMATION_MODE_IDLE:
			break;
		case LED_ANIMATION_MODE_SLOW:
			//gpio_level =  gpio_get_level(GPIO_OUTPUT_NOTIFICATION_LED);
			if ((gpio_level == NOTIFICATION_LED_GPIO_ON) && (now_ms - led_last_time_ms > 200)) {
				gpio_write(&gpio_ctrl_noti, 0);
				led_last_time_ms = now_ms;
			}
			if ((gpio_level == NOTIFICATION_LED_GPIO_OFF) && (now_ms - led_last_time_ms > 1000)) {
				gpio_write(&gpio_ctrl_noti, 1);
				led_last_time_ms = now_ms;
			}
			break;
		case LED_ANIMATION_MODE_FAST:
			//gpio_level =  gpio_get_level(GPIO_OUTPUT_NOTIFICATION_LED);
			if ((gpio_level == NOTIFICATION_LED_GPIO_ON) && (now_ms - led_last_time_ms > 100)) {
				gpio_write(&gpio_ctrl_noti, 0);
				led_last_time_ms = now_ms;
			}
			if ((gpio_level == NOTIFICATION_LED_GPIO_OFF) && (now_ms - led_last_time_ms > 100)) {
				gpio_write(&gpio_ctrl_noti, 1);
				led_last_time_ms = now_ms;
			}
			break;
		default:
			break;
	}
}

void led_button_init(void)
{
	//notify led init
	gpio_init(&gpio_ctrl_noti, GPIO_OUTPUT_NOTIFICATION_LED);
	gpio_mode(&gpio_ctrl_noti, PullNone);
	gpio_dir(&gpio_ctrl_noti, PIN_OUTPUT);

	//0 init
	gpio_init(&gpio_ctrl_zero, GPIO_OUTPUT_COLORLED_0);
	gpio_mode(&gpio_ctrl_zero, PullDown);
	gpio_dir(&gpio_ctrl_zero, PIN_OUTPUT);
	gpio_write(&gpio_ctrl_zero, 0);

	//red init
	gpio_init(&gpio_ctrl_r, GPIO_OUTPUT_COLORLED_R);
	gpio_mode(&gpio_ctrl_r, PullDown);
	gpio_dir(&gpio_ctrl_r, PIN_OUTPUT);

	//green init
	gpio_init(&gpio_ctrl_g, GPIO_OUTPUT_COLORLED_G);
	gpio_mode(&gpio_ctrl_g, PullDown);
	gpio_dir(&gpio_ctrl_g, PIN_OUTPUT);

	//blue init
	gpio_init(&gpio_ctrl_b, GPIO_OUTPUT_COLORLED_B);
	gpio_mode(&gpio_ctrl_b, PullDown);
	gpio_dir(&gpio_ctrl_b, PIN_OUTPUT);

	//button init
	button_event_queue = xQueueCreate(10, sizeof(uint32_t));
	gpio_irq_init(&gpio_ctrl_button, GPIO_INPUT_BUTTON, button_isr_handler, (uint32_t)(&gpio_ctrl_button));
	gpio_irq_set(&gpio_ctrl_button, IRQ_LOW, 1);   // LOW Trigger
	gpio_irq_enable(&gpio_ctrl_button);

}
