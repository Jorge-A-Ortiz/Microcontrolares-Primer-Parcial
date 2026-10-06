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

#define APP_LED_EXT_GPIO      GPIO3
#define APP_LED_EXT_PIN       12U

#define APP_BTN_SW3_GPIO      GPIO0
#define APP_BTN_SW3_PIN       6U

#define APP_BTN_SW2_GPIO      GPIO1
#define APP_BTN_SW2_PIN       7U

#define APP_TICK_TIMER        CTIMER0
#define APP_TICK_IRQn         CTIMER0_IRQn

static volatile uint32_t g_ms = 0U;

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

static inline void StatusLed_Set(bool on)
{
    GPIO_PinWrite(APP_LED_STATUS_GPIO, APP_LED_STATUS_PIN, on ? 0U : 1U);
}

static inline void ExtLed_Set(bool on)
{
    GPIO_PinWrite(APP_LED_EXT_GPIO, APP_LED_EXT_PIN, on ? 1U : 0U);
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

int main(void)
{
    BOARD_InitHardware();
    Hardware_TickInit();

    StatusLed_Set(true);
    ExtLed_Set(false);

    PRINTF("\r\n====================================================\r\n");
    PRINTF(" FRDM-MCXA156 - CONTROLADOR DE ILUMINACION BARE-METAL\r\n");
    PRINTF(" Estudiante: Jorge Alberto Ortiz Nieves | Mat: 20250504\r\n");
    PRINTF(" Baudrate: 115200 8N1 | Tick: CTIMER0 (1 ms)\r\n");
    PRINTF("====================================================\r\n");
    PRINTF("Prueba de temporizador y GPIO:\r\n");
    PRINTF(" - LED verde conmuta cada 500 ms con base de tiempo CTIMER0.\r\n");
    PRINTF(" - Presione SW2 o SW3 para conmutar el LED externo.\r\n\r\n");

    uint32_t lastLedToggleMs = 0U;
    bool statusLedState = true;
    uint8_t lastBtnState = 1U;
    uint32_t btnCheckMs = 0U;

    while (1)
    {
        uint32_t now = g_ms;

        if ((uint32_t)(now - lastLedToggleMs) >= 500U)
        {
            lastLedToggleMs = now;
            statusLedState = !statusLedState;
            StatusLed_Set(statusLedState);
        }

        if ((uint32_t)(now - btnCheckMs) >= 20U)
        {
            btnCheckMs = now;

            uint32_t sw3 = GPIO_PinRead(APP_BTN_SW3_GPIO, APP_BTN_SW3_PIN);
            uint32_t sw2 = GPIO_PinRead(APP_BTN_SW2_GPIO, APP_BTN_SW2_PIN);
            uint8_t btnPressed = (sw3 == 0U || sw2 == 0U) ? 0U : 1U;

            if (btnPressed != lastBtnState)
            {
                lastBtnState = btnPressed;

                if (btnPressed == 0U)
                {
                    ExtLed_Set(true);
                    PRINTF("[GPIO] Pulsador PRESIONADO -> LED Externo ON (t = %u ms)\r\n", (unsigned int)now);
                }
                else
                {
                    ExtLed_Set(false);
                    PRINTF("[GPIO] Pulsador LIBERADO   -> LED Externo OFF (t = %u ms)\r\n", (unsigned int)now);
                }
            }
        }
    }
}
