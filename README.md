# Semáforo Solar IoT ESP32

Sistema de semáforo inteligente con ESP32 conectado a red celular (SIM7000G), alimentado por panel solar y batería. Controla LEDs de semáforo, muestra información en display LED matrix y se comunica vía MQTT.

## Características

- **Microcontrolador**: ESP32 Dev Module
- **Comunicación**: SIM7000G (GSM/LTE) con MQTT
- **Display**: Matriz LED 4x8x8 (MAX7219)
- **Alimentación**: Panel solar + Batería con gestión de energía
- **Sensores**: Voltaje batería/panel, corriente (ACS712), DHT11, NTC, GPS
- **Web**: Dashboard embebido con API REST
- **Actualización**: OTA (Over-The-Air)

## Hardware

| Componente | Descripción |
|------------|-------------|
| ESP32 | Microcontrolador principal |
| SIM7000G | Módulo GSM/LTE + GPS |
| MAX7219 | Display LED Matrix 4x8x8 |
| ACS712 | Sensor de corriente 5A |
| DHT11 | Sensor temperatura/humedad |
| NTC 10K | Sensor temperatura batería |

## Estados del Semáforo

| Estado | LED | Descripción |
|--------|-----|-------------|
| ROJO | 🔴 | Detener (30s default) |
| AMARILLO | 🟡 | Precaución (5s default) |
| VERDE | 🟢 | Avanzar (25s default) |
| PARPADEANDO | 🔴 | Modo alerta |

## Niveles de Batería

| Nivel | Voltaje | Acción |
|-------|---------|--------|
| NORMAL | >= 12.0V | Operación normal |
| ADVERTENCIA | < 12.0V | Modo ahorro |
| BAJO | < 11.5V | Modo mínimo |
| CRÍTICO | < 11.0V | Deep sleep |

## MQTT Topics

```
semaforo/control    → Entrada de comandos
semaforo/status     → Estado completo
semaforo/solar      → Datos solares
semaforo/gps        → Ubicación GPS
```

## Web Dashboard

Acceso vía IP del dispositivo (cuando hay conexión GPRS):
- `/` - Dashboard principal
- `/api` - JSON con todos los datos
- `/control?cmd=ROJO|VERDE|AMARILLO|PARPADEO` - Control
- `/reset` - Guardar config y reiniciar

## Pines

| Pin | Función |
|-----|---------|
| 4 | Power Key SIM7000G |
| 26, 27 | RX/TX Modem |
| 5 | CS Display |
| 12, 13, 14 | LED Rojo, Amarillo, Verde |
| 34, 35 | Voltaje Batería, Panel |
| 32 | Corriente (ACS712) |
| 33 | DHT11 |
| 36 | NTC Batería |

## Instalación

```bash
# Clonar repositorio
git clone https://github.com/palbina/semaforo-esp32.git
cd semaforo-esp32

# Compilar
pio run

# Subir
pio run --target upload

# Monitor
pio device monitor
```

## OTA

- Host: IP del dispositivo
- Puerto: 3232
- Password: semaforo123

## Configuración

Los parámetros se almacenan en NVS y pueden modificarse desde el panel web:
- Tiempos del semáforo (rojo, verde, amarillo)
- Umbrales de batería

## Licencia

MIT License

## Autor

Pablo Albina (@palbina)