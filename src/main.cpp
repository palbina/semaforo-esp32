#define TINY_GSM_MODEM_SIM7000
#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <DHT.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

#define PIN_MODEM_TX 27
#define PIN_MODEM_RX 26
#define PIN_PWRKEY 4
#define PIN_LED_RED 12
#define PIN_LED_YELLOW 13
#define PIN_LED_GREEN 14

#define PIN_ADC_BATERIA 34
#define PIN_ADC_PANEL 35
#define PIN_ADC_CURRENT 32
#define PIN_NTC_BATERIA 36

#define PIN_CS_DISPLAY 5
#define PIN_DHT 33
#define DHT_TYPE DHT11
#define MAX_DEVICES 4

#define LED_OFF HIGH
#define LED_ON LOW

#define MQTT_SERVER "broker.mqttdashboard.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "semaforo_esp32"
#define MQTT_TOPIC_CONTROL "semaforo/control"
#define MQTT_TOPIC_STATUS "semaforo/status"
#define MQTT_TOPIC_SOLAR "semaforo/solar"
#define MQTT_TOPIC_GPS "semaforo/gps"
#define MQTT_TOPIC_ALERTAS "semaforo/alertas"

#define ADC_SAMPLES 5

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
PubSubClient mqttClient(gsmClient);

MD_MAX72XX mx = MD_MAX72XX(MD_MAX72XX::FC16_HW, PIN_CS_DISPLAY, MAX_DEVICES);
MD_Parola display = MD_Parola(MD_MAX72XX::FC16_HW, PIN_CS_DISPLAY, MAX_DEVICES);
DHT dht(PIN_DHT, DHT_TYPE);
Preferences preferences;
WebServer server(80);

enum EstadoSemaforo {
  ESTADO_ROJO,
  ESTADO_AMARILLO,
  ESTADO_VERDE,
  ESTADO_ROJO_PARPADEANDO
};

EstadoSemaforo estadoActual = ESTADO_ROJO;
unsigned long ultimoCambio = 0;
unsigned long TIEMPO_ROJO = 30000;
unsigned long TIEMPO_VERDE = 25000;
unsigned long TIEMPO_AMARILLO = 5000;

bool gsmConectado = false;
bool mqttConectado = false;
bool gpsHabilitado = false;

const float VOLTAJE_REFERENCIA = 3.3;
const int ADC_RESOLUTION = 4095;
const float DIVISOR_BATERIA_R1 = 100000.0;
const float DIVISOR_BATERIA_R2 = 100000.0;
const float FACTOR_BATERIA = (DIVISOR_BATERIA_R1 + DIVISOR_BATERIA_R2) / DIVISOR_BATERIA_R2;

const float DIVISOR_PANEL_R1 = 470000.0;
const float DIVISOR_PANEL_R2 = 100000.0;
const float FACTOR_PANEL = (DIVISOR_PANEL_R1 + DIVISOR_PANEL_R2) / DIVISOR_PANEL_R2;

const float SENSIBILIDAD_ACS712_5A = 0.185;
const float CORRIENTE_SENSOR_OFFSET = 1.65;

const float NTC_BETA = 3950.0;
const float NTC_SERIE_R = 10000.0;
const float NTC_REF_TEMP = 25.0;
const float NTC_REF_RES = 10000.0;

float BATERIA_CRITICA = 11.0;
float BATERIA_BAJA = 11.5;
float BATERIA_ADVERTENCIA = 12.0;
float BATERIA_NORMAL = 12.5;

#define DEEPSLEEP_SEGUNDOS 300
#define DEEPSLEEP_CRITICO_SEGUNDOS 600

#define TEMP_BATERIA_MIN 0.0
#define TEMP_BATERIA_MAX 45.0

enum NivelBateria {
  NIVEL_NORMAL,
  NIVEL_ADVERTENCIA,
  NIVEL_BAJO,
  NIVEL_CRITICO
};

enum ModoOperacion {
  MODO_NORMAL,
  MODO_AHORRO,
  MODO_MINIMO
};

RTC_DATA_ATTR uint32_t wakeUpCount = 0;
RTC_DATA_ATTR uint32_t lastBateriaCritica = 0;
RTC_DATA_ATTR uint32_t lastGPSlat = 0;
RTC_DATA_ATTR uint32_t lastGPSlon = 0;

NivelBateria nivelBateria = NIVEL_NORMAL;
ModoOperacion modoOperacion = MODO_NORMAL;

float temperaturaBateria = 25.0;

struct DatosSolar {
  float voltajeBateria;
  float voltajePanel;
  float corriente;
  float porcentajeBateria;
  float potencia;
};

struct DatosAmbiente {
  float temperatura;
  float humedad;
};

struct DatosGPS {
  float latitud;
  float longitud;
  bool valido;
};

