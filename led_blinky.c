#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
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

void SysTick_Handler(void)
{
}

static inline void StatusLed_Set(bool on)
{
    GPIO_PinWrite(APP_LED_STATUS_GPIO, APP_LED_STATUS_PIN, on ? 0U : 1U);
}

static inline void ExtLed_Set(bool on)
{
    GPIO_PinWrite(APP_LED_EXT_GPIO, APP_LED_EXT_PIN, on ? 1U : 0U);
}

int main(void)
{
    BOARD_InitHardware();

    StatusLed_Set(true);
    ExtLed_Set(false);

    PRINTF("\r\n====================================================\r\n");
    PRINTF(" FRDM-MCXA156 - CONTROLADOR DE ILUMINACION BARE-METAL\r\n");
    PRINTF(" Estudiante: Jorge Alberto Ortiz Nieves | Mat: 20250504\r\n");
    PRINTF(" Baudrate: 115200 8N1\r\n");
    PRINTF("====================================================\r\n");
    PRINTF("Prueba de hardware GPIO:\r\n");
    PRINTF(" - Presione SW2 o SW3 para conmutar el LED externo en J1-15.\r\n\r\n");

    uint8_t lastBtnState = 1U;

    while (1)
    {
        uint32_t sw3 = GPIO_PinRead(APP_BTN_SW3_GPIO, APP_BTN_SW3_PIN);
        uint32_t sw2 = GPIO_PinRead(APP_BTN_SW2_GPIO, APP_BTN_SW2_PIN);
        uint8_t btnPressed = (sw3 == 0U || sw2 == 0U) ? 0U : 1U;

        if (btnPressed != lastBtnState)
        {
            lastBtnState = btnPressed;

            if (btnPressed == 0U)
            {
                ExtLed_Set(true);
                PRINTF("[GPIO] Pulsador PRESIONADO -> LED Externo ON\r\n");
            }
            else
            {
                ExtLed_Set(false);
                PRINTF("[GPIO] Pulsador LIBERADO   -> LED Externo OFF\r\n");
            }

            SDK_DelayAtLeastUs(50000U, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
        }
    }
}
