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
- Base de tiempo periodica de 1 ms implementada con temporizador de periferico `CTIMER0` por interrupcion.
- Eliminacion de SysTick y funciones de retardo bloqueantes (`SDK_DelayAtLeastUs`).
- Conmutacion periodica del LED de estado cada 500 ms usando el contador global `g_ms`.
- Avances previos: ruteo de pines y consola serie LPUART0 a 115200.
