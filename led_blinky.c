#include <string.h>
#include <ctype.h>
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_ctimer.h"
#include "fsl_lpuart.h"
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

#define AUTO_STEP_INTERVAL_MS 20U
#define AUTO_MIN_DUTY         10U
#define AUTO_MAX_DUTY         90U

#define STATUS_BLINK_AUTO_MS  500U
#define STATUS_BLINK_PAUSE_MS 125U

#define RX_RING_BUFFER_SIZE   128U
#define CMD_LINE_MAX_LEN      63U
#define TELEMETRY_INTERVAL_MS 1000U

typedef enum {
    STATE_MANUAL = 0,
    STATE_AUTO,
    STATE_PAUSE
} app_state_t;

typedef enum {
    BTN_STATE_IDLE = 0,
    BTN_STATE_DEBOUNCE_PRESS,
    BTN_STATE_PRESSED,
    BTN_STATE_DEBOUNCE_RELEASE
} button_fsm_state_t;

static volatile uint32_t g_ms = 0U;
static volatile uint32_t g_buttonEdgePending = 0U;

static volatile uint8_t  s_rxRingBuffer[RX_RING_BUFFER_SIZE];
static volatile uint16_t s_rxRingHead = 0U;
static volatile uint16_t s_rxRingTail = 0U;
static volatile uint32_t s_rxOverflowCount = 0U;
static volatile uint32_t s_rxErrorCount = 0U;

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

static uint32_t s_shortPressCount  = 0U;
static uint32_t s_longPressCount   = 0U;
static uint32_t s_totalErrors      = 0U;
static bool     s_telemetryEnabled = false;
static uint32_t s_lastTelemetryMs  = 0U;

static char     s_cmdLine[CMD_LINE_MAX_LEN + 1U];
static uint16_t s_cmdLineIndex = 0U;
static bool     s_cmdDiscardUntilEol = false;

