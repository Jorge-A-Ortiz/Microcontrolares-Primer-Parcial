# Primer Parcial - Microcontroladores (MCXA156)

- Estudiante: Jorge Alberto Ortiz Nieves
- Matrícula: 20250504
- Reto asignado: Controlador de Iluminación Bare-Metal con Consola Serial
- Placa: NXP FRDM-MCXA156

## Descripción
Sistema de control de iluminación y consola serial desarrollado sobre el microcontrolador NXP MCXA156 en lenguaje C bare-metal.

## Compilación y ejecución
1. Abrir la carpeta del proyecto en MCUXpresso for VS Code.
2. Compilar utilizando el preset Debug.
3. Flashear el binario en la placa FRDM-MCXA156.
4. Conectar un monitor serial a 115200 baudios (8N1).

## Video demostrativo
(El enlace al video se colocara aqui al finalizar la implementacion)

## Estado actual
- Consola serial interactiva no bloqueante en LPUART0 a 115200 baudios (8N1) con recepcion por interrupcion (`LPUART0_IRQn`) y bufer circular de 128 bytes.
- Analizador de comandos delimitado por terminadores CR, LF o CRLF (longitud maxima de 63 caracteres) con gestion de desbordamiento (`ERR OVERFLOW`).
- Implementacion completa del protocolo de comandos: `HELP`, `STATUS`, `MODE MANUAL`, `MODE AUTO`, `DUTY n`, `FREQ n`, `PAUSE`, `RESUME`, `STREAM ON`, `STREAM OFF`.
- Matriz de validacion numerica y codigos de error normativos: `ERR COMMAND`, `ERR ARG`, `ERR RANGE`, `ERR STATE`, `ERR OVERFLOW`.
- Telemetria periodica (`[TELEMETRY]`) configurable a 1000 ms y telemetria bajo demanda con `STATUS` (uptime, pulsaciones cortas/largas y errores acumulados).
- Avances previos: maquina de estados de 3 modos (MANUAL, AUTO con rampa y retencion en extremos, PAUSA), senalizacion en LED de estado, modulacion PWM por hardware en CTIMER1, interrupciones GPIO con antirrebote y temporizador CTIMER0 de 1 ms.
