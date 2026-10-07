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
- Maquina de estados finita (FSM) con 3 modos operativos: MANUAL (duty fijo al 25%), AUTO (rampa triangular de 0% a 100% en pasos de 60 ms con retencion en extremos de apagado y encendido) y PAUSA (PWM forzado a 0% con retencion de estado).
- Senalizacion en LED de estado (P3_13): encendido continuo en MANUAL, parpadeo a 1500 ms en AUTO y parpadeo a 375 ms en PAUSA.
- Control concurrente por pulsadores fisicos: pulsacion corta conmuta MANUAL <-> AUTO; pulsacion larga conmuta / reanuda modo PAUSA.
- Avances previos: modulacion PWM por hardware en CTIMER1, interrupciones GPIO con filtro antirrebote de 30 ms, temporizador CTIMER0 de 1 ms, ruteo de pines y consola serie LPUART0.