DatosSolar datosSolar = {0, 0, 0, 0, 0};
DatosAmbiente datosAmbiente = {0, 0};
DatosGPS datosGPS = {0, 0, false};

int obtenerRetryExponencial(int intento, int baseMs = 1000, int maxMs = 60000) {
  int delayTime = baseMs * pow(2, intento);
  return min(delayTime, maxMs);
}

int analogReadPromedio(int pin) {
  long suma = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    suma += analogRead(pin);
    delayMicroseconds(100);
  }
  return suma / ADC_SAMPLES;
}

float leerNTCTemperatura() {
  int rawADC = analogReadPromedio(PIN_NTC_BATERIA);
  float voltaje = (rawADC * VOLTAJE_REFERENCIA) / ADC_RESOLUTION;
  float resistencia = NTC_SERIE_R * (voltaje / (VOLTAJE_REFERENCIA - voltaje));
  float temperatura = (1.0 / ((log(resistencia / NTC_REF_RES) / NTC_BETA) + (1.0 / (NTC_REF_TEMP + 273.15)))) - 273.15;
  return temperatura;
}

float leerVoltajeBateria() {
  int rawADC = analogReadPromedio(PIN_ADC_BATERIA);
  float voltajeADC = (rawADC * VOLTAJE_REFERENCIA) / ADC_RESOLUTION;
  return voltajeADC * FACTOR_BATERIA;
}

float leerVoltajePanel() {
  int rawADC = analogReadPromedio(PIN_ADC_PANEL);
  float voltajeADC = (rawADC * VOLTAJE_REFERENCIA) / ADC_RESOLUTION;
  return voltajeADC * FACTOR_PANEL;
}

float leerCorriente() {
  int rawADC = analogReadPromedio(PIN_ADC_CURRENT);
  float voltajeSensor = (rawADC * VOLTAJE_REFERENCIA) / ADC_RESOLUTION;
  float corriente = (voltajeSensor - CORRIENTE_SENSOR_OFFSET) / SENSIBILIDAD_ACS712_5A;
  return abs(corriente);
}

float calcularPorcentajeBateria(float voltaje) {
  if (voltaje >= 13.0) return 100.0;
  if (voltaje <= 10.5) return 0.0;
  return ((voltaje - 10.5) / (13.0 - 10.5)) * 100.0;
}

DatosSolar leerDatosSolar() {
  DatosSolar datos;
  
  datos.voltajeBateria = leerVoltajeBateria();
  datos.voltajePanel = leerVoltajePanel();
  datos.corriente = leerCorriente();
  datos.porcentajeBateria = calcularPorcentajeBateria(datos.voltajeBateria);
  datos.potencia = datos.voltajeBateria * datos.corriente;
  
  return datos;
}

DatosAmbiente leerDatosAmbiente() {
  DatosAmbiente datos;
  
  datos.temperatura = dht.readTemperature();
  datos.humedad = dht.readHumidity();
  
  if (isnan(datos.temperatura)) {
    datos.temperatura = 0;
  }
  if (isnan(datos.humedad)) {
    datos.humedad = 0;
  }
  
  return datos;
}

bool obtenerGPS() {
  if (!gpsHabilitado) return false;
  
  Serial.println("Obteniendo GPS...");
  
  modem.sendAT("+CGNSSPWR=1");
  delay(500);
  
  int retry = 0;
  while (retry < 10) {
    modem.sendAT("+CGNSINF");
    
    String response = "";
    if (modem.waitResponse(1000, response)) {
      if (response.indexOf("+CGNSINF:") != -1 && response.indexOf(",1,") != -1) {
        int firstComma = response.indexOf(",");
        int secondComma = response.indexOf(",", firstComma + 1);
        int thirdComma = response.indexOf(",", secondComma + 1);
        
        if (firstComma > 0 && secondComma > 0 && thirdComma > 0) {
          datosGPS.latitud = response.substring(firstComma + 1, secondComma).toFloat();
          datosGPS.longitud = response.substring(secondComma + 1, thirdComma).toFloat();
          datosGPS.valido = true;
          
          Serial.printf("GPS: %.6f, %.6f\n", datosGPS.latitud, datosGPS.longitud);
          return true;
        }
      }
    }
    retry++;
    delay(1000);
  }
  
  datosGPS.valido = false;
  return false;
}

NivelBateria evaluarNivelBateria(float voltaje) {
  if (voltaje < BATERIA_CRITICA) return NIVEL_CRITICO;
  if (voltaje < BATERIA_BAJA) return NIVEL_BAJO;
  if (voltaje < BATERIA_ADVERTENCIA) return NIVEL_ADVERTENCIA;
  return NIVEL_NORMAL;
}

