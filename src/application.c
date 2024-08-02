#include <twr.h>
#include <twr_servo.h>

#define FIRMWARE "at-dcdc-tester"

static struct
{
    twr_led_t led;
    twr_gfx_t *gfx;
} app;

#define GPIO_BUZZER TWR_GPIO_P13
#define GPIO_LOAD TWR_GPIO_P12

#define GPIO_GNSS_VCC TWR_GPIO_P9
#define GPIO_GNSS_BCKP TWR_GPIO_P8

#define GPIO_SERVO TWR_GPIO_P6

twr_scheduler_task_id_t task_buzzer_off;
twr_scheduler_task_id_t task_load_off;

twr_servo_t servo;

float adc_voltage_v[2];

void pulse_counter_event_handler(twr_module_sensor_channel_t channel, twr_pulse_counter_event_t event, void *event_param);

/*

ADC
P0
P1

Buzzer P13
Load P12

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

static void _adc_event_handler(twr_adc_channel_t channel, twr_adc_event_t event, void *param)
{
    (void) channel;
    (void) param;

    if (event == TWR_ADC_EVENT_DONE)
    {
        float voltage;
        switch(channel)
        {
            case TWR_ADC_CHANNEL_A0:
            twr_adc_async_get_voltage(TWR_ADC_CHANNEL_A0, &adc_voltage_v[0]);
            break;

            case TWR_ADC_CHANNEL_A1:
            twr_adc_async_get_voltage(TWR_ADC_CHANNEL_A1, &adc_voltage_v[1]);
            break;

            default:
            break;
        }
    }
}



bool at_i(void)
{
    twr_atci_printfln("\"" FIRMWARE "-v%u.%u.%u\"",
                      (twr_info_fw_version() >> 24) & 0xff,
                      (twr_info_fw_version() >> 16) & 0xff,
                      (twr_info_fw_version() >> 8) & 0xff);
    return true;
}

bool atci_buzzer(twr_atci_param_t *param)
{
    uint32_t delay;

    if (!twr_atci_get_uint(param, &delay))
    {
        return false;
    }

    twr_gpio_set_output(GPIO_BUZZER, 1);
    twr_scheduler_plan_relative(task_buzzer_off, delay);

    return true;
}

static void buzzer_off_task(void* param) {
    (void) param;
    twr_gpio_set_output(GPIO_BUZZER, 0);
}


bool atci_load(twr_atci_param_t *param)
{
    int output;

    if (!twr_atci_get_uint(param, &output))
    {
        return false;
    }

    twr_gpio_set_output(GPIO_LOAD, (output > 0) ? 1 : 0);

    // If other than 0/1, trigger delayed safety off
    if(output > 1)
    {
        twr_scheduler_plan_relative(task_load_off, output);
    }

    return true;
}


static void load_off_task(void* param) {
    (void) param;
    twr_gpio_set_output(GPIO_LOAD, 0);
}


bool atci_servo(twr_atci_param_t *param)
{
    int angle;

    if (!twr_atci_get_uint(param, &angle))
    {
        return false;
    }

    twr_servo_set_angle(&servo, angle);

    return true;
}

void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DEBUG, TWR_LOG_TIMESTAMP_ABS);

    static twr_atci_command_t commands[] = {
        {"I", at_i, NULL, NULL, NULL, "Request product information"},
        {"$BUZZER", NULL, atci_buzzer, NULL, NULL, "Control buzzer"},
        {"$LOAD", NULL, atci_load, NULL, NULL, "Control load"}, // 0/1 or >1 for duration
        {"$SERVO", NULL, atci_servo, NULL, NULL, "Control servo"},
        TWR_ATCI_COMMAND_CLAC,
        TWR_ATCI_COMMAND_HELP};

    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));
    twr_atci_set_uart_active_callback(NULL, 0); // atci is always active

	twr_led_init(&app.led, TWR_GPIO_LED, false, false);

    // Buzzer
    twr_gpio_init(GPIO_BUZZER);
    twr_gpio_set_mode(GPIO_BUZZER, TWR_GPIO_MODE_OUTPUT);
    // Task pro vypnutí buzzeru
    task_buzzer_off = twr_scheduler_register(buzzer_off_task, NULL, TWR_TICK_INFINITY);

    // Load
    twr_gpio_init(GPIO_LOAD);
    twr_gpio_set_mode(GPIO_LOAD, TWR_GPIO_MODE_OUTPUT);
    // Task pro vypnutí LOAD, pokud by se zapomněl
    task_load_off = twr_scheduler_register(load_off_task, NULL, TWR_TICK_INFINITY);

    // ADC
    twr_adc_init();
    twr_adc_set_event_handler(TWR_ADC_CHANNEL_A0, _adc_event_handler, NULL);
    twr_adc_set_event_handler(TWR_ADC_CHANNEL_A1, _adc_event_handler, NULL);

    // GNDD VCC/BCKUP digital in
    twr_gpio_init(GPIO_GNSS_VCC);
    twr_gpio_set_mode(GPIO_GNSS_VCC, TWR_GPIO_MODE_INPUT);
    twr_gpio_init(GPIO_GNSS_BCKP);
    twr_gpio_set_mode(GPIO_GNSS_BCKP, TWR_GPIO_MODE_INPUT);

    // Servo
    twr_servo_init(&servo, TWR_PWM_P14);
    twr_pwm_enable(TWR_PWM_P14);

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

    //twr_module_lcd_init();
    //app.gfx = twr_module_lcd_get_gfx();
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

    twr_atci_printfln("@pulses: %ld, current(A): %.6f\r\n", pulses, current_amps);
    twr_atci_printfln("@adc (V) P0: %.6f, P1: %.6f\r\n", adc_voltage_v[0], adc_voltage_v[1]);
    twr_atci_printfln("@gpio (V) P8: %d, P9: %d\r\n", twr_gpio_get_input(GPIO_GNSS_BCKP), twr_gpio_get_input(GPIO_GNSS_VCC));

    twr_adc_async_measure(TWR_ADC_CHANNEL_A0);
    twr_adc_async_measure(TWR_ADC_CHANNEL_A1);

    /*if(twr_gfx_display_is_ready(app.gfx))
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
    }*/

    twr_scheduler_plan_current_relative(1000);
}
