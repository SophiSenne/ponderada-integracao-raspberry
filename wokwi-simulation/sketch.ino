#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char* SSID     = "Wokwi-GUEST";
const char* PASSWORD = "";
const char* API_URL  = "http://unrevolving-lauren-nonpermeable.ngrok-free.dev/sensorData";

const char* SENSOR_ID_TEMP   = "SN-TH-001";
const char* SENSOR_TYPE_TEMP = "temperatura";
const char* READ_TYPE_TEMP   = "analog";

const char* SENSOR_ID_PIR   = "SN-PIR-001";
const char* SENSOR_TYPE_PIR = "presença";
const char* READ_TYPE_PIR   = "discrete";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600;
const int   daylightOffset_sec = 0;

#define SENSOR_PIN     A0
#define PIR_PIN        15

void setup() {
  Serial1.begin(115200);
  pinMode(PIR_PIN, INPUT);
  delay(1000);

  Serial1.println("Iniciando");

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
    configTime(10, gmtOffset_sec, ntpServer, "time.nist.gov");
  } else {
    Serial1.println("\nFalha na conexão Wi-Fi");
  }

  Serial1.println("Sincronizando NTP");
  time_t now = time(nullptr);
  int i = 0;

  while (now < 1000000000 && i < 20) { 
    delay(500);
    now = time(nullptr);
    i++;
  }

  if (now < 1000000000) {
    Serial1.println("\nFalha ao sincronizar NTP");
  } else {
    Serial1.println("\nNTP sincronizado!");
  }

}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  if (timeinfo == nullptr) {
    return "1970-01-01T00:00:00";
  }

  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  return String(buf) + "-03:00";
}

float lerTemperatura() {
  int raw = analogRead(SENSOR_PIN);
  float voltage = raw * (3.3f / 1023.0f);
  float temperatura = (voltage - 0.5f) * 100.0f;
  return temperatura;
}

bool lerPresenca() {
  return digitalRead(PIR_PIN) == HIGH;
}

bool enviarDadosTemperatura(float valor) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("Sem Wi-Fi, pulando envio");
    return false;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  StaticJsonDocument<256> doc;
  doc["idSensor"]   = SENSOR_ID_TEMP;
  doc["timestamp"]  = getTimestamp();
  doc["sensorType"] = SENSOR_TYPE_TEMP;
  doc["readType"]   = READ_TYPE_TEMP;
  doc["value"]      = valor;

  String payload;
  serializeJson(doc, payload);

  Serial1.println("Enviando temperatura: " + payload);

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

bool enviarDadosPresenca(bool presenca) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("Sem Wi-Fi, pulando envio");
    return false;
  }

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  StaticJsonDocument<256> doc;
  doc["idSensor"]   = SENSOR_ID_PIR;
  doc["timestamp"]  = getTimestamp();
  doc["sensorType"] = SENSOR_TYPE_PIR;
  doc["readType"]   = READ_TYPE_PIR;
  doc["value"]      = presenca ? 1 : 0;

  String payload;
  serializeJson(doc, payload);

  Serial1.println("Enviando presença: " + payload);

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
  float temperatura = lerTemperatura();
  Serial1.printf("Temperatura lida: %.2f C\n", temperatura);
  bool okTemp = enviarDadosTemperatura(temperatura);
  Serial1.println(okTemp ? "Temperatura enviada com sucesso!" : "Falha no envio da temperatura");

  bool presenca = lerPresenca();
  Serial1.printf("Presença detectada: %s\n", presenca ? "SIM" : "NÃO");
  bool okPir = enviarDadosPresenca(presenca);
  Serial1.println(okPir ? "Presença enviada com sucesso!" : "Falha no envio da presença");
}