/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*${header:start}*/
#include "pin_mux.h"
#include "peripherals.h"
#include "fsl_clock.h"
#include "fsl_reset.h"
#include "board.h"
#include "app.h"
#include <stdbool.h>
/*${header:end}*/

/*${function:start}*/
void BOARD_InitHardware(void)
{
    CLOCK_SetClockDiv(kCLOCK_DivCTIMER0, 1u);
    CLOCK_AttachClk(kFRO12M_to_CTIMER0);

    BOARD_InitPins();
    BOARD_BootClockFRO12M();
    BOARD_InitBootPeripherals();
    BOARD_InitDebugConsole();
}
/*${function:end}*/
