# Proyecto Semáforo IoT - Contexto del Proyecto

## Resumen
Semáforo inteligente con ESP32 conectado a red celular (SIM7000G), alimentado por panel solar y batería. Controla LEDs de semáforo, muestra información en display LED matrix, se comunica vía MQTT y dispone de panel web embebido con OTA.

## Hardware
- **Microcontrolador**: ESP32 Dev Module
- **Módulo GSM/LTE**: SIM7000G
- **Display**: Matriz LED 4x8x8 (MAX7219)
- **Sensores**: 
  - Voltaje batería (divisor 100K/100K)
  - Voltaje panel solar (divisor 470K/100K)
  - Corriente (ACS712 5A)
  - Temperatura/Humedad ambiente (DHT11)
  - Temperatura batería (NTC 10K)
  - GPS (integrado en SIM7000G)
- **Actuadores**: 3 LEDs (Rojo, Amarillo, Verde)

## Estados del Proyecto
- ✅ Proyecto base ESP32 + Arduino framework
- ✅ TinyGSM integrado para SIM7000G
- ✅ PubSubClient para MQTT
- ✅ Sensores de voltaje/corriente solar
- ✅ Display LED Matrix MD_MAX72XX + MD_Parola
- ✅ Lógica de semáforo con tiempos configurables
- ✅ Control MQTT para cambiar estados
- ✅ Publicación de estado y datos solares por MQTT
- ✅ Sensor DHT11 (temperatura/humedad ambiente)
- ✅ Sensor NTC (temperatura batería)
- ✅ GPS integrado SIM7000G
- ✅ Web Server embebido (dashboard + API)
- ✅ OTA (Over-The-Air updates)
- ✅ Watchdog Timer (60s)
- ✅ Deep Sleep con protección de batería
- ✅ Preferences (config persistente en NVS)
- ✅ Reintentos exponenciales GPRS/MQTT
- ✅ Promedio ADC (5 muestras por lectura)

## Estructura de Archivos
```
Solmaforo/
├── platformio.ini          # Configuración del proyecto
├── src/
│   └── main.cpp           # Código principal (800+ líneas)
├── lib/                   # Librerías personalizadas (vacío)
├── include/               # Headers (vacío)
└── test/                  # Tests (vacío)
```

## Librerías en uso
| Librería | Versión | Propósito |
|----------|---------|-----------|
| TinyGSM | 0.12.0 | Comunicación SIM7000G |
| PubSubClient | 2.8.0 | Cliente MQTT |
| MD_MAX72xx | 3.5.1 | Control display LED |
| MD_Parola | 3.7.5 | Animaciones display |
| DHT sensor library | 1.4.7 | Sensor temperatura/humedad |
| Adafruit Unified Sensor | 1.1.15 | Sensor unified |
| ArduinoOTA | 2.0.0 | Actualizaciones OTA |
| Preferences | 2.0.0 | Almacenamiento NVS |
| WebServer | 2.0.0 | Panel web embebido |
| SPI | 2.0.0 | Comunicación SPI |

## Configuración de Pines
| Pin | Función |
|-----|---------|
| 4 | Power Key SIM7000G |
| 26 | RX Modem SIM7000G |
| 27 | TX Modem SIM7000G |
| 5 | CS Display LED Matrix |
| 12 | LED Rojo |
| 13 | LED Amarillo |
| 14 | LED Verde |
| 32 | Sensor Corriente (ACS712) |
| 34 | Sensor Voltaje Batería |
| 35 | Sensor Voltaje Panel Solar |
| 33 | Sensor DHT11 |
| 36 | Sensor NTC Temperatura Batería |

## Estados del Semáforo
```cpp
enum EstadoSemaforo {
  ESTADO_ROJO,           // configurable (default 30s)
  ESTADO_AMARILLO,       // configurable (default 5s)
  ESTADO_VERDE,          // configurable (default 25s)
  ESTADO_ROJO_PARPADEANDO  // Modo alerta
};
```

## Modos de Operación
```cpp
enum ModoOperacion {
  MODO_NORMAL,    // Todo activo
  MODO_AHORRO,    // Display apagado, publicación reducida
  MODO_MINIMO     // Solo LED rojo parpadeante, sin MQTT
};
```

## Niveles de Batería
| Nivel | Voltaje | Acción |
|-------|---------|--------|
| NORMAL | >= 12.0V | Operación normal |
| ADVERTENCIA | < 12.0V | Modo ahorro |
| BAJO | < 11.5V | Modo mínimo |
| CRÍTICO | < 11.0V | Deep sleep inmediato (10 min) |

## Topics MQTT
| Topic | Dirección | Contenido |
|-------|-----------|-----------|
| semaforo/control | Entrada | "ROJO", "VERDE", "AMARILLO", "PARPADEO" |
| semaforo/status | Salida | JSON completo con estado, señal, gps, temp |
| semaforo/solar | Salida | JSON con datos solares + temp batería |
| semaforo/gps | Salida | JSON con latitud, longitud |
| semaforo/alertas | Salida | JSON con alertas (batería, temp, OTA) |