bool evaluarTemperaturaBateria() {
  temperaturaBateria = leerNTCTemperatura();
  
  if (temperaturaBateria < TEMP_BATERIA_MIN || temperaturaBateria > TEMP_BATERIA_MAX) {
    Serial.printf("ALERTA: Temperatura bateria fuera de rango: %.1fC\n", temperaturaBateria);
    return true;
  }
  return false;
}

void guardarConfig() {
  preferences.begin("semaforo", false);
  preferences.putFloat("tiempoRojo", TIEMPO_ROJO);
  preferences.putFloat("tiempoVerde", TIEMPO_VERDE);
  preferences.putFloat("tiempoAmarillo", TIEMPO_AMARILLO);
  preferences.putFloat("batCritica", BATERIA_CRITICA);
  preferences.putFloat("batBaja", BATERIA_BAJA);
  preferences.putFloat("batAdvertencia", BATERIA_ADVERTENCIA);
  preferences.putFloat("batNormal", BATERIA_NORMAL);
  preferences.end();
  Serial.println("Configuracion guardada en NVS");
}

void cargarConfig() {
  preferences.begin("semaforo", true);
  TIEMPO_ROJO = preferences.getFloat("tiempoRojo", 30000);
  TIEMPO_VERDE = preferences.getFloat("tiempoVerde", 25000);
  TIEMPO_AMARILLO = preferences.getFloat("tiempoAmarillo", 5000);
  BATERIA_CRITICA = preferences.getFloat("batCritica", 11.0);
  BATERIA_BAJA = preferences.getFloat("batBaja", 11.5);
  BATERIA_ADVERTENCIA = preferences.getFloat("batAdvertencia", 12.0);
  BATERIA_NORMAL = preferences.getFloat("batNormal", 12.5);
  preferences.end();
  Serial.println("Configuracion cargada desde NVS");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Semafoto IoT</title>";
  html += "<style>body{font-family:Arial;padding:20px;background:#f0f0f0}";
  html += "h1{color:#333}.card{background:#fff;padding:15px;border-radius:8px;margin:10px 0;box-shadow:0 2px 5px rgba(0,0,0,0.1)}";
  html += ".status{font-size:24px;font-weight:bold}.ok{color:#4CAF50}.warn{color:#ff9800}.crit{color:#f44336}";
  html += "table{width:100%;border-collapse:collapse}td{padding:8px;border-bottom:1px solid #ddd}";
  html += ".btn{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}";
  html += ".btn:hover{background:#45a049}</style></head><body>";
  html += "<h1>Semafoto IoT</h1>";
  
  const char* modoStr = (modoOperacion == MODO_NORMAL) ? "NORMAL" : (modoOperacion == MODO_AHORRO) ? "AHORRO" : "MINIMO";
  const char* nivelStr = (nivelBateria == NIVEL_NORMAL) ? "OK" : (nivelBateria == NIVEL_ADVERTENCIA) ? "ADVERTENCIA" : (nivelBateria == NIVEL_BAJO) ? "BAJA" : "CRITICO";
  const char* nivelClass = (nivelBateria == NIVEL_NORMAL) ? "ok" : (nivelBateria == NIVEL_ADVERTENCIA) ? "warn" : "crit";
  const char* estadoStr = (estadoActual == ESTADO_ROJO) ? "ROJO" : (estadoActual == ESTADO_VERDE) ? "VERDE" : (estadoActual == ESTADO_AMARILLO) ? "AMARILLO" : "PARPADEANDO";
  
  html += "<div class='card'><h2>Estado</h2>";
  html += "<p class='status'>Modo: " + String(modoStr) + "</p>";
  html += "<p class='status'>Semaforo: " + String(estadoStr) + "</p>";
  html += "<p class='status " + String(nivelClass) + "'>Bateria: " + String(nivelStr) + "</p>";
  html += "<p>Wake Count: " + String(wakeUpCount) + "</p></div>";
  
  html += "<div class='card'><h2>Datos</h2>";
  html += "<table><tr><td>Bateria</td><td>" + String(datosSolar.voltajeBateria, 2) + "V (" + String(datosSolar.porcentajeBateria, 1) + "%)</td></tr>";
  html += "<tr><td>Panel Solar</td><td>" + String(datosSolar.voltajePanel, 2) + "V</td></tr>";
  html += "<tr><td>Corriente</td><td>" + String(datosSolar.corriente, 3) + "A</td></tr>";
  html += "<tr><td>Temperatura Bateria</td><td>" + String(temperaturaBateria, 1) + "C</td></tr>";
  html += "<tr><td>Temperatura Ambiente</td><td>" + String(datosAmbiente.temperatura, 1) + "C</td></tr>";
  html += "<tr><td>Humedad</td><td>" + String(datosAmbiente.humedad, 0) + "%</td></tr>";
  if (datosGPS.valido) {
    html += "<tr><td>GPS</td><td>" + String(datosGPS.latitud, 6) + ", " + String(datosGPS.longitud, 6) + "</td></tr>";
  }
  html += "</table></div>";
  
  html += "<div class='card'><h2>Control</h2>";
  html += "<a href='/control?cmd=ROJO'><button class='btn'>ROJO</button></a>";
  html += "<a href='/control?cmd=VERDE'><button class='btn'>VERDE</button></a>";
  html += "<a href='/control?cmd=AMARILLO'><button class='btn'>AMARILLO</button></a>";
  html += "<a href='/control?cmd=PARPADEO'><button class='btn'>PARPADEO</button></a>";
  html += "<a href='/reset'><button class='btn' style='background:#f44336'>RESET</button></a>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    if (cmd == "ROJO") {
      estadoActual = ESTADO_ROJO;
      ultimoCambio = millis();
    } else if (cmd == "VERDE") {
      estadoActual = ESTADO_VERDE;
      ultimoCambio = millis();
    } else if (cmd == "AMARILLO") {
      estadoActual = ESTADO_AMARILLO;
      ultimoCambio = millis();
    } else if (cmd == "PARPADEO") {
      estadoActual = ESTADO_ROJO_PARPADEANDO;
    }
    Serial.println("Control web: " + cmd);
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleReset() {
  guardarConfig();
  server.send(200, "text/plain", "Config guardada, reiniciando...");
  delay(1000);
  ESP.restart();
}

