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

#define PIR_DEBOUNCE_MS 50

#define TEMP_CAL_RAW_LOW   110      
#define TEMP_CAL_REF_LOW   0.0f     
#define TEMP_CAL_RAW_HIGH  955      
#define TEMP_CAL_REF_HIGH  100.0f   
#define TEMP_MIN_VALID    -24.0f
#define TEMP_MAX_VALID     85.0f
#define TEMP_WINDOW_SIZE   8        
static float tempBuffer[TEMP_WINDOW_SIZE] = {};
static uint8_t tempBufIdx = 0;
static uint8_t tempBufCount = 0;

bool     pirLastStable   = false;
bool     pirLastRaw      = false;
uint32_t pirLastChangeMs = 0; 

#define RETRY_MAX_ATTEMPTS   4      
#define RETRY_BASE_DELAY_MS  500    
#define RETRY_MAX_DELAY_MS   16000   
#define RETRY_JITTER_MS      200 

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

  float tempRaw = -0.125f * raw + 88.5f;
  Serial1.printf("[TEMP] (raw=%d, calc=%.1f)\n", raw, tempRaw);

  if (tempRaw < TEMP_MIN_VALID || tempRaw > TEMP_MAX_VALID) {
    Serial1.printf("[TEMP] Amostra descartada (raw=%d, calc=%.1f)\n", raw, tempRaw);
    if (tempBufCount == 0) return 0.0f;
    float sum = 0;
    for (uint8_t i = 0; i < tempBufCount; i++) sum += tempBuffer[i];
    return sum / tempBufCount;
  }

  tempBuffer[tempBufIdx] = tempRaw;
  tempBufIdx = (tempBufIdx + 1) % TEMP_WINDOW_SIZE;
  if (tempBufCount < TEMP_WINDOW_SIZE) tempBufCount++;

  float sum = 0;
  for (uint8_t i = 0; i < tempBufCount; i++) sum += tempBuffer[i];
  float media = sum / tempBufCount;

  if (media < TEMP_MIN_VALID) media = TEMP_MIN_VALID;
  if (media > TEMP_MAX_VALID) media = TEMP_MAX_VALID;

  return media;
}

bool lerPresenca() {
  bool rawNow = (digitalRead(PIR_PIN) == HIGH);
  uint32_t now = millis();

  if (rawNow != pirLastRaw) {
    pirLastRaw      = rawNow;
    pirLastChangeMs = now;
  }

  if ((now - pirLastChangeMs) >= PIR_DEBOUNCE_MS) {
    pirLastStable = pirLastRaw;
  }

  return pirLastStable;
}

bool isRetriable(int httpCode) {
  if (httpCode <= 0)       return true;   
  if (httpCode == 429)     return true;   
  if (httpCode >= 500)     return true;  
  return false;
}

uint32_t calcBackoff(uint8_t attempt) {
  uint32_t expo = RETRY_BASE_DELAY_MS;
  for (uint8_t i = 0; i < attempt; i++) {
    expo *= 2;
    if (expo >= RETRY_MAX_DELAY_MS) { expo = RETRY_MAX_DELAY_MS; break; }
  }
  uint32_t jitter = (uint32_t)random(0, RETRY_JITTER_MS + 1);
  return expo + jitter;
}

void reconectarWiFiSeNecessario() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial1.println("[RETRY] Wi-Fi caiu — reconectando...");
  WiFi.disconnect();
  WiFi.begin(SSID, PASSWORD);

  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial1.print(".");
  }
  Serial1.println(WiFi.status() == WL_CONNECTED ? "\n[RETRY] Wi-Fi restaurado" : "\n[RETRY] Wi-Fi indisponível");
}

bool enviarComRetry(const String& payload, const char* label) {
  for (uint8_t attempt = 0; attempt < RETRY_MAX_ATTEMPTS; attempt++) {

    if (attempt > 0) {
      uint32_t waitMs = calcBackoff(attempt - 1);
      Serial1.printf("[RETRY] %s — tentativa %u/%u em %u ms...\n",
                     label, attempt + 1, RETRY_MAX_ATTEMPTS, waitMs);
      delay(waitMs);
      reconectarWiFiSeNecessario();
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial1.println("[RETRY] Sem Wi-Fi, pulando tentativa");
      continue;
    }

    HTTPClient http;
    http.begin(API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("ngrok-skip-browser-warning", "true");
    http.setTimeout(10000);

    int httpCode = http.POST(payload);
    String body  = (httpCode > 0) ? http.getString() : http.errorToString(httpCode);
    http.end();

    if (httpCode > 0) {
      Serial1.printf("[%s] HTTP %d — %s\n", label, httpCode, body.c_str());
    } else {
      Serial1.printf("[%s] Erro de rede: %s\n", label, body.c_str());
    }

    if (!isRetriable(httpCode)) {
      return (httpCode >= 200 && httpCode < 300);
    }
  }

  Serial1.printf("[RETRY] %s — todas as tentativas esgotadas\n", label);
  return false;
}

bool enviarDadosTemperatura(float valor) {
  StaticJsonDocument<256> doc;
  doc["idSensor"]   = SENSOR_ID_TEMP;
  doc["timestamp"]  = getTimestamp();
  doc["sensorType"] = SENSOR_TYPE_TEMP;
  doc["readType"]   = READ_TYPE_TEMP;
  doc["value"]      = valor;

  String payload;
  serializeJson(doc, payload);
  Serial1.println("Enviando temperatura: " + payload);

  return enviarComRetry(payload, "TEMP");
}

bool enviarDadosPresenca(bool presenca) {
  StaticJsonDocument<256> doc;
  doc["idSensor"]   = SENSOR_ID_PIR;
  doc["timestamp"]  = getTimestamp();
  doc["sensorType"] = SENSOR_TYPE_PIR;
  doc["readType"]   = READ_TYPE_PIR;
  doc["value"]      = presenca ? 1 : 0;

  String payload;
  serializeJson(doc, payload);
  Serial1.println("Enviando presença: " + payload);

  return enviarComRetry(payload, "PIR");
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