## Payload MQTT Status
```json
{
  "estado": "VERDE",
  "senal": 25,
  "uptime": 3600,
  "bateria": 12.5,
  "panel": 18.2,
  "corriente": 0.45,
  "porcentaje": 80.0,
  "potencia": 5.6,
  "tempBat": 28.5,
  "temperatura": 25.0,
  "humedad": 65,
  "lat": -34.603723,
  "lon": -58.381594
}
```

## Configuración Solar
- **Divisor Batería**: R1=100K, R2=100K (factor 2.0)
- **Divisor Panel**: R1=470K, R2=100K (factor 5.7)
- **Sensor Corriente**: ACS712 5A (sensibilidad 0.185 V/A)
- **Rango Batería**: 10.5V (0%) a 13.0V (100%)

## Configuración NTC Batería
- **Beta**: 3950
- **R serie**: 10KΩ
- **R referencia**: 10KΩ @ 25°C
- **Rango seguro**: 0°C a 45°C

## APN Configurados (Claro Argentina)
1. internet.claro.com.ar
2. wap.claro.com.ar
3. igprs.claro

## Funciones Principales
### Sensores
- `leerDatosSolar()` - Lee voltaje batería, panel, corriente (con promedio ADC)
- `leerDatosAmbiente()` - Lee DHT11
- `leerNTCTemperatura()` - Lee temperatura batería (NTC)
- `obtenerGPS()` - Obtiene coordenadas GPS

### Gestión de Energía
- `evaluarNivelBateria()` - Evalúa estado de batería
- `evaluarTemperaturaBateria()` - Evalúa temp batería (NTC)
- `gestionarBateria()` - Gestiona modos según niveles
- `entrarDeepSleep()` - Entra en deep sleep

### Configuración
- `cargarConfig()` - Carga config desde NVS
- `guardarConfig()` - Guarda config en NVS

### MQTT
- `conectarGPRS()` - Conecta a internet (con backoff exponencial)
- `conectarMQTT()` - Conecta al broker (con backoff exponencial)
- `publicarEstado()` - Envía estado completo por MQTT
- `publicarDatosSolar()` - Envía datos solares por MQTT
- `publicarGPS()` - Envía coordenadas GPS
- `publicarAlerta()` - Envía alertas

### Display y Control
- `actualizarSemaforo()` - Controla LEDs y tiempos
- `mostrarEstadoSemaforo()` - Muestra estado en display
- `mostrarInfoSolar()` - Muestra datos solares en display
- `mostrarInfoAmbiente()` - Muestra temp/humedad en display

### Sistema
- `iniciarWatchdog()` - Inicia watchdog timer (60s)
- `alimentarWatchdog()` - Resetea watchdog
- `iniciarOTA()` - Inicia servicio OTA
- `iniciarWebServer()` - Inicia panel web

## Web Server Endpoints
| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/` | GET | Dashboard HTML |
| `/api` | GET | JSON con todos los datos |
| `/control?cmd=X` | GET | Cambiar estado semáforo |
| `/reset` | GET | Guardar config y reiniciar |

## Tiempos del Loop Principal
- Lectura sensores solares: cada 5 segundos (30s en modo mínimo)
- Lectura sensores ambiente: cada 5 segundos
- Publicación MQTT: cada 30 segundos
- Actualización display: cada 3 segundos
- Debug serial: cada 10 segundos (30s en modo mínimo)
- Publicación GPS: cada 5 minutos

## Deep Sleep
| Condición | Duración |
|-----------|----------|
| Batería crítica | 600 segundos |
| Batería baja sin sol | 300 segundos |
| Temperatura extrema | 300 segundos |

## Uso de Memoria
- **RAM**: ~34KB (10.3%)
- **Flash**: ~537KB (41.0%)

## Compilación
```bash
pio run                    # Compilar
pio run --target upload    # Subir a ESP32
pio device monitor          # Monitor serial
```

## OTA
- **Host**: Configurado automáticamente
- **Puerto**: 3232
- **Password**: semaforo123
- **Path actualización**: /update

## Configuración Persistente (NVS)
- `tiempoRojo` - Duración estado rojo (ms)
- `tiempoVerde` - Duración estado verde (ms)
- `tiempoAmarillo` - Duración estado amarillo (ms)
- `batCritica` - Umbral batería crítica (V)
- `batBaja` - Umbral batería baja (V)
- `batAdvertencia` - Umbral batería advertencia (V)
- `batNormal` - Umbral batería normal (V)

## RTC Memory (persiste en deep sleep)
- `wakeUpCount` - Contador de despertares
- `lastBateriaCritica` - Timestamp última crítica
- `lastGPSlat` - Última latitud conocida
- `lastGPSlon` - Última longitud conocida
