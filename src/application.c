#include <twr.h>

static struct
{
    twr_led_t led;
    twr_gfx_t *gfx;
} app;

void pulse_counter_event_handler(twr_module_sensor_channel_t channel, twr_pulse_counter_event_t event, void *event_param);

/*

A - P4/A4/DAC0,
B - P5/A5/DAC1
C - P7.

PA4
PA5
PA6 - TIM3 CH1

Works properly from 0 - 50mA

Over 50mA the DC/DC switches faster in chunks and device is not able
to track signal so fast.

*/


void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DEBUG, TWR_LOG_TIMESTAMP_ABS);

	twr_led_init(&app.led, TWR_GPIO_LED, false, false);

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

    // Remap TI1 to PA6
    TIM3->OR = TIM3_OR_TI1_RMP;

    // Enable counter
    TIM3->CR1 |= TIM_CR1_CEN;

    twr_led_pulse(&app.led, 2000);

    twr_system_pll_enable();

    twr_module_lcd_init();
    app.gfx = twr_module_lcd_get_gfx();
}

void application_task(void *param)
{
	(void) param;

    uint32_t pulses = TIM3->CNT;
    TIM3->CNT = 0;

    float current_ua = 0.327955f * (float)pulses - 20.0f; // 69.4696;

    float current_amps = current_ua / 1e6f;

    if (current_amps < 0.0f)
    {
        current_amps = 0.0f;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "@pulses: %ld, current(A): %.6f\r\n", pulses, current_amps);
    twr_uart_write(TWR_UART_UART2, buf, strlen(buf));

    if(twr_gfx_display_is_ready(app.gfx))
    {
        twr_gfx_clear(app.gfx);
        twr_gfx_set_font(app.gfx, &twr_font_ubuntu_24);

        char str[32];

        snprintf(str, sizeof(str), "%.3f mA", current_amps * 1000.f);
        twr_gfx_draw_string(app.gfx, 5, 5, str, true);

        snprintf(str, sizeof(str), "%ld pps", pulses);
        twr_gfx_draw_string(app.gfx, 5, 35, str, true);

        twr_gfx_draw_string(app.gfx, 5, 85, "Input ch C", true);

        twr_module_lcd_update();
    }

    twr_scheduler_plan_current_relative(1000);
}
