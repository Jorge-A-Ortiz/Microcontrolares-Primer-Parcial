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
- Configuracion de pines de hardware en `pin_mux.c`: LED de estado (`P3_13`), LED externo (`P3_12`), pulsadores `SW3` y `SW2` con pull-up interno, y lineas serie UART.
- Consola serie LPUART0 inicializada a 115200 baudios (8N1) con mensaje de arranque.
- Prueba basica de lectura de pulsadores y encendido de LEDs.
