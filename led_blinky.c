#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_ctimer.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "app.h"

#define APP_LED_STATUS_GPIO   GPIO3
#define APP_LED_STATUS_PIN    13U

#define APP_BTN_SW3_GPIO      GPIO0
#define APP_BTN_SW3_PIN       6U
#define APP_BTN_SW3_IRQn      GPIO0_IRQn

#define APP_BTN_SW2_GPIO      GPIO1
#define APP_BTN_SW2_PIN       7U
#define APP_BTN_SW2_IRQn      GPIO1_IRQn

#define APP_TICK_TIMER        CTIMER0
#define APP_TICK_IRQn         CTIMER0_IRQn

#define APP_PWM_TIMER         CTIMER1
#define APP_PWM_PERIOD_CH     kCTIMER_Match_3
#define APP_PWM_OUTPUT_CH     kCTIMER_Match_2

#define DEBOUNCE_TIME_MS      30U
#define LONG_PRESS_TIME_MS    1500U

static volatile uint32_t g_ms = 0U;
static volatile uint32_t g_buttonEdgePending = 0U;

static uint32_t s_pwmFrequency = 1000U;
static uint8_t s_pwmDuty = 25U;

void SysTick_Handler(void)
{
}

void CTimer0_TickCallback(uint32_t flags)
{
    if ((flags & kCTIMER_Match0Flag) != 0U)
    {
        g_ms++;
    }
}

static ctimer_callback_t s_tickCallbacks[] = { CTimer0_TickCallback };

void GPIO0_IRQHandler(void)
{
    uint32_t mask = 1UL << APP_BTN_SW3_PIN;
    if ((GPIO_GpioGetInterruptFlags(APP_BTN_SW3_GPIO) & mask) != 0U)
    {
        GPIO_GpioClearInterruptFlags(APP_BTN_SW3_GPIO, mask);
        g_buttonEdgePending = 1U;
    }
    SDK_ISR_EXIT_BARRIER;
}

void GPIO1_IRQHandler(void)
{
    uint32_t mask = 1UL << APP_BTN_SW2_PIN;
    if ((GPIO_GpioGetInterruptFlags(APP_BTN_SW2_GPIO) & mask) != 0U)
    {
        GPIO_GpioClearInterruptFlags(APP_BTN_SW2_GPIO, mask);
        g_buttonEdgePending = 1U;
    }
    SDK_ISR_EXIT_BARRIER;
}

static inline uint32_t TakeButtonEdge(void)
{
    uint32_t irqState = DisableGlobalIRQ();
    uint32_t pending = g_buttonEdgePending;
    g_buttonEdgePending = 0U;
    EnableGlobalIRQ(irqState);
    return pending;
}

static inline void StatusLed_Set(bool on)
{
    GPIO_PinWrite(APP_LED_STATUS_GPIO, APP_LED_STATUS_PIN, on ? 0U : 1U);
}

static void Hardware_TickInit(void)
{
    ctimer_config_t cfg;
    ctimer_match_config_t match = {0};
    uint32_t timerClockHz = CLOCK_GetCTimerClkFreq(0U);

    CTIMER_GetDefaultConfig(&cfg);
    cfg.prescale = 0U;
    CTIMER_Init(APP_TICK_TIMER, &cfg);

    match.matchValue         = (timerClockHz / 1000U) - 1U;
    match.enableCounterReset = true;
    match.enableCounterStop  = false;
    match.outControl         = kCTIMER_Output_NoAction;
    match.outPinInitState    = false;
    match.enableInterrupt    = true;

    CTIMER_RegisterCallBack(APP_TICK_TIMER, s_tickCallbacks, kCTIMER_SingleCallback);
    CTIMER_SetupMatch(APP_TICK_TIMER, kCTIMER_Match_0, &match);
    EnableIRQ(APP_TICK_IRQn);
    CTIMER_StartTimer(APP_TICK_TIMER);
}

static void Hardware_PwmApply(uint8_t duty, uint32_t freq)
{
    uint32_t timerClockHz = CLOCK_GetCTimerClkFreq(1U);

    CTIMER_SetupPwm(APP_PWM_TIMER, APP_PWM_PERIOD_CH, APP_PWM_OUTPUT_CH,
                    duty, freq, timerClockHz, false);

    if (APP_PWM_TIMER->TC >= APP_PWM_TIMER->MR[APP_PWM_PERIOD_CH])
    {
        APP_PWM_TIMER->TC = 0U;
        APP_PWM_TIMER->PC = 0U;
    }

    CTIMER_StartTimer(APP_PWM_TIMER);
    s_pwmDuty = duty;
    s_pwmFrequency = freq;
}

static void Hardware_PwmInit(void)
{
    ctimer_config_t cfg;
    CTIMER_GetDefaultConfig(&cfg);
    cfg.prescale = 0U;
    CTIMER_Init(APP_PWM_TIMER, &cfg);

    Hardware_PwmApply(25U, 1000U);
}