void SysTick_Handler(void)
{
    /* SysTick desactivado en peripherals.c; CTIMER0 provee la base de tiempo de 1 ms */
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

void LPUART0_IRQHandler(void)
{
    uint32_t status = LPUART_GetStatusFlags(LPUART0);

    if ((status & kLPUART_RxDataRegFullFlag) != 0U)
    {
        uint8_t byte = LPUART_ReadByte(LPUART0);
        uint16_t nextHead = (uint16_t)((s_rxRingHead + 1U) % RX_RING_BUFFER_SIZE);
        if (nextHead != s_rxRingTail)
        {
            s_rxRingBuffer[s_rxRingHead] = byte;
            s_rxRingHead = nextHead;
        }
        else
        {
            s_rxOverflowCount++;
        }
    }

    if ((status & (kLPUART_RxOverrunFlag | kLPUART_FramingErrorFlag | kLPUART_ParityErrorFlag)) != 0U)
    {
        LPUART_ClearStatusFlags(LPUART0, kLPUART_RxOverrunFlag | kLPUART_FramingErrorFlag | kLPUART_ParityErrorFlag);
        s_rxErrorCount++;
    }
    SDK_ISR_EXIT_BARRIER;
}

static inline bool UartRingBufferGet(uint8_t *ch)
{
    if (s_rxRingHead == s_rxRingTail)
    {
        return false;
    }
    *ch = s_rxRingBuffer[s_rxRingTail];
    s_rxRingTail = (uint16_t)((s_rxRingTail + 1U) % RX_RING_BUFFER_SIZE);
    return true;
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
    uint32_t periodTicks = (timerClockHz / freq) - 1U;

    /* Configurar el período en MR3 */
    APP_PWM_TIMER->MR[APP_PWM_PERIOD_CH] = periodTicks;

    /* Tratamiento especial de casos límite 0% y 100% para evitar pulsos residuales / glitches */
    if (duty == 0U)
    {
        /* Desacoplar salida de modo PWM y forzar nivel lógico bajo constante (0V) vía EMR */
        APP_PWM_TIMER->PWMC &= ~(1UL << APP_PWM_OUTPUT_CH);
        APP_PWM_TIMER->EMR &= ~(1UL << APP_PWM_OUTPUT_CH);
        APP_PWM_TIMER->MR[APP_PWM_OUTPUT_CH] = periodTicks + 1U;
    }
    else if (duty == 100U)
    {
        /* Desacoplar salida de modo PWM y forzar nivel lógico alto constante (3.3V) vía EMR */
        APP_PWM_TIMER->PWMC &= ~(1UL << APP_PWM_OUTPUT_CH);
        APP_PWM_TIMER->EMR |= (1UL << APP_PWM_OUTPUT_CH);
        APP_PWM_TIMER->MR[APP_PWM_OUTPUT_CH] = 0U;
    }
    else
    {
        /* Modo PWM activo continuo: calcular punto de coincidencia */
        uint32_t pulseTicks = (periodTicks * (100U - (uint32_t)duty)) / 100U;
        APP_PWM_TIMER->MR[APP_PWM_OUTPUT_CH] = pulseTicks;
        APP_PWM_TIMER->PWMC |= (1UL << APP_PWM_OUTPUT_CH);
    }

    /* Si el contador actual TC superó el nuevo período MR3 tras un cambio de frecuencia,
     * reseteamos TC a 0 para evitar desbordamiento de 358 s. */
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
    uint32_t timerClockHz = CLOCK_GetCTimerClkFreq(1U);

    CTIMER_GetDefaultConfig(&cfg);
    cfg.prescale = 0U;
    CTIMER_Init(APP_PWM_TIMER, &cfg);

    /* Inicializar PWM en CTIMER1 con Match 3 como período y Match 2 como salida (P3_12) */
    CTIMER_SetupPwm(APP_PWM_TIMER, APP_PWM_PERIOD_CH, APP_PWM_OUTPUT_CH,
                    s_manualDuty, s_pwmFrequency, timerClockHz, false);

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

static void Hardware_UartInit(void)
{
    LPUART_EnableInterrupts(LPUART0, kLPUART_RxDataRegFullInterruptEnable);
    EnableIRQ(LPUART0_IRQn);
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
        s_currentState   = STATE_AUTO;
        s_autoDuty       = AUTO_MIN_DUTY; /* Inicia en 10% ascendente según mandato */
        s_autoAscending  = true;
        s_lastAutoStepMs = g_ms;
        Hardware_PwmApply(s_autoDuty, s_pwmFrequency);
    }
    else if (nextState == STATE_MANUAL)
    {
        s_currentState = STATE_MANUAL;
        Hardware_PwmApply(s_manualDuty, s_pwmFrequency); /* Restaura último duty manual */
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
        Hardware_PwmApply(s_autoDuty, s_pwmFrequency); /* Continúa rampa desde donde quedó */
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
    if (s_currentState != STATE_AUTO)
    {
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
                s_autoDuty--;
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
                s_autoDuty++;
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
    static button_fsm_state_t s_btnState = BTN_STATE_IDLE;
    static uint32_t s_debounceStartTime = 0U;
    static uint32_t s_pressStartTime    = 0U;
    static bool     s_longPressExecuted = false;

    /* Leer nivel físico de los pulsadores de usuario (activos-bajos con pull-up) */
    uint32_t sw3 = GPIO_PinRead(APP_BTN_SW3_GPIO, APP_BTN_SW3_PIN);
    uint32_t sw2 = GPIO_PinRead(APP_BTN_SW2_GPIO, APP_BTN_SW2_PIN);
    uint8_t currentPin = (sw3 == 0U || sw2 == 0U) ? 0U : 1U;

    uint32_t edgeDetected = TakeButtonEdge();

    switch (s_btnState)
    {
        case BTN_STATE_IDLE:
            if (edgeDetected != 0U)
            {
                if (currentPin == 0U)
                {
                    s_debounceStartTime = now;
                    s_btnState = BTN_STATE_DEBOUNCE_PRESS;
                }
            }
            break;

        case BTN_STATE_DEBOUNCE_PRESS:
            if (currentPin != 0U)
            {
                s_btnState = BTN_STATE_IDLE;
            }
            else
            {
                /* Si ocurre un nuevo flanco mientras permanece en bajo, reiniciar ventana de 30 ms */
                if (edgeDetected != 0U)
                {
                    s_debounceStartTime = now;
                }
                else if ((uint32_t)(now - s_debounceStartTime) >= DEBOUNCE_TIME_MS)
                {
                    s_pressStartTime = now;
                    s_longPressExecuted = false;
                    s_btnState = BTN_STATE_PRESSED;
                }
            }
            break;

        case BTN_STATE_PRESSED:
            if (!s_longPressExecuted && ((uint32_t)(now - s_pressStartTime) >= LONG_PRESS_TIME_MS))
            {
                s_longPressExecuted = true;
                s_longPressCount++;

                if (s_currentState == STATE_PAUSE)
                {
                    FSM_ResumeFromPause();
                    PRINTF("\r\n>>> [BOTON] Pulsacion larga (>= 1.5 s) -> REANUDADO desde PAUSA\r\n");
                }
                else
                {
                    FSM_TransitionTo(STATE_PAUSE);
                    PRINTF("\r\n>>> [BOTON] Pulsacion larga (>= 1.5 s) -> Modo PAUSA (PWM apagado)\r\n");
                }
            }

            if (edgeDetected != 0U || currentPin != 0U)
            {
                if (currentPin != 0U)
                {
                    s_debounceStartTime = now;
                    s_btnState = BTN_STATE_DEBOUNCE_RELEASE;
                }
            }
            break;

        case BTN_STATE_DEBOUNCE_RELEASE:
            if (currentPin == 0U)
            {
                s_btnState = BTN_STATE_PRESSED;
            }
            else
            {
                if (edgeDetected != 0U)
                {
                    s_debounceStartTime = now;
                }
                else if ((uint32_t)(now - s_debounceStartTime) >= DEBOUNCE_TIME_MS)
                {
                    uint32_t pressDuration = (uint32_t)(now - s_pressStartTime);
                    s_btnState = BTN_STATE_IDLE;

                    if (!s_longPressExecuted && (pressDuration >= DEBOUNCE_TIME_MS) && (pressDuration < LONG_PRESS_TIME_MS))
                    {
                        if (s_currentState == STATE_MANUAL)
                        {
                            s_shortPressCount++;
                            FSM_TransitionTo(STATE_AUTO);
                            PRINTF("\r\n>>> [BOTON] Pulsacion corta (%u ms) -> Modo AUTO\r\n", (unsigned int)pressDuration);
                        }
                        else if (s_currentState == STATE_AUTO)
                        {
                            s_shortPressCount++;
                            FSM_TransitionTo(STATE_MANUAL);
                            PRINTF("\r\n>>> [BOTON] Pulsacion corta (%u ms) -> Modo MANUAL\r\n", (unsigned int)pressDuration);
                        }
                        else
                        {
                            PRINTF("\r\n>>> [BOTON] Pulsacion corta ignorada en modo PAUSA\r\n");
                        }
                    }
                }
            }
            break;

        default:
            s_btnState = BTN_STATE_IDLE;
            break;
    }
}

static bool ParseUnsignedNumber(const char *str, uint32_t *valOut)
{
    if (str == NULL || *str == '\0')
    {
        return false;
    }

    uint64_t val = 0;
    while (*str != '\0')
    {
        if (!isdigit((unsigned char)*str))
        {
            return false;
        }
        val = (val * 10U) + (uint32_t)(*str - '0');
        if (val > 0xFFFFFFFFULL)
        {
            return false;
        }
        str++;
    }

    *valOut = (uint32_t)val;
    return true;
}

static void ExecuteCommand(char *cmd)
{
    while (*cmd == ' ' || *cmd == '\t')
    {
        cmd++;
    }

    int len = (int)strlen(cmd);
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
    {
        cmd[--len] = '\0';
    }

    if (len == 0)
    {
        return;
    }

    if (strcmp(cmd, "HELP") == 0)
    {
        PRINTF("\r\n--- COMANDOS DISPONIBLES ---\r\n");
        PRINTF("HELP\r\n");
        PRINTF("STATUS\r\n");
        PRINTF("MODE MANUAL\r\n");
        PRINTF("MODE AUTO\r\n");
        PRINTF("DUTY <0-100>\r\n");
        PRINTF("FREQ <500|1000|2000>\r\n");
        PRINTF("PAUSE\r\n");
        PRINTF("RESUME\r\n");
        PRINTF("STREAM ON\r\n");
        PRINTF("STREAM OFF\r\n");
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "STATUS") == 0)
    {
        const char *stateStr = (s_currentState == STATE_MANUAL) ? "MANUAL" :
                               (s_currentState == STATE_AUTO)   ? "AUTO"   : "PAUSA";

        PRINTF("STATUS MODE=%s FREQ=%u DUTY=%u DUTY_MANUAL=%u UPTIME=%ums BTN_SHORT=%u BTN_LONG=%u ERRORS=%u\r\n",
               stateStr, (unsigned int)s_pwmFrequency, (unsigned int)s_appliedDuty,
               (unsigned int)s_manualDuty, (unsigned int)g_ms,
               (unsigned int)s_shortPressCount, (unsigned int)s_longPressCount,
               (unsigned int)s_totalErrors);
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "MODE MANUAL") == 0)
    {
        if (s_currentState == STATE_PAUSE)
        {
            s_totalErrors++;
            PRINTF("ERR STATE\r\n");
            return;
        }
        FSM_TransitionTo(STATE_MANUAL);
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "MODE AUTO") == 0)
    {
        if (s_currentState == STATE_PAUSE)
        {
            s_totalErrors++;
            PRINTF("ERR STATE\r\n");
            return;
        }
        FSM_TransitionTo(STATE_AUTO);
        PRINTF("OK\r\n");
        return;
    }

    if (strncmp(cmd, "DUTY", 4) == 0)
    {
        char *arg = cmd + 4;
        if (*arg == '\0')
        {
            s_totalErrors++;
            PRINTF("ERR ARG\r\n");
            return;
        }
        if (*arg != ' ')
        {
            s_totalErrors++;
            PRINTF("ERR COMMAND\r\n");
            return;
        }
        while (*arg == ' ')
        {
            arg++;
        }

        if (s_currentState == STATE_PAUSE || s_currentState == STATE_AUTO)
        {
            s_totalErrors++;
            PRINTF("ERR STATE\r\n");
            return;
        }

        uint32_t dutyVal = 0U;
        if (!ParseUnsignedNumber(arg, &dutyVal))
        {
            s_totalErrors++;
            PRINTF("ERR ARG\r\n");
            return;
        }

        if (dutyVal > 100U)
        {
            s_totalErrors++;
            PRINTF("ERR RANGE\r\n");
            return;
        }

        s_manualDuty = (uint8_t)dutyVal;
        Hardware_PwmApply(s_manualDuty, s_pwmFrequency);
        PRINTF("OK\r\n");
        return;
    }

    if (strncmp(cmd, "FREQ", 4) == 0)
    {
        char *arg = cmd + 4;
        if (*arg == '\0')
        {
            s_totalErrors++;
            PRINTF("ERR ARG\r\n");
            return;
        }
        if (*arg != ' ')
        {
            s_totalErrors++;
            PRINTF("ERR COMMAND\r\n");
            return;
        }
        while (*arg == ' ')
        {
            arg++;
        }

        if (s_currentState == STATE_PAUSE)
        {
            s_totalErrors++;
            PRINTF("ERR STATE\r\n");
            return;
        }

        uint32_t freqVal = 0U;
        if (!ParseUnsignedNumber(arg, &freqVal))
        {
            s_totalErrors++;
            PRINTF("ERR ARG\r\n");
            return;
        }

        if (freqVal != 500U && freqVal != 1000U && freqVal != 2000U)
        {
            s_totalErrors++;
            PRINTF("ERR RANGE\r\n");
            return;
        }

        s_pwmFrequency = freqVal;
        Hardware_PwmApply(s_appliedDuty, s_pwmFrequency);
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "PAUSE") == 0)
    {
        if (s_currentState == STATE_PAUSE)
        {
            PRINTF("OK\r\n");
            return;
        }
        FSM_TransitionTo(STATE_PAUSE);
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "RESUME") == 0)
    {
        if (s_currentState != STATE_PAUSE)
        {
            s_totalErrors++;
            PRINTF("ERR STATE\r\n");
            return;
        }
        FSM_ResumeFromPause();
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "STREAM ON") == 0)
    {
        s_telemetryEnabled = true;
        s_lastTelemetryMs  = g_ms;
        PRINTF("OK\r\n");
        return;
    }

    if (strcmp(cmd, "STREAM OFF") == 0)
    {
        s_telemetryEnabled = false;
        PRINTF("OK\r\n");
        return;
    }

    s_totalErrors++;
    PRINTF("ERR COMMAND\r\n");
}

