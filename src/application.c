#include <application.h>

static struct
{
    twr_led_t led;
} app;

void pulse_counter_event_handler(twr_module_sensor_channel_t channel, twr_pulse_counter_event_t event, void *event_param);

/*

A - P4/A4/DAC0,
B - P5/A5/DAC1
C - P7.

PA4
PA5
PA6 - TIM3 CH1

*/


void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DEBUG, TWR_LOG_TIMESTAMP_ABS);

	twr_led_init(&app.led, TWR_GPIO_LED, false, false);

	twr_pulse_counter_init(TWR_MODULE_SENSOR_CHANNEL_C, TWR_PULSE_COUNTER_EDGE_FALL);
	//twr_pulse_counter_set_event_handler(TWR_MODULE_SENSOR_CHANNEL_A, pulse_counter_event_handler, NULL);

    //twr_module_sensor_set_mode(TWR_MODULE_SENSOR_CHANNEL_B, TWR_MODULE_SENSOR_MODE_INPUT);
    //twr_module_sensor_set_pull(TWR_MODULE_SENSOR_CHANNEL_B, TWR_MODULE_SENSOR_PULL_NONE);

/*
    // Cannot get it to work properly in counter mode

    // Sensor channel C to TIM3_CH1
    twr_gpio_set_mode(TWR_GPIO_P7, TWR_GPIO_MODE_ALTERNATE_2);

    // Enable TIM3 clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // Errata workaround
    RCC->APB1ENR;

    // Disable counter if it is running
    TIM3->CR1 &= ~TIM_CR1_CEN;

    TIM3->ARR = 65535;

    // Map IC1 to TI1
    TIM3->CCMR1 = TIM_CCMR1_CC1S_0;
    // Set external clock mode to SMS, TS trigger selection TI1 edge
    TIM3->SMCR = TIM_SMCR_SMS_Msk | (TIM_SMCR_TS_2 | TIM_SMCR_TS_0);

    // Enable counter
    TIM3->CR1 |= TIM_CR1_CEN;
*/
    twr_led_pulse(&app.led, 2000);
}

void application_task(void *param)
{
	(void) param;

    uint32_t pulses = twr_pulse_counter_get(TWR_MODULE_SENSOR_CHANNEL_C);
    twr_pulse_counter_reset(TWR_MODULE_SENSOR_CHANNEL_C);

    float current_ua = 0.327955f * (float)pulses - 20.0f; // 69.4696;

    float current_amps = current_ua / 1e6f;

    twr_log_debug("pulses: %ld, current(A): %.6f", pulses, current_amps);

/*
    uint32_t channel_count_a = TIM3->CNT;
    uint32_t channel_count_b = TIM3->CCR1;

    twr_log_debug("pulses: %d, %d", channel_count_a, channel_count_b);
*/
    twr_scheduler_plan_current_relative(1000);
}