void handleApi() {
  String json = "{";
  json += "\"estado\":\"" + String((estadoActual == ESTADO_ROJO) ? "ROJO" : (estadoActual == ESTADO_VERDE) ? "VERDE" : (estadoActual == ESTADO_AMARILLO) ? "AMARILLO" : "PARPADEANDO") + "\",";
  json += "\"modo\":\"" + String((modoOperacion == MODO_NORMAL) ? "NORMAL" : (modoOperacion == MODO_AHORRO) ? "AHORRO" : "MINIMO") + "\",";
  json += "\"bateria\":" + String(datosSolar.voltajeBateria, 2) + ",";
  json += "\"porcentaje\":" + String(datosSolar.porcentajeBateria, 1) + ",";
  json += "\"panel\":" + String(datosSolar.voltajePanel, 2) + ",";
  json += "\"corriente\":" + String(datosSolar.corriente, 3) + ",";
  json += "\"tempBateria\":" + String(temperaturaBateria, 1) + ",";
  json += "\"tempAmbiente\":" + String(datosAmbiente.temperatura, 1) + ",";
  json += "\"humedad\":" + String(datosAmbiente.humedad, 0) + ",";
  json += "\"wakeUpCount\":" + String(wakeUpCount);
  json += "}";
  server.send(200, "application/json", json);
}

void iniciarWebServer() {
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/reset", handleReset);
  server.on("/api", handleApi);
  server.begin();
  Serial.println("Web server iniciado en http://" + WiFi.localIP().toString());
}

