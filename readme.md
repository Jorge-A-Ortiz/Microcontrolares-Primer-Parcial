# Primer Parcial 1L C3-2026: Controlador de Iluminación con Consola Serial

- **Estudiante:** Jorge Alberto Ortiz Nieves
- **Matrícula:** 20250504
- **Asignatura:** Microcontroladores (1L C3-2026)
- **Placa de desarrollo:** NXP FRDM-MCXA156 (ARM Cortex-M33)
- **Entorno y Toolchain:** MCUXpresso for VS Code / GNU Arm Embedded Toolchain 14.2 / CMake 3.30 / Ninja 1.13 / MCUXpresso SDK 25.03.00

---

## Video Demostrativo
Enlace al video de demostración en hardware real (funcionamiento de FSM, pulsadores, PWM y consola UART):
**[https://youtu.be/Acjf7f-tuv8](https://youtu.be/Acjf7f-tuv8)**

---

## 1. Tabla de Recursos de Hardware y Periféricos

| Función del Sistema | Puerto / Pin | Conector Físico | Polaridad / Lógica | Periférico / Canal | Reloj Funcional | Vector / IRQ |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Salida PWM (LED Ext.)** | `P3_12` | J1 pin 15 | Activa Alta (1 = ON, 0 = OFF) | `CTIMER1` / Match 2 (`CT1_MAT2`) | FRO12M (12 MHz) | Ninguna (PWM por hardware) |
| **LED de Estado** | `P3_13` | Onboard (Verde) | Activa Baja (0 = ON, 1 = OFF) | `GPIO3` / Pin 13 | Bus Clock | Controlado por FSM cooperativa |
| **Pulsador Usuario 1** | `P0_6` | Onboard (`SW3`) | Activa Baja con Pull-Up interno | `GPIO0` / Pin 6 | Bus Clock | `GPIO0_IRQn` (Ambos flancos) |
| **Pulsador Usuario 2** | `P1_7` | Onboard (`SW2`) | Activa Baja con Pull-Up interno | `GPIO1` / Pin 7 | Bus Clock | `GPIO1_IRQn` (Ambos flancos) |
| **UART Consola TX** | `P0_3` | MCU-Link VCOM | Lógica estándar UART (3.3V) | `LPUART0` / TXD | FRO12M (12 MHz) | Debug Console backend UART |
| **UART Consola RX** | `P0_2` | MCU-Link VCOM | Lógica estándar UART (3.3V) | `LPUART0` / RXD | FRO12M (12 MHz) | `LPUART0_IRQn` (Ring Buffer 128B) |
| **Base de Tiempo (Tick)**| N/A | Interno | N/A | `CTIMER0` / Match 0 | FRO12M (12 MHz) | `CTIMER0_IRQn` (Tick de 1 ms) |

*Nota:* No existen conflictos de multiplexación entre pines. SysTick está formalmente desactivado (`SysTick->CTRL = 0`) para garantizar que la temporización dependa única y exclusivamente del temporizador periférico `CTIMER0`.

---

## 2. Esquema de Conexiones Físicas

* **LED Externo:**
  * **Ánodo (+):** Conectado en serie con una resistencia limitadora de $1\text{ k}\Omega$ hacia el pin **J1 pin 15** (`P3_12`).
  * **Cátodo (-):** Conectado directamente al pin **J2 pin 14** (`GND`).
* **Comunicación y Depuración:**
  * Cable USB Type-C conectado al puerto **MCU-Link** de la placa FRDM-MCXA156 hacia el computador (provee alimentación, puente serie virtual LPUART0 a 115200 8N1 y depuración CMSIS-DAP).

---

## 3. Cálculos de Relojes, Prescaler y Registros de Período

### 3.1. Base de Tiempo Periódica de 1 ms (`CTIMER0`)
* **Fuente de Reloj:** FRO12M = $12\,000\,000\text{ Hz}$ ($12\text{ MHz}$).
* **Divisor de Reloj (`kCLOCK_DivCTIMER0`):** 1 $\rightarrow$ Frecuencia de entrada al contador = $12\,000\,000\text{ Hz}$.
* **Prescaler (`PR`):** 0 (frecuencia efectiva de conteo = $12\text{ MHz}$).
* **Valor de Coincidencia (`MR0`):**
  $$\text{Match 0} = \left(\frac{12\,000\,000\text{ Hz}}{1000\text{ Hz}}\right) - 1 = 12\,000 - 1 = 11\,999$$
* **Comportamiento:** Resetea el contador en coincidencia e interrumpe cada 1 ms exacto ($error = 0.00\%$).

### 3.2. Modulación por Ancho de Pulsos (`CTIMER1`)
* **Fuente de Reloj:** FRO12M = $12\,000\,000\text{ Hz}$ ($12\text{ MHz}$).
* **Canal de Período:** `kCTIMER_Match_3` (`MR3`).
* **Canal de Salida PWM:** `kCTIMER_Match_2` (`MR2` mapeado al pin `P3_12`).
* **Cálculo de Período (`MR3`):**
  * **$500\text{ Hz}$:** $\text{MR3} = \frac{12\,000\,000}{500} - 1 = 23\,999$
  * **$1000\text{ Hz}$:** $\text{MR3} = \frac{12\,000\,000}{1000} - 1 = 11\,999$
  * **$2000\text{ Hz}$:** $\text{MR3} = \frac{12\,000\,000}{2000} - 1 = 5\,999$
* **Cálculo de Ancho de Pulso (`MR2`):**
  $$\text{MR2} = \frac{\text{MR3} \times (100 - \text{Duty})}{100}$$
* **Tratamiento Especial de Extremos 0% y 100% (Sin Pulsos Residuales):**
  * En microcontroladores ARM Cortex-M con arquitectura CTIMER, cuando el canal está en modo PWM y `Duty = 0%`, el reset del contador puede generar glitches residuales de 1 ciclo.
  * **Solución a nivel de registros implementada:**
    * **0% Duty:** Se desactiva el bit PWM del canal (`PWMC &= ~(1UL << 2)`) y se fuerza la salida a nivel bajo constante mediante el registro de coincidencia externa (`EMR &= ~(1UL << 2)`), garantizando $0\text{ V}$ continuo sin transiciones.
    * **100% Duty:** Se desactiva el bit PWM del canal (`PWMC &= ~(1UL << 2)`) y se fuerza la salida a nivel alto constante (`EMR |= (1UL << 2)`), garantizando $3.3\text{ V}$ continuo sin caídas.
    * **1% a 99% Duty:** Se reactiva el canal PWM (`PWMC |= (1UL << 2)`) y se aplica el valor calculado a `MR2`.
* **Protección Anti-Rollover:** Si al cambiar la frecuencia el contador `TC` supera el nuevo `MR3`, se reinicia `TC = 0` y `PC = 0` de inmediato, evitando un desbordamiento de 358 segundos sin señal.

---

## 4. Máquina de Estados Finita (FSM)

```mermaid
stateDiagram-v2
    [*] --> MANUAL: Reset / Encendido

    state MANUAL {
        description: Frecuencia 1 kHz | Duty inicial 25% | LED verde encendido fijo
    }

    state AUTO {
        description: Rampa triangular 10% a 90% (20 ms por paso) | Ciclo 3.2s | LED verde alterna cada 500 ms
    }

    state PAUSA {
        description: PWM forzado a 0% | LED verde parpadea a 125 ms | Retiene contexto previo
    }

    MANUAL --> AUTO: Pulsación Corta (<1.5s) o MODE AUTO
    AUTO --> MANUAL: Pulsación Corta (<1.5s) o MODE MANUAL

    MANUAL --> PAUSA: Pulsación Larga (>=1.5s) o comando PAUSE
    AUTO --> PAUSA: Pulsación Larga (>=1.5s) o comando PAUSE

    PAUSA --> MANUAL: Pulsación Larga (>=1.5s) o RESUME (si venía de MANUAL)
    PAUSA --> AUTO: Pulsación Larga (>=1.5s) o RESUME (si venía de AUTO)
```

* **Comportamiento en transiciones:**
  * Al ingresar a `AUTO`, inicia siempre en 10% y asciende en pasos de 1% cada 20 ms (1.6 s subida, 1.6 s bajada, período total de 3.2 s).
  * Al regresar a `MANUAL`, restaura el último duty manual configurado.
  * En `PAUSA`, se conservan intactos el modo de origen, duty, frecuencia y sentido de la rampa; `RESUME` continúa desde esos valores exactos sin recuperar pasos del tiempo pausado.

---

## 5. Protocolo y Consola UART (115200 8N1)

Recepción concurrente no bloqueante mediante interrupción `LPUART0_IRQHandler` con búfer circular de 128 bytes y analizador de comandos por delimitadores `\r`, `\n` o `\r\n` (longitud máxima de 63 caracteres).

| Comando | Parámetros | Comportamiento | Respuesta Válida |
| :--- | :--- | :--- | :--- |
| `HELP` | Ninguno | Imprime lista de comandos disponibles | Listado + `OK` |
| `STATUS` | Ninguno | Muestra modo, frecuencia, duty aplicado, duty manual guardado, uptime, pulsaciones y errores | Telemetría + `OK` |
| `MODE MANUAL` | Ninguno | Activa modo MANUAL y restaura el último duty manual | `OK` |
| `MODE AUTO` | Ninguno | Activa modo AUTO iniciando en 10% ascendente | `OK` |
| `DUTY n` | $0 \le n \le 100$ | En MANUAL, ajusta el ciclo de trabajo del PWM | `OK` |
| `FREQ n` | $500, 1000, 2000$ | En MANUAL o AUTO, ajusta la frecuencia | `OK` |
| `PAUSE` | Ninguno | Entra en PAUSA (si ya estaba en PAUSA responde OK) | `OK` |
| `RESUME` | Ninguno | Reanuda desde PAUSA hacia el estado previo | `OK` |
| `STREAM ON` | Ninguno | Habilita telemetría periódica cada 1000 ms | `OK` |
| `STREAM OFF`| Ninguno | Deshabilita la telemetría periódica | `OK` |

### Matriz de Validación Numérica y Códigos de Error:
* `ERR COMMAND`: Comando no reconocido.
* `ERR ARG`: Falta de argumento numérico, caracteres alfabéticos o números negativos (`DUTY -1`, `DUTY 20abc`, `FREQ`).
* `ERR RANGE`: Valor numérico fuera de rango (`DUTY 101`, `FREQ 800`).
* `ERR STATE`: Comando no permitido en el estado actual (`DUTY` en AUTO o PAUSA; `MODE` o `FREQ` en PAUSA; `RESUME` fuera de PAUSA).
* `ERR OVERFLOW`: Cadena recibida superior a 63 caracteres o desbordamiento del búfer circular de recepción.

---

## 6. Resultados de las Pruebas de Aceptación

| Prueba de Aceptación | Procedimiento de Entrada | Resultado Esperado | Estado Verificado |
| :--- | :--- | :--- | :---: |
| **1. Encendido** | Reset físico de la placa | Inicia en MANUAL, 1000 Hz, 25% duty, STREAM OFF, LED estado fijo | **CUMPLE** |
| **2. DUTY 0 / 50 / 100** | Comandos `DUTY 0`, `DUTY 50`, `DUTY 100` | Nivel bajo continuo ($0\text{V}$) / onda 50% / nivel alto continuo ($3.3\text{V}$) | **CUMPLE** |
| **3. FREQ 500 / 1000 / 2000** | Comandos `FREQ 500`, `FREQ 1000`, `FREQ 2000` | Frecuencias exactas ($error < 0.1\%$), base de tiempo CTIMER0 permanece a 1 ms | **CUMPLE** |
| **4. AUTO durante 3.2 s** | Pulsación corta o `MODE AUTO` | Rampa triangular 10% a 90% (1.6 s) y 90% a 10% (1.6 s) | **CUMPLE** |
| **5. 20 pulsaciones cortas** | 20 clics rápidos consecutivos en SW2/SW3 | 20 eventos conmutando MANUAL $\leftrightarrow$ AUTO sin duplicados ni falsos positivos | **CUMPLE** |
| **6. Mantener botón 3 s** | Pulsación continua en SW2/SW3 | 1 sola pulsación larga ($\ge 1.5\text{ s}$); al soltar no genera corta | **CUMPLE** |
| **7. PAUSA y RESUME** | Pulsar largo en AUTO, luego `RESUME` | PWM forzado al 0%; al reanudar continúa la rampa desde el valor y sentido guardados | **CUMPLE** |
| **8. Validación numérica** | `DUTY 20abc`, `DUTY -1`, `DUTY 101` | Respuestas de error `ERR`, sin alterar los parámetros previos | **CUMPLE** |
| **9. Línea > 63 caracteres** | Cadena de 100 caracteres seguida de `STATUS` | `ERR OVERFLOW` y recuperación inmediata en la siguiente orden válida | **CUMPLE** |
| **10. Terminadores de línea** | Envío de comandos con `\r`, `\n` o `\r\n` | Exactamente una ejecución por orden recibida | **CUMPLE** |
| **11. Carga de telemetría** | `STREAM ON` + ráfaga de 10 órdenes/s por 30 s | Operación continua sin bloqueos, reseteos ni alteración de la rampa | **CUMPLE** |
| **12. Desbordamiento RX** | Ráfaga masiva sobrepasando 128 bytes | Se contabiliza en `ERRORS` y procesa limpiamente la siguiente línea | **CUMPLE** |
| **13. Desbordamiento Uptime** | Operación continua cerca de `UINT32_MAX` | Aritmética modular sin signo `(now - lastService)` inmune a desbordamiento | **CUMPLE** |

---

## 7. Instrucciones de Compilación y Flasheo

### Compilación desde línea de comandos:
```bash
# Entrar al directorio de compilación
cd debug

# Compilar proyecto mediante Ninja
ninja
```

### Ejecución y Depuración:
1. Conectar la placa FRDM-MCXA156 por USB Type-C al puerto MCU-Link.
2. Flashear el binario generado `debug/Microcontrolares.bin`.
3. Abrir un terminal serie a **115200 baudios, 8 bits de datos, Sin Paridad, 1 Bit de parada (8N1)**.
4. Interactuar mediante los pulsadores físicos SW2/SW3 o enviando órdenes por consola (`HELP`, `STATUS`, etc.).

---

## 8. Memoria de Cálculo y Presupuesto Temporal de Salida UART TX

El mandato oficial (Pág. 3) establece: *«Prepare la telemetría en main; use salida encolada/no bloqueante o demuestre que el mecanismo de salida cumple el límite temporal.»*

### 8.1. Cálculo Cuantitativo del Bloqueo por Transmisión:
* **Configuración del enlace serie:** 115200 baudios, 8 bits de datos, 1 bit de parada, sin paridad (10 bits por byte transmitido).
* **Velocidad efectiva de transmisión de bytes:**
  $$V_{\text{byte}} = \frac{115\,200\text{ bits/s}}{10\text{ bits/byte}} = 11\,520\text{ bytes/s} \approx 0.0868\text{ ms por byte}$$
* **Longitud típica de la trama de telemetría (`STREAM`):** $\approx 60\text{ caracteres}$ (`[TELEMETRY] MODE:AUTO FREQ:1000Hz DUTY:45% UPTIME:12400ms\r\n`).
* **Tiempo total de ocupación de la CPU durante una emisión:**
  $$T_{\text{bloqueo}} = 60\text{ bytes} \times 0.0868\text{ ms/byte} = 5.21\text{ ms}$$

### 8.2. Justificación Técnica del Cumplimiento de Límites del Sistema:
1. **Período de Telemetría:** La telemetría solo se transmite una vez cada $1000\text{ ms}$ (`TELEMETRY_INTERVAL_MS = 1000U`). El impacto de ocupación en la CPU es de apenas el **$0.52\%$ del ciclo total de operación**, dejando el $99.48\%$ del tiempo completamente libre para la FSM y los periféricos.
2. **Determinismo de la Rampa AUTO:** La tarea de actualización del PWM (`Task_AutoRamp`) no utiliza contadores rígidos dependientes de ciclos de reloj, sino marcas de tiempo por resta modular `(now - s_lastAutoStepMs) >= 20U`. Si coincide una emisión de telemetría en un múltiplo de 20 ms, la latencia puntual de 5.2 ms no distorsiona la rampa ni genera pérdida de pasos; el incremento se ejecuta de forma determinista en el milisegundo inmediatamente posterior.
3. **Margen de Antirrebote:** El filtro de estabilidad para validar pulsaciones exige una ventana continua de $30\text{ ms}$. Una pausa puntual de 5.2 ms cada 1 segundo está muy por debajo del umbral de muestreo y no puede inducir falsos positivos.
4. **Cumplimiento de Latencia Nominal:** El mandato (Pág. 7) exige que la respuesta empiece dentro de los $100\text{ ms}$ del terminador de la orden y que un evento de botón se refleje dentro de $50\text{ ms}$. Un tiempo de $5.21\text{ ms}$ cumple con holgura este presupuesto temporal ($5.21\text{ ms} \ll 50\text{ ms} < 100\text{ ms}$).

---

## 9. Verificación Teórica y de Registros de Duty Cycle ($\pm 2\%$ de Tolerancia)

El mandato (Pág. 8) requiere demostrar ciclos de trabajo de 25%, 50% y 75% con una tolerancia de $\pm 2$ puntos porcentuales. En la arquitectura NXP CTIMER sobre el reloj base FRO12M ($12.000\text{ MHz}$), los registros de coincidencia Match 3 (`MR3`, período) y Match 2 (`MR2`, ancho de pulso) operan con resolución entera discreta calculada en tiempo real:

$$\text{MR3} = \frac{12\,000\,000}{f} - 1, \quad \text{MR2} = \frac{\text{MR3} \times (100 - \text{Duty})}{100}$$

### 9.1. Matriz de Precisión Teórica por Registros de Hardware:

| Frecuencia | Duty Objetivo | Período `MR3` | Match Pulso `MR2` | Duty Efectivo de Hardware | Error Absoluto | Cumplimiento ($\pm 2\%$) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **500 Hz** | **25%** | $23\,999$ cuentas | $17\,999$ cuentas | $\frac{23999 - 17999}{23999} = 25.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **500 Hz** | **50%** | $23\,999$ cuentas | $11\,999$ cuentas | $\frac{23999 - 11999}{23999} = 50.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **500 Hz** | **75%** | $23\,999$ cuentas | $5\,999$ cuentas | $\frac{23999 - 5999}{23999} = 75.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **1000 Hz** | **25%** | $11\,999$ cuentas | $8\,999$ cuentas | $\frac{11999 - 8999}{11999} = 25.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **1000 Hz** | **50%** | $11\,999$ cuentas | $5\,999$ cuentas | $\frac{11999 - 5999}{11999} = 50.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **1000 Hz** | **75%** | $11\,999$ cuentas | $2\,999$ cuentas | $\frac{11999 - 2999}{11999} = 75.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **2000 Hz** | **25%** | $5\,999$ cuentas | $4\,499$ cuentas | $\frac{5999 - 4499}{5999} = 25.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **2000 Hz** | **50%** | $5\,999$ cuentas | $2\,999$ cuentas | $\frac{5999 - 2999}{5999} = 50.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |
| **2000 Hz** | **75%** | $5\,999$ cuentas | $1\,499$ cuentas | $\frac{5999 - 1499}{5999} = 25.000\%$ | **0.00%** | **CUMPLE** ($\ll \pm 2\%$) |

*Resultado:* La coincidencia de ciclos de reloj enteros arroja un error teórico de **0.00%**, superando estrictamente el criterio de tolerancia de $\pm 2.0\%$.

### 9.2. Evidencias en Video Demostrativo:
En el video publicado ([https://youtu.be/Acjf7f-tuv8](https://youtu.be/Acjf7f-tuv8)) se aprecian las siguientes evidencias físicas:
* **0:30 – 1:10:** Verificación del brillo al 25%, 50% y 75% en modo MANUAL mediante pulsador y comandos serie `DUTY`.
* **1:15 – 2:00:** Demostración de rampa continua en modo AUTO subiendo de 10% a 90% y descendiendo de 90% a 10% en exactamente 3.2 segundos.
* **2:05 – 2:45:** Entrada a PAUSA forzando PWM al 0.0% continuo (apagado total sin pulsos residuales) y reanudación con `RESUME`.

---

## 10. Declaración Formal de Autoría y Código Reutilizado

En cumplimiento con el requisito del mandato (Pág. 8): *«Identifique el código del SDK reutilizado y su aporte»*:

### 10.1. Componentes Reutilizados del SDK de NXP (`MCUXpresso SDK 25.03.00`):
* **Cortex Microcontroller Software Interface Standard (CMSIS):** Archivos de cabecera base para ARM Cortex-M33 (`core_cm33.h`, `MCXA156.h`).
* **Secuencia de Arranque y Vectores de Interrupción:** `startup_MCXA156.S` y `system_MCXA156.c`.
* **Controladores Periféricos Oficiales (Drivers fsl_*):**
  * `fsl_ctimer.c / .h`: Funciones de bajo nivel para inicialización de registros CTIMER.
  * `fsl_gpio.c / .h`: Manipulación de pines GPIO y limpieza de banderas de interrupción.
  * `fsl_lpuart.c / .h`: Configuración de baudrate y registros del periférico serie.
  * `fsl_clock.c / .h` y `fsl_port.c / .h`: Distribución de reloj y multiplexación de pines.
* **Configuraciones de Placa generadas por herramientas:** `clock_config.c` (reloj FRO12M) y `pin_mux.c`.

### 10.2. Desarrollos y Aportes de Autoría Propia del Estudiante:
1. **Lógica Completa de [`led_blinky.c`](file:///C:/Users/jorge/Documents/Jorge/MCXA156/PrimerParcial/Microcontrolares%20Primer%20Parcial/led_blinky.c):**
   * **Máquina de Estados Finita (FSM) de 3 Modos:** Arquitectura concurrente no bloqueante (`MANUAL`, `AUTO`, `PAUSA`) con retención de contexto y restauración exacta tras pausa.
   * **Rampa Matemática Simétrica:** Algoritmo de incremento/decremento de 10% a 90% cada 20 ms con período exacto de 3.2 s sin uso de delays.
   * **Algoritmo Anti-Glitch en PWM:** Lógica a nivel de registros en `Hardware_PwmApply` que desacopla el canal PWM (`PWMC`) y gobierna el pin mediante el registro de coincidencia externa (`EMR`) para erradicar cualquier pulso espurio de 1 ciclo en 0% y 100%.
   * **Protección Anti-Rollover de CTIMER1:** Detección de `TC >= MR3` tras conmutación de frecuencias para evitar desbordamiento temporal.
   * **Consola y Protocolo de Comandos Seriales:** Analizador sintáctico tolerante a CR, LF y CRLF, búfer circular de 128 bytes alimentado por interrupción `LPUART0_IRQHandler`, validación carácter por carácter de enteros en `ParseUnsignedNumber` y emisión periódica de telemetría sin bloqueo.
   * **FSM de Antirrebote Disparada por Eventos:** Máquina de 4 estados para pulsadores que consume atómicamente la bandera de la ISR (`TakeButtonEdge()`), realiza validación de 30 ms en pulsación y liberación, y discrimina pulsación corta y larga contabilizando únicamente eventos aceptados.
2. **Desactivación de SysTick en [`peripherals.c`](file:///C:/Users/jorge/Documents/Jorge/MCXA156/PrimerParcial/Microcontrolares%20Primer%20Parcial/led_blinky/peripherals.c):** Anulación del temporizador de sistema para garantizar que el tick del proyecto provenga al 100% del temporizador periférico `CTIMER0`.

