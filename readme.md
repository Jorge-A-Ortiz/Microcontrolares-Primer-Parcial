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
