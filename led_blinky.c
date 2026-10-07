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

#define AUTO_STEP_INTERVAL_MS 60U
#define AUTO_MIN_DUTY         0U
#define AUTO_MAX_DUTY         100U
#define AUTO_HOLD_MS          1500U

#define STATUS_BLINK_AUTO_MS  1500U
#define STATUS_BLINK_PAUSE_MS 375U

typedef enum {
    STATE_MANUAL = 0,
    STATE_AUTO,
    STATE_PAUSE
} app_state_t;

static volatile uint32_t g_ms = 0U;
static volatile uint32_t g_buttonEdgePending = 0U;

static app_state_t s_currentState  = STATE_MANUAL;
static app_state_t s_previousState = STATE_MANUAL;

static uint32_t s_pwmFrequency = 1000U;
static uint8_t  s_manualDuty   = 25U;
static uint8_t  s_appliedDuty  = 25U;

static uint8_t  s_autoDuty       = AUTO_MIN_DUTY;
static bool     s_autoAscending  = true;
static uint32_t s_lastAutoStepMs = 0U;

static uint32_t s_lastStatusLedToggleMs = 0U;
static bool     s_statusLedOn = true;

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
    s_appliedDuty = duty;
    s_pwmFrequency = freq;
}

static void Hardware_PwmInit(void)
{
    ctimer_config_t cfg;
    CTIMER_GetDefaultConfig(&cfg);
    cfg.prescale = 0U;
    CTIMER_Init(APP_PWM_TIMER, &cfg);

    Hardware_PwmApply(s_manualDuty, s_pwmFrequency);
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

static void FSM_TransitionTo(app_state_t nextState)
{
    if (nextState == s_currentState)
    {
        return;
    }

    if (nextState == STATE_PAUSE)
    {
        s_previousState = s_currentState;
        s_currentState  = STATE_PAUSE;
        Hardware_PwmApply(0U, s_pwmFrequency);
    }
    else if (nextState == STATE_AUTO)
    {
        s_currentState = STATE_AUTO;
        s_lastAutoStepMs = g_ms;
        Hardware_PwmApply(s_autoDuty, s_pwmFrequency);
    }
    else if (nextState == STATE_MANUAL)
    {
        s_currentState = STATE_MANUAL;
        Hardware_PwmApply(s_manualDuty, s_pwmFrequency);
        StatusLed_Set(true);
        s_statusLedOn = true;
    }
}

static void FSM_ResumeFromPause(void)
{
    if (s_currentState != STATE_PAUSE)
    {
        return;
    }

    app_state_t target = (s_previousState == STATE_AUTO) ? STATE_AUTO : STATE_MANUAL;
    s_currentState = target;

    if (target == STATE_AUTO)
    {
        s_lastAutoStepMs = g_ms;
        Hardware_PwmApply(s_autoDuty, s_pwmFrequency);
    }
    else
    {
        Hardware_PwmApply(s_manualDuty, s_pwmFrequency);
        StatusLed_Set(true);
        s_statusLedOn = true;
    }
}

static void Task_AutoRamp(uint32_t now)
{
    static uint32_t s_holdStartTime = 0U;
    static bool s_isHolding = false;

    if (s_currentState != STATE_AUTO)
    {
        s_isHolding = false;
        return;
    }

    if (s_isHolding)
    {
        if ((uint32_t)(now - s_holdStartTime) >= AUTO_HOLD_MS)
        {
            s_isHolding = false;
            s_lastAutoStepMs = now;
        }
        return;
    }

    if ((uint32_t)(now - s_lastAutoStepMs) >= AUTO_STEP_INTERVAL_MS)
    {
        s_lastAutoStepMs = now;

        if (s_autoAscending)
        {
            if (s_autoDuty < AUTO_MAX_DUTY)
            {
                s_autoDuty++;
            }
            else
            {
                s_autoAscending = false;
                s_isHolding = true;
                s_holdStartTime = now;
            }
        }
        else
        {
            if (s_autoDuty > AUTO_MIN_DUTY)
            {
                s_autoDuty--;
            }
            else
            {
                s_autoAscending = true;
                s_isHolding = true;
                s_holdStartTime = now;
            }
        }

        Hardware_PwmApply(s_autoDuty, s_pwmFrequency);
    }
}

static void Task_StatusLed(uint32_t now)
{
    if (s_currentState == STATE_MANUAL)
    {
        if (!s_statusLedOn)
        {
            StatusLed_Set(true);
            s_statusLedOn = true;
        }
        return;
    }

    uint32_t interval = (s_currentState == STATE_AUTO) ? STATUS_BLINK_AUTO_MS : STATUS_BLINK_PAUSE_MS;

    if ((uint32_t)(now - s_lastStatusLedToggleMs) >= interval)
    {
        s_lastStatusLedToggleMs = now;
        s_statusLedOn = !s_statusLedOn;
        StatusLed_Set(s_statusLedOn);
    }
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
                    if (s_currentState == STATE_MANUAL)
                    {
                        FSM_TransitionTo(STATE_AUTO);
                        PRINTF("\r\n>>> [BOTON] Pulsacion corta (%u ms) -> Modo AUTO (Rampa triangular 10%% a 90%%)\r\n",
                               (unsigned int)pressDuration);
                    }
                    else if (s_currentState == STATE_AUTO)
                    {
                        FSM_TransitionTo(STATE_MANUAL);
                        PRINTF("\r\n>>> [BOTON] Pulsacion corta (%u ms) -> Modo MANUAL (Duty: %u%%)\r\n",
                               (unsigned int)pressDuration, (unsigned int)s_manualDuty);
                    }
                    else
                    {
                        PRINTF("\r\n>>> [BOTON] Pulsacion corta ignorada en modo PAUSA\r\n");
                    }
                }
            }
        }
    }

    if (s_isPressed && !s_longPressExecuted)
    {
        if ((uint32_t)(now - s_pressStartTime) >= LONG_PRESS_TIME_MS)
        {
            s_longPressExecuted = true;

            if (s_currentState == STATE_PAUSE)
            {
                FSM_ResumeFromPause();
                PRINTF("\r\n>>> [BOTON] Pulsacion larga (>= 1500 ms) -> REANUDADO a modo %s\r\n",
                       (s_currentState == STATE_AUTO) ? "AUTO" : "MANUAL");
            }
            else
            {
                FSM_TransitionTo(STATE_PAUSE);
                PRINTF("\r\n>>> [BOTON] Pulsacion larga (>= 1500 ms) -> Modo PAUSA (PWM apagado)\r\n");
            }
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
    PRINTF(" Modo Inicial: MANUAL | Freq: 1000 Hz | Duty: 25%%\r\n");
    PRINTF("====================================================\r\n");
    PRINTF("Controles por pulsador (SW2 / SW3):\r\n");
    PRINTF(" - Pulsacion corta (< 1.5s): Alterna MANUAL <-> AUTO\r\n");
    PRINTF(" - Pulsacion larga (>= 1.5s): Entra / Sale de PAUSA\r\n\r\n");

    while (1)
    {
        uint32_t now = g_ms;

        Task_StatusLed(now);
        Task_AutoRamp(now);
        ProcessButton(now);
    }
}