void iniciarOTA() {
  ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  ArduinoOTA.setPassword("semaforo123");
  
  ArduinoOTA.onStart([]() {
    Serial.println("OTA: Inicio actualizacion");
    if (mqttConectado) {
      mqttClient.publish(MQTT_TOPIC_ALERTAS, "{\"tipo\":\"OTA\",\"estado\":\"INICIO\"}");
    }
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA: Actualizacion completada");
    if (mqttConectado) {
      mqttClient.publish(MQTT_TOPIC_ALERTAS, "{\"tipo\":\"OTA\",\"estado\":\"FIN\"}");
    }
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (modoOperacion != MODO_MINIMO) {
      Serial.printf("OTA: %u%%\n", (progress / (total / 100)));
    }
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error: %d\n", error);
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA habilitado");
}

void iniciarWatchdog() {
  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog timer iniciado (60s)");
}

void alimentarWatchdog() {
  esp_task_wdt_reset();
}

void apagarModem() {
  Serial.println("Apagando modem GSM...");
  modem.gprsDisconnect();
  delay(500);
  modem.poweroff();
  delay(1000);
  SerialAT.end();
  pinMode(PIN_MODEM_RX, INPUT);
  pinMode(PIN_MODEM_TX, INPUT);
  Serial.println("Modem apagado");
}

void apagarDisplay() {
  display.displayClear();
  display.setIntensity(0);
  Serial.println("Display apagado");
}

void entrarDeepSleep(uint32_t segundosSleep) {
  Serial.printf("Entrando en deep sleep por %u segundos...\n", segundosSleep);
  
  apagarModem();
  apagarDisplay();
  
  digitalWrite(PIN_LED_RED, LED_OFF);
  digitalWrite(PIN_LED_YELLOW, LED_OFF);
  digitalWrite(PIN_LED_GREEN, LED_OFF);
  
  esp_sleep_enable_timer_wakeup(segundosSleep * 1000000ULL);
  
  Serial.flush();
  esp_deep_sleep_start();
}

void publicarAlerta(const char* tipo, const char* mensaje) {
  char payload[200];
  snprintf(payload, sizeof(payload), 
    "{\"tipo\":\"%s\",\"mensaje\":\"%s\",\"bateria\":%.2f,\"tempBat\":%.1f,\"wakeCount\":%u}",
    tipo, mensaje, datosSolar.voltajeBateria, temperaturaBateria, wakeUpCount
  );
  
  mqttClient.publish(MQTT_TOPIC_ALERTAS, payload);
  Serial.println("Alerta publicada: " + String(payload));
}

void publicarEstado() {
  String estadoStr;
  switch (estadoActual) {
    case ESTADO_ROJO: estadoStr = "ROJO"; break;
    case ESTADO_VERDE: estadoStr = "VERDE"; break;
    case ESTADO_AMARILLO: estadoStr = "AMARILLO"; break;
    case ESTADO_ROJO_PARPADEANDO: estadoStr = "PARPADEANDO"; break;
  }
  
  char payload[300];
  snprintf(payload, sizeof(payload), 
    "{\"estado\":\"%s\",\"senal\":%d,\"uptime\":%lu,\"bateria\":%.2f,\"panel\":%.2f,\"corriente\":%.2f,\"porcentaje\":%.1f,\"potencia\":%.2f,\"tempBat\":%.1f,\"temperatura\":%.1f,\"humedad\":%.0f,\"lat\":%.6f,\"lon\":%.6f}",
    estadoStr.c_str(), 
    modem.getSignalQuality(),
    millis() / 1000,
    datosSolar.voltajeBateria,
    datosSolar.voltajePanel,
    datosSolar.corriente,
    datosSolar.porcentajeBateria,
    datosSolar.potencia,
    temperaturaBateria,
    datosAmbiente.temperatura,
    datosAmbiente.humedad,
    datosGPS.latitud,
    datosGPS.longitud
  );
  
  mqttClient.publish(MQTT_TOPIC_STATUS, payload);
  Serial.println("Estado publicado");
}

void publicarGPS() {
  if (!datosGPS.valido) return;
  
  char payload[150];
  snprintf(payload, sizeof(payload),
    "{\"lat\":%.6f,\"lon\":%.6f,\"uptime\":%lu}",
    datosGPS.latitud,
    datosGPS.longitud,
    millis() / 1000
  );
  
  mqttClient.publish(MQTT_TOPIC_GPS, payload);
}

bool verificarRecuperacionBateria() {
  if (nivelBateria == NIVEL_CRITICO || nivelBateria == NIVEL_BAJO) {
    if (datosSolar.voltajeBateria >= BATERIA_NORMAL) {
      Serial.println("Bateria recuperada a nivel normal");
      return true;
    }
  }
  return false;
}

void gestionarBateria() {
  nivelBateria = evaluarNivelBateria(datosSolar.voltajeBateria);
  
  if (evaluarTemperaturaBateria()) {
    if (mqttConectado) {
      publicarAlerta("TEMP_BATERIA", "Temperatura fuera de rango");
    }
    if (temperaturaBateria < TEMP_BATERIA_MIN || temperaturaBateria > TEMP_BATERIA_MAX + 10) {
      Serial.println("Temperatura critica, entrando deep sleep...");
      delay(2000);
      entrarDeepSleep(DEEPSLEEP_SEGUNDOS);
    }
  }
  
  if (modoOperacion == MODO_NORMAL && nivelBateria >= NIVEL_BAJO) {
    modoOperacion = MODO_AHORRO;
    Serial.println("Cambiando a modo AHORRO");
    if (mqttConectado) {
      publicarAlerta("MODO_CAMBIO", "Cambiando a modo AHORRO");
    }
  }
  
  if (modoOperacion == MODO_AHORRO && nivelBateria == NIVEL_NORMAL) {
    if (verificarRecuperacionBateria()) {
      modoOperacion = MODO_NORMAL;
      Serial.println(" Recuperando modo NORMAL");
      if (mqttConectado) {
        publicarAlerta("MODO_CAMBIO", "Recuperando modo NORMAL");
      }
    }
  }
  
  switch (nivelBateria) {
    case NIVEL_CRITICO:
      Serial.printf("ALERTA: Bateria CRITICA (%.2fV) - Deep sleep inmediato\n", datosSolar.voltajeBateria);
      if (mqttConectado) {
        publicarAlerta("BATERIA_CRITICA", "Entrada a deep sleep critico");
      }
      delay(1000);
      entrarDeepSleep(DEEPSLEEP_CRITICO_SEGUNDOS);
      break;
      
    case NIVEL_BAJO:
      Serial.printf("ALERTA: Bateria BAJA (%.2fV)\n", datosSolar.voltajeBateria);
      modoOperacion = MODO_MINIMO;
      if (mqttConectado) {
        publicarAlerta("BATERIA_BAJA", "Modo minimo activado");
      }
      if (datosSolar.voltajePanel < 5.0) {
        Serial.println("Panel sin carga, entrando deep sleep...");
        delay(2000);
        entrarDeepSleep(DEEPSLEEP_SEGUNDOS);
      }
      break;
      
    case NIVEL_ADVERTENCIA:
      Serial.printf("ADVERTENCIA: Bateria baja (%.2fV)\n", datosSolar.voltajeBateria);
      modoOperacion = MODO_AHORRO;
      break;
      
    case NIVEL_NORMAL:
      modoOperacion = MODO_NORMAL;
      break;
  }
}

void iniciarDisplay() {
  mx.begin();
  display.begin();
  display.setIntensity(7);
  display.setInvert(false);
  display.displayClear();
  display.displayScroll("SEMAFORO IoT", PA_CENTER, PA_SCROLL_LEFT, 50);
  delay(2000);
  display.displayClear();
  Serial.println("Display MAX7219 inicializado");
}

void mostrarEnDisplay(const char* texto) {
  if (display.displayAnimate()) {
    display.displayClear();
    display.displayScroll(texto, PA_CENTER, PA_SCROLL_LEFT, 50);
  }
}

void mostrarEstadoSemaforo() {
  switch (estadoActual) {
    case ESTADO_ROJO:
      mostrarEnDisplay("PARAR");
      break;
    case ESTADO_VERDE:
      mostrarEnDisplay("AVANZAR");
      break;
    case ESTADO_AMARILLO:
      mostrarEnDisplay("PRECAUCION");
      break;
    case ESTADO_ROJO_PARPADEANDO:
      mostrarEnDisplay("ALERTA");
      break;
  }
}

void mostrarInfoSolar() {
  char buffer[100];
  
  if (display.displayAnimate()) {
    display.displayClear();
    snprintf(buffer, sizeof(buffer), "Bat:%.1f%% %.1fV", 
      datosSolar.porcentajeBateria, datosSolar.voltajeBateria);
    display.displayScroll(buffer, PA_CENTER, PA_SCROLL_UP, 40);
  }
}

void mostrarInfoAmbiente() {
  char buffer[100];
  
  if (display.displayAnimate()) {
    display.displayClear();
    snprintf(buffer, sizeof(buffer), "T:%.1fC H:%.0f%%", 
      datosAmbiente.temperatura, datosAmbiente.humedad);
    display.displayScroll(buffer, PA_CENTER, PA_SCROLL_UP, 40);
  }
}

void iniciarModem() {
  SerialAT.begin(115200, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  
  pinMode(PIN_PWRKEY, OUTPUT);
  digitalWrite(PIN_PWRKEY, LOW);
  delay(100);
  digitalWrite(PIN_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(PIN_PWRKEY, LOW);
  
  Serial.println("Esperando modem SIM7000G...");
  
  int retry = 0;
  while (!modem.testAT(1000) && retry < 30) {
    Serial.print(".");
    retry++;
    delay(500);
  }
  Serial.println();
  
  if (retry >= 30) {
    Serial.println("ERROR: No se detecto el modem");
    return;
  }
  
  Serial.println("Modem detectado, inicializando...");
  modem.init();
  
  String modelo = modem.getModemName();
  Serial.println("Modem: " + modelo);
  
  Serial.println("Esperando senal GSM...");
  retry = 0;
  while (modem.getSignalQuality() < 5 && retry < 60) {
    delay(1000);
    retry++;
    Serial.print(".");
  }
  Serial.println();
  
  int signal = modem.getSignalQuality();
  Serial.printf("Nivel de senal: %d/31\n", signal);
  
  if (signal < 5) {
    Serial.println("ERROR: Senal muy debil");
    return;
  }
  
  gsmConectado = true;
  Serial.println("Modem GSM conectado correctamente");
}

bool conectarGPRS() {
  Serial.println("Configurando GPRS...");
  
  modem.sendAT("+COPS=0");
  delay(2000);
  
  modem.sendAT("+CNMP=38");
  delay(1000);
  
  const char* apns[] = {"internet.claro.com.ar", "wap.claro.com.ar", "igprs.claro"};
  
  for (int i = 0; i < 3; i++) {
    Serial.printf("Intentando APN: %s...\n", apns[i]);
    if (modem.gprsConnect(apns[i])) {
      Serial.println("GPRS conectado");
      return true;
    }
    
    if (i < 2) {
      int espera = obtenerRetryExponencial(i);
      Serial.printf("Reintentando en %d ms...\n", espera);
      delay(espera);
    }
  }
  
  Serial.println("ERROR: No se pudo conectar GPRS");
  return false;
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje MQTT en topic: ");
  Serial.println(topic);
  
  char mensaje[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    mensaje[i] = (char)payload[i];
  }
  mensaje[length] = '\0';
  
  Serial.println("Mensaje: " + String(mensaje));
  
  if (strcmp(topic, MQTT_TOPIC_CONTROL) == 0) {
    if (strcmp(mensaje, "ROJO") == 0) {
      estadoActual = ESTADO_ROJO;
      ultimoCambio = millis();
    } else if (strcmp(mensaje, "VERDE") == 0) {
      estadoActual = ESTADO_VERDE;
      ultimoCambio = millis();
    } else if (strcmp(mensaje, "AMARILLO") == 0) {
      estadoActual = ESTADO_AMARILLO;
      ultimoCambio = millis();
    } else if (strcmp(mensaje, "PARPADEO") == 0) {
      estadoActual = ESTADO_ROJO_PARPADEANDO;
    }
  }
}

bool conectarMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callbackMQTT);
  
  Serial.print("Conectando a MQTT broker...");
  
  int intento = 0;
  while (!mqttClient.connect(MQTT_CLIENT_ID) && intento < 5) {
    Serial.print(".");
    int espera = obtenerRetryExponencial(intento, 1000, 30000);
    delay(min(espera, 5000));
    intento++;
  }
  Serial.println();
  
  if (!mqttClient.connected()) {
    Serial.println("ERROR: Fallo conexion MQTT");
    return false;
  }
  
  mqttClient.subscribe(MQTT_TOPIC_CONTROL);
  mqttConectado = true;
  Serial.println("MQTT conectado y suscripto a: " + String(MQTT_TOPIC_CONTROL));
  return true;
}

void publicarDatosSolar() {
  char payload[200];
  snprintf(payload, sizeof(payload),
    "{\"vbateria\":%.2f,\"vpanel\":%.2f,\"corriente\":%.3f,\"porcentaje\":%.1f,\"potencia\":%.2f,\"tempBat\":%.1f,\"uptime\":%lu}",
    datosSolar.voltajeBateria,
    datosSolar.voltajePanel,
    datosSolar.corriente,
    datosSolar.porcentajeBateria,
    datosSolar.potencia,
    temperaturaBateria,
    millis() / 1000
  );
  
  mqttClient.publish(MQTT_TOPIC_SOLAR, payload);
}

void actualizarSemaforo() {
  digitalWrite(PIN_LED_RED, LED_OFF);
  digitalWrite(PIN_LED_YELLOW, LED_OFF);
  digitalWrite(PIN_LED_GREEN, LED_OFF);
  
  unsigned long tiempoTranscurrido = millis() - ultimoCambio;
  
  switch (estadoActual) {
    case ESTADO_ROJO:
      digitalWrite(PIN_LED_RED, LED_ON);
      if (tiempoTranscurrido >= TIEMPO_ROJO) {
        estadoActual = ESTADO_VERDE;
        ultimoCambio = millis();
      }
      break;
      
    case ESTADO_VERDE:
      digitalWrite(PIN_LED_GREEN, LED_ON);
      if (tiempoTranscurrido >= TIEMPO_VERDE) {
        estadoActual = ESTADO_AMARILLO;
        ultimoCambio = millis();
      }
      break;
      
    case ESTADO_AMARILLO:
      digitalWrite(PIN_LED_YELLOW, LED_ON);
      if (tiempoTranscurrido >= TIEMPO_AMARILLO) {
        estadoActual = ESTADO_ROJO;
        ultimoCambio = millis();
      }
      break;
      
    case ESTADO_ROJO_PARPADEANDO:
      if ((millis() / 500) % 2 == 0) {
        digitalWrite(PIN_LED_RED, LED_ON);
      } else {
        digitalWrite(PIN_LED_RED, LED_OFF);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== SEMAFORO ESP32 v3.0 ===");
  
  cargarConfig();
  
  wakeUpCount++;
  Serial.printf("Wake count: %u\n", wakeUpCount);
  
  esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();
  if (causa == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Despertado por timer (deep sleep)");
  } else if (causa == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println("Primer arranque");
  }
  
  iniciarWatchdog();
  
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  
  pinMode(PIN_ADC_BATERIA, INPUT);
  pinMode(PIN_ADC_PANEL, INPUT);
  pinMode(PIN_ADC_CURRENT, INPUT);
  pinMode(PIN_NTC_BATERIA, INPUT);
  
  analogSetAttenuation(ADC_11db);
  
  digitalWrite(PIN_LED_RED, LED_OFF);
  digitalWrite(PIN_LED_YELLOW, LED_OFF);
  digitalWrite(PIN_LED_GREEN, LED_OFF);
  
  datosSolar = leerDatosSolar();
  temperaturaBateria = leerNTCTemperatura();
  
  Serial.printf("Bateria: %.2fV (%.1f%%) | Temp: %.1fC\n", 
    datosSolar.voltajeBateria, datosSolar.porcentajeBateria, temperaturaBateria);
  Serial.printf("Panel solar: %.2fV\n", datosSolar.voltajePanel);
  
  nivelBateria = evaluarNivelBateria(datosSolar.voltajeBateria);
  
  if (nivelBateria == NIVEL_CRITICO || evaluarTemperaturaBateria()) {
    Serial.println("BATERIA/TEMP CRITICA - Deep sleep inmediato");
    delay(1000);
    entrarDeepSleep(DEEPSLEEP_CRITICO_SEGUNDOS);
  }
  
  if (nivelBateria == NIVEL_BAJO && datosSolar.voltajePanel < 5.0) {
    Serial.println("Bateria baja + sin sol - Deep sleep");
    delay(1000);
    entrarDeepSleep(DEEPSLEEP_SEGUNDOS);
  }
  
  if (nivelBateria >= NIVEL_BAJO) {
    modoOperacion = MODO_MINIMO;
  }
  
  dht.begin();
  
  iniciarDisplay();
  
  iniciarModem();
  
  if (gsmConectado) {
    if (conectarGPRS()) {
      conectarMQTT();
    }
  }
  
  if (gsmConectado && modoOperacion != MODO_MINIMO) {
    obtenerGPS();
    iniciarWebServer();
    iniciarOTA();
  }
  
  ultimoCambio = millis();
  Serial.println("Sistema iniciado");
}

void loop() {
  alimentarWatchdog();
  
  static unsigned long lastSolarRead = 0;
  unsigned long intervalRead = (modoOperacion == MODO_MINIMO) ? 30000 : 5000;
  
  if (millis() - lastSolarRead > intervalRead) {
    datosSolar = leerDatosSolar();
    datosAmbiente = leerDatosAmbiente();
    lastSolarRead = millis();
    
    gestionarBateria();
  }
  
  if (modoOperacion == MODO_NORMAL) {
    if (gsmConectado) {
      if (!mqttClient.connected()) {
        mqttConectado = false;
        int intento = 0;
        while (!conectarMQTT() && intento < 3) {
          intento++;
          delay(obtenerRetryExponencial(intento, 2000, 60000));
        }
      }
      mqttClient.loop();
      server.handleClient();
      ArduinoOTA.handle();
      
      static unsigned long lastPublish = 0;
      if (millis() - lastPublish > 30000) {
        publicarEstado();
        publicarDatosSolar();
        lastPublish = millis();
      }
      
      static unsigned long lastGPS = 0;
      if (millis() - lastGPS > 300000 && gpsHabilitado) {
        if (obtenerGPS()) {
          publicarGPS();
        }
        lastGPS = millis();
      }
    }
  }
  
  if (modoOperacion != MODO_MINIMO) {
    actualizarSemaforo();
    
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 3000) {
      static uint8_t displayMode = 0;
      displayMode = (displayMode + 1) % 3;
      
      if (displayMode == 0) {
        mostrarEstadoSemaforo();
      } else if (displayMode == 1) {
        mostrarInfoSolar();
      } else {
        mostrarInfoAmbiente();
      }
      lastDisplayUpdate = millis();
    }
  } else {
    digitalWrite(PIN_LED_RED, LED_OFF);
    digitalWrite(PIN_LED_YELLOW, LED_OFF);
    digitalWrite(PIN_LED_GREEN, LED_OFF);
    
    static unsigned long lastLowBatteryBlink = 0;
    if (millis() - lastLowBatteryBlink > 2000) {
      if ((millis() / 4000) % 2 == 0) {
        digitalWrite(PIN_LED_RED, LED_ON);
      }
      lastLowBatteryBlink = millis();
    }
  }
  
  static unsigned long lastDebug = 0;
  unsigned long debugInterval = (modoOperacion == MODO_MINIMO) ? 30000 : 10000;
  if (millis() - lastDebug > debugInterval) {
    const char* modoStr = (modoOperacion == MODO_NORMAL) ? "NORMAL" : 
                         (modoOperacion == MODO_AHORRO) ? "AHORRO" : "MINIMO";
    const char* nivelStr = (nivelBateria == NIVEL_NORMAL) ? "OK" : 
                          (nivelBateria == NIVEL_ADVERTENCIA) ? "WARN" : 
                          (nivelBateria == NIVEL_BAJO) ? "BAJA" : "CRIT";
    Serial.printf("[%lu] %s | Bat: %.2fV (%s) T:%.1fC | Modo: %s | Wake: %u\n",
      millis() / 1000,
      mqttConectado ? "MQTT OK" : "MQTT OFF",
      datosSolar.voltajeBateria,
      nivelStr,
      temperaturaBateria,
      modoStr,
      wakeUpCount
    );
    lastDebug = millis();
  }
  
  delay(50);
}
