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
- Interrupciones fisicas GPIO en ambos flancos para pulsadores `SW3` (`GPIO0`) y `SW2` (`GPIO1`).
- Filtro antirrebote de 30 ms no bloqueante en el bucle principal.
- Discriminacion temporal entre pulsacion corta (< 1.5 s) y pulsacion larga (>= 1.5 s) con reporte por consola serie y medicion de tiempo en ms.
- Avances previos: base de tiempo CTIMER0 de 1 ms, ruteo de pines y consola serie LPUART0.
