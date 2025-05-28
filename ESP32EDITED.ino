#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

const char* ssid = "ESP32_SensorsAP";
const char* password = "12345678";

WebServer server(80);

// Pines
const int ptc1Pin = 33;
const int fanPin  = 25;

// Sensor config
struct SensorConfig {
  const char* name;
  int pin;
  float nominalResistance;
  float beta;
  float nominalTemperature;
  float temperature;
  String state;
};

SensorConfig sensors[] = {
  {"570-080A", 36, 10000.0, 3892.0, 25.0, 0.0, ""},
  {"570-142A", 39, 100000.0, 3892.0, 25.0, 0.0, ""},
  {"PSX2560-9", 34, 10000.0, 3892.0, 25.0, 0.0, ""},
  {"PSX1584-11", 35, 10000.0, 3892.0, 25.0, 0.0, ""}
};

// Estados
bool ptc1Enabled = false;
bool ptc1Blocked = false;
bool cycleRunning = false;
bool inCooldown = false;
bool fanEnabled = false;

unsigned long ptc1StartTime = 0;
unsigned long cooldownStartTime = 0;
unsigned long previousSensorMillis = 0;

const unsigned long ptcOnTime = 45000;
const unsigned long ptcCooldown = 45000;
const unsigned long sensorInterval = 1000;

float calculateTemperature(int rawValue, float nominalResistance, float beta, float nominalTemperature) {
  float voltage = rawValue * 3.3 / 4095.0;
  if (voltage <= 0.01) return -100.0;
  float resistance = nominalResistance * (3.3 / voltage - 1.0);
  float temperatureK = 1.0 / (log(resistance / nominalResistance) / beta + 1.0 / (nominalTemperature + 273.15));
  return temperatureK - 273.15;
}

String getSensorState(float temperature) {
  if (temperature < 25) return "Frio";
  if (temperature > 45) return "Caliente";
  return "Normal";
}

int analogReadAverage(int pin) {
  const int numSamples = 10;
  int total = 0;
  for (int i = 0; i < numSamples; i++) {
    total += analogRead(pin);
    delay(5);
  }
  return total / numSamples;
}

void updateSensors() {
  for (int i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
    int rawValue = analogReadAverage(sensors[i].pin);
    float temp = calculateTemperature(rawValue, sensors[i].nominalResistance, sensors[i].beta, sensors[i].nominalTemperature);
    sensors[i].temperature = temp;
    sensors[i].state = getSensorState(temp);
  }
}

float getMainSensorTemperature() {
  return sensors[0].temperature;
}

void handleStart() {
  float temp = getMainSensorTemperature();
  if (!cycleRunning && !ptc1Blocked && temp <= 30.0) {
    cycleRunning = true;
    ptc1Enabled = true;
    ptc1StartTime = millis();
    server.send(200, "text/plain", "✅ Ciclo PTC iniciado");
  } else {
    server.send(200, "text/plain", "⛔ No se puede iniciar el PTC (temp > 30°C o ciclo bloqueado)");
  }
}

void handleStop() {
  ptc1Enabled = false;
  cycleRunning = false;
  inCooldown = false;
  server.send(200, "text/plain", "🔴 Ciclo PTC detenido manualmente");
}

void handleFanOn() {
  fanEnabled = true;
  server.send(200, "text/plain", "🌀 Abanico encendido");
}

void handleFanOff() {
  fanEnabled = false;
  server.send(200, "text/plain", "🌀 Abanico apagado");
}

void handleStatus() {
  String json = "{";
  json += "\"ptcEnabled\": " + String(ptc1Enabled ? "true" : "false") + ",";
  json += "\"fanEnabled\": " + String(fanEnabled ? "true" : "false") + ",";
  json += "\"blocked\": " + String(ptc1Blocked ? "true" : "false") + ",";
  json += "\"sensors\": {";
  for (int i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
    json += "\"" + String(sensors[i].name) + "\": {";
    json += "\"temperature\": " + String(sensors[i].temperature, 2) + ", ";
    json += "\"state\": \"" + sensors[i].state + "\"}";
    if (i < sizeof(sensors) / sizeof(sensors[0]) - 1) json += ", ";
  }
  json += "}}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(ptc1Pin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  digitalWrite(ptc1Pin, LOW); // ✅ Arranca apagado
  digitalWrite(fanPin, LOW);  // ✅ Arranca apagado

  for (auto& sensor : sensors) {
    pinMode(sensor.pin, INPUT);
  }

  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/fan/on", handleFanOn);
  server.on("/fan/off", handleFanOff);
  server.on("/status", handleStatus);

  server.begin();
  Serial.println("🔌 Servidor web iniciado");
}

void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  if (currentMillis - previousSensorMillis >= sensorInterval) {
    previousSensorMillis = currentMillis;
    updateSensors();
  }

  float temperature = getMainSensorTemperature();

  // Bloqueo por temperatura alta
  if (ptc1Enabled && temperature >= 55.0) {
    ptc1Enabled = false;
    cycleRunning = false;
    ptc1Blocked = true;
    Serial.println("🔥 PTC bloqueado por sobretemperatura");
  }

  // Fin de ciclo activo → inicia cooldown
  if (ptc1Enabled && currentMillis - ptc1StartTime >= ptcOnTime) {
    ptc1Enabled = false;
    inCooldown = true;
    cooldownStartTime = currentMillis;
    Serial.println("⏸️ Fin del ciclo. Cooldown iniciado");
  }

  // Fin de cooldown → reinicia ciclo si no está bloqueado
  if (inCooldown && currentMillis - cooldownStartTime >= ptcCooldown) {
    inCooldown = false;
    if (cycleRunning && !ptc1Blocked) {
      ptc1Enabled = true;
      ptc1StartTime = millis();
      Serial.println("🔁 Cooldown terminado. Ciclo reiniciado");
    }
  }

  // Desbloqueo automático
  if (ptc1Blocked && temperature <= 30.0) {
    ptc1Blocked = false;
    Serial.println("✅ Temperatura bajó a 30°C. Desbloqueado");
  }

  // Aplica estado real
  digitalWrite(ptc1Pin, ptc1Enabled ? HIGH : LOW);
  digitalWrite(fanPin, fanEnabled ? HIGH : LOW);
}