static void ProcessUartRx(void)
{
    uint8_t byte = 0U;

    while (UartRingBufferGet(&byte))
    {
        if (byte == '\r' || byte == '\n')
        {
            if (s_cmdDiscardUntilEol)
            {
                s_cmdDiscardUntilEol = false;
                s_cmdLineIndex = 0U;
                continue;
            }

            if (s_cmdLineIndex > 0U)
            {
                s_cmdLine[s_cmdLineIndex] = '\0';
                ExecuteCommand(s_cmdLine);
                s_cmdLineIndex = 0U;
            }
        }
        else
        {
            if (s_cmdDiscardUntilEol)
            {
                continue;
            }

            if (s_cmdLineIndex < CMD_LINE_MAX_LEN)
            {
                s_cmdLine[s_cmdLineIndex++] = (char)byte;
            }
            else
            {
                s_cmdDiscardUntilEol = true;
                s_cmdLineIndex = 0U;
                s_totalErrors++;
                PRINTF("ERR OVERFLOW\r\n");
            }
        }
    }

    if (s_rxOverflowCount > 0U)
    {
        s_totalErrors += s_rxOverflowCount;
        s_rxOverflowCount = 0U;
    }
}

static void ProcessTelemetry(uint32_t now)
{
    if (!s_telemetryEnabled)
    {
        return;
    }

    if ((uint32_t)(now - s_lastTelemetryMs) >= TELEMETRY_INTERVAL_MS)
    {
        s_lastTelemetryMs = now;
        const char *stateStr = (s_currentState == STATE_MANUAL) ? "MANUAL" :
                               (s_currentState == STATE_AUTO)   ? "AUTO"   : "PAUSA";

        PRINTF("[TELEMETRY] MODE:%s FREQ:%uHz DUTY:%u%% UPTIME:%ums\r\n",
               stateStr, (unsigned int)s_pwmFrequency, (unsigned int)s_appliedDuty,
               (unsigned int)now);
    }
}

int main(void)
{
    BOARD_InitHardware();
    Hardware_TickInit();
    Hardware_PwmInit();
    Hardware_ButtonInit();
    Hardware_UartInit();

    StatusLed_Set(true);

    PRINTF("\r\n====================================================\r\n");
    PRINTF(" FRDM-MCXA156 - CONTROLADOR DE ILUMINACION BARE-METAL\r\n");
    PRINTF(" Estudiante: Jorge Alberto Ortiz Nieves | Mat: 20250504\r\n");
    PRINTF(" Modo Inicial: MANUAL | Freq: 1000 Hz | Duty: 25%%\r\n");
    PRINTF(" Ingrese 'HELP' para ver la lista de comandos.\r\n");
    PRINTF("====================================================\r\n\r\n");

    uint32_t lastService = g_ms;

    while (1)
    {
        uint32_t now = g_ms;

        if ((uint32_t)(now - lastService) >= 1U)
        {
            lastService = now;

            Task_StatusLed(now);
            Task_AutoRamp(now);
            ProcessButton(now);
            ProcessTelemetry(now);
        }

        ProcessUartRx();
    }
}
