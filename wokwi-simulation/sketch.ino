#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


const char* SSID     = "Wokwi-GUEST";
const char* PASSWORD = "";
const char* API_URL  = "http://unrevolving-lauren-nonpermeable.ngrok-free.dev/sensorData";

const char* SENSOR_ID   = "SN-TH-001";
const char* SENSOR_TYPE = "temperatura";
const char* READ_TYPE   = "analog";

#define SENSOR_PIN A0 

void setup() {
  Serial1.begin(115200);
  delay(1000);

  Serial1.println("Iniciando...");

  WiFi.begin(SSID, PASSWORD);
  Serial1.print("Conectando ao Wi-Fi");

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial1.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial1.println("\nConectado! IP: " + WiFi.localIP().toString());
  } else {
    Serial1.println("\nFalha na conexão Wi-Fi");
  }
}

String getTimestamp() {
  return "2024-06-01T12:00:00Z";
}

float lerSensor() {
  int raw = analogRead(SENSOR_PIN);
  float voltage = raw * (3.3f / 1023.0f);
  float temperatura = (voltage - 0.5f) * 100.0f;
  return temperatura;
}

bool enviarDados(float valor) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("Sem Wi-Fi, pulando envio");
    return false;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  StaticJsonDocument<256> doc;
  doc["idSensor"]   = SENSOR_ID;
  doc["timestamp"]  = getTimestamp();
  doc["sensorType"] = SENSOR_TYPE;
  doc["readType"]   = READ_TYPE;
  doc["value"]      = valor;

  String payload;
  serializeJson(doc, payload);

  Serial1.println("Enviando: " + payload);

  http.setTimeout(10000);
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial1.printf("Resposta HTTP: %d\n", httpCode);
    Serial1.println("Body: " + http.getString());
    http.end();
    return (httpCode == 200 || httpCode == 201);
  } else {
    Serial1.printf("Erro HTTP: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }
}

void loop() {
  float temperatura = lerSensor();
  Serial1.printf("Temperatura lida: %.2f C\n", temperatura);

  bool ok = enviarDados(temperatura);
  Serial1.println(ok ? "Enviado com sucesso!" : "Falha no envio");

  delay(30000);
}