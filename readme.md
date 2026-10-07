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
- Modulacion por ancho de pulsos (PWM) por hardware implementada en `CTIMER1` con salida en pin `P3_12` (Match 2).
- Soporte para frecuencias de 500 Hz, 1000 Hz y 2000 Hz, y ciclo de trabajo de 0% a 100% con proteccion anti-desbordamiento del contador `TC`.
- Conmutacion interactiva mediante pulsadores: pulsacion corta cambia ciclo de trabajo (25%, 50%, 75%, 100%, 0%) y pulsacion larga cambia frecuencia (1000 Hz, 2000 Hz, 500 Hz).
- Avances previos: interrupciones GPIO con antirrebote de 30 ms, base de tiempo CTIMER0 de 1 ms, ruteo de pines y consola serie LPUART0.
