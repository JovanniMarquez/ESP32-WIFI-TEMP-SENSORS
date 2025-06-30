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
  float filteredTemperature;
  String state;
};
 
SensorConfig sensors[] = {
  {"USP10982", 36, 10000.0, 3892.0, 25.0, 0.0, 0.0, ""},
  {"USP10976", 39, 10000.0, 3892.0, 25.0, 0.0, 0.0, ""},
  {"USP10978", 34, 10000.0, 3892.0, 25.0, 0.0, 0.0, ""},
  {"USP10973", 35, 10000.0, 3892.0, 25.0, 0.0, 0.0, ""},
  {"GS104J1K", 32, 100000.0, 3977.0, 25.0, 0.0, 0.0, ""},
  {"DC103J2F", 26, 10000.0, 3977.0, 25.0, 0.0, 0.0, ""}
};
 
// Estados
bool ptc1Enabled = false;
bool ptc1Blocked = false;
bool fanEnabled = false;
String bloqueoSensor = "";
 
unsigned long previousSensorMillis = 0;
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
  const float alpha = 0.2; // Suavizado
 
  for (int i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
    int rawValue = analogReadAverage(sensors[i].pin);
    float temp = calculateTemperature(rawValue, sensors[i].nominalResistance, sensors[i].beta, sensors[i].nominalTemperature);
 
    if (sensors[i].filteredTemperature == 0.0) {
      sensors[i].filteredTemperature = temp;
    } else {
      sensors[i].filteredTemperature = alpha * temp + (1 - alpha) * sensors[i].filteredTemperature;
    }
 
    sensors[i].temperature = sensors[i].filteredTemperature;
    sensors[i].state = getSensorState(sensors[i].temperature);
  }
}
 
float getMainSensorTemperature() {
  return sensors[0].temperature;
}
 
void handleStart() {
  if (!ptc1Blocked) {
    ptc1Enabled = true;
    server.send(200, "text/plain", "✅ PTC encendido");
  } else {
    server.send(200, "text/plain", "⛔ No se puede encender el PTC (temp > 30°C o bloqueado)");
  }
}
 
void handleStop() {
  ptc1Enabled = false;
  server.send(200, "text/plain", "🔴 PTC apagado manualmente");
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
  json += "\"blockedSensor\": \"" + bloqueoSensor + "\",";
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
  digitalWrite(ptc1Pin, LOW);
  digitalWrite(fanPin, LOW);
 
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
 
  // Verificación de bloqueo y desbloqueo
  bool anyAbove55 = false;
  bool allBelow30 = true;
 
  for (int i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
    if (sensors[i].temperature >= 55.0) {
      anyAbove55 = true;
      bloqueoSensor = sensors[i].name;
    }
    if (sensors[i].temperature > 30.0) {
      allBelow30 = false;
    }
  }
 
  if (ptc1Enabled && anyAbove55) {
    ptc1Enabled = false;
    ptc1Blocked = true;
    Serial.println("🔥 PTC bloqueado por sobretemperatura en sensor: " + bloqueoSensor);
  }
 
  if (ptc1Blocked && allBelow30) {
    ptc1Blocked = false;
    Serial.println("✅ Todos los sensores por debajo de 30°C. Desbloqueado");
  }
 
  digitalWrite(ptc1Pin, ptc1Enabled ? HIGH : LOW);
  digitalWrite(fanPin, fanEnabled ? HIGH : LOW);
}