static void Hardware_ButtonInit(void)
{
    GPIO_GpioClearInterruptFlags(APP_BTN_SW3_GPIO, 1UL << APP_BTN_SW3_PIN);
    GPIO_SetPinInterruptConfig(APP_BTN_SW3_GPIO, APP_BTN_SW3_PIN, kGPIO_InterruptEitherEdge);
    EnableIRQ(APP_BTN_SW3_IRQn);

    GPIO_GpioClearInterruptFlags(APP_BTN_SW2_GPIO, 1UL << APP_BTN_SW2_PIN);
    GPIO_SetPinInterruptConfig(APP_BTN_SW2_GPIO, APP_BTN_SW2_PIN, kGPIO_InterruptEitherEdge);
    EnableIRQ(APP_BTN_SW2_IRQn);
}

static void ProcessButton(uint32_t now)
{
    static uint8_t s_validatedLevel = 1U;
    static uint8_t s_sampleLevel = 1U;
    static uint32_t s_lastLevelChangeTime = 0U;
    static uint32_t s_pressStartTime = 0U;
    static bool s_isPressed = false;
    static bool s_longPressExecuted = false;

    uint32_t sw3 = GPIO_PinRead(APP_BTN_SW3_GPIO, APP_BTN_SW3_PIN);
    uint32_t sw2 = GPIO_PinRead(APP_BTN_SW2_GPIO, APP_BTN_SW2_PIN);
    uint8_t currentPin = (sw3 == 0U || sw2 == 0U) ? 0U : 1U;

    if (currentPin != s_sampleLevel)
    {
        s_sampleLevel = currentPin;
        s_lastLevelChangeTime = now;
    }

    if ((s_validatedLevel != s_sampleLevel) && ((uint32_t)(now - s_lastLevelChangeTime) >= DEBOUNCE_TIME_MS))
    {
        s_validatedLevel = s_sampleLevel;

        if (s_validatedLevel == 0U)
        {
            s_isPressed = true;
            s_pressStartTime = now;
            s_longPressExecuted = false;
        }
        else
        {
            if (s_isPressed)
            {
                uint32_t pressDuration = (uint32_t)(now - s_pressStartTime);
                s_isPressed = false;

                if (!s_longPressExecuted && (pressDuration >= DEBOUNCE_TIME_MS) && (pressDuration < LONG_PRESS_TIME_MS))
                {
                    static const uint8_t duties[] = {25U, 50U, 75U, 100U, 0U};
                    static uint8_t dutyIdx = 0U;
                    dutyIdx = (dutyIdx + 1U) % 5U;
                    uint8_t nextDuty = duties[dutyIdx];

                    Hardware_PwmApply(nextDuty, s_pwmFrequency);
                    PRINTF("\r\n>>> [PWM] Pulsacion corta (%u ms) -> Duty: %u%% (Freq: %u Hz)\r\n",
                           (unsigned int)pressDuration, (unsigned int)nextDuty, (unsigned int)s_pwmFrequency);
                }
            }
        }
    }

    if (s_isPressed && !s_longPressExecuted)
    {
        if ((uint32_t)(now - s_pressStartTime) >= LONG_PRESS_TIME_MS)
        {
            s_longPressExecuted = true;
            uint32_t nextFreq = (s_pwmFrequency == 1000U) ? 2000U : (s_pwmFrequency == 2000U) ? 500U : 1000U;

            Hardware_PwmApply(s_pwmDuty, nextFreq);
            PRINTF("\r\n>>> [PWM] Pulsacion larga (>= 1500 ms) -> Freq: %u Hz (Duty: %u%%)\r\n",
                   (unsigned int)nextFreq, (unsigned int)s_pwmDuty);
        }
    }

    (void)TakeButtonEdge();
}

int main(void)
{
    BOARD_InitHardware();
    Hardware_TickInit();
    Hardware_PwmInit();
    Hardware_ButtonInit();

    StatusLed_Set(true);

    PRINTF("\r\n====================================================\r\n");
    PRINTF(" FRDM-MCXA156 - CONTROLADOR DE ILUMINACION BARE-METAL\r\n");
    PRINTF(" Estudiante: Jorge Alberto Ortiz Nieves | Mat: 20250504\r\n");
    PRINTF(" PWM Hardware CTIMER1: Freq: %u Hz | Duty: %u%%\r\n", (unsigned int)s_pwmFrequency, (unsigned int)s_pwmDuty);
    PRINTF("====================================================\r\n");
    PRINTF("Prueba de PWM por hardware en P3_12 (J1-15):\r\n");
    PRINTF(" - Pulsacion corta: cambia Duty (25%% -> 50%% -> 75%% -> 100%% -> 0%%).\r\n");
    PRINTF(" - Pulsacion larga: cambia Frecuencia (1000 Hz -> 2000 Hz -> 500 Hz).\r\n\r\n");

    uint32_t lastLedToggleMs = 0U;
    bool statusLedState = true;

    while (1)
    {
        uint32_t now = g_ms;

        if ((uint32_t)(now - lastLedToggleMs) >= 500U)
        {
            lastLedToggleMs = now;
            statusLedState = !statusLedState;
            StatusLed_Set(statusLedState);
        }

        ProcessButton(now);
    }
}
