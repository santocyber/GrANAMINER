#include <WiFi.h>
#include <ArduinoJson.h>
#include <Crypto.h>
#include <SHA256.h>
#include <esp_task_wdt.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include "WebServer.h"
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

#define CONFIG_FILE "/wifi_config.txt"
#define MAX_WIFI_ATTEMPTS 3

// Configurações do display
TFT_eSPI tft = TFT_eSPI();
#define MAX_LINES 16
struct Line {
  String text;
  uint16_t color;
  uint8_t textSize;
};
Line lines[MAX_LINES]; // Buffer para linhas de texto
int screenLine = 0; // Linha inicial para exibição no display

// Configurações de rede WiFi
String ssid, password, username, botname;
bool conectadoweb = false;
int wifiAttempts = 0;
int connectionAttempts = 0;

// Configurações da Antpool
const char* stratumServer = "solo.ckpool.org";
const int stratumPort = 3333;
const char* workerName = "1EhGEPUDoUHqi9c2TQbwKMq6cnNVjpBqQF";
const char* workerPassword = "x"; // Antpool usa 'x' como senha por padrão

WiFiClient client;

// Variáveis para calcular o hashrate
unsigned long lastHashTime = 0;
unsigned long hashCount = 0;
float hashRate = 0;

// Variáveis para mineração
String jobId;
String extranonce1;
String extranonce2;
String ntime;
String nbits;
String coinbase1;
String coinbase2;
JsonArray merkleBranches;
String previousBlockHash;
String version;
int difficulty;

// Variáveis para configuração web
String networksList;
bool loopweb = false;
unsigned long previousMillis = 0;
unsigned long lastCommunicationMillis = 0;
unsigned long lastScreenUpdateMillis = 0; // Tempo da última atualização da tela

WebServer server(80);
TaskHandle_t miningMonitorTaskHandle = NULL;

// Configurações do NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, IPAddress(129, 6, 15, 28), -3 * 3600); // IP do servidor NTP
String timeStamp;

void setup() {
  Serial.begin(115200);
  
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
    return;
  }

  // Iniciar o display
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true);

  // Mostrar logo na inicialização
  showLogo();
  setupWEB();

  // Inicializar o NTP
  timeClient.begin();

  // Redefinir o timeout do watchdog timer para 10 segundos
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 120000, // Timeout de 120000 milissegundos (120 segundos)
    .idle_core_mask = 1,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  // Criar a tarefa de monitoramento
  xTaskCreatePinnedToCore(
    miningMonitorTask,    // Função da tarefa
    "MiningMonitorTask",  // Nome da tarefa
    2000,                // Tamanho da pilha da tarefa
    NULL,                 // Parâmetro da tarefa
    1,                    // Prioridade da tarefa
    &miningMonitorTaskHandle, // Handle da tarefa
    0                    // Core onde a tarefa será executada
  );
}

void loop() {
  esp_task_wdt_reset();

  // Reiniciar o Watchdog Timer
  loopWEB();

  // Executar lógica de mineração se conectado ao servidor Stratum
  if (conectadoweb) {
    mineBitcoin();
  }
  delay(1000);
}

void miningMonitorTask(void *parameter) {
  while (true) {
    // Verificar se houve comunicação nos últimos 120 segundos
    if (millis() - lastCommunicationMillis > 120000) {
      Serial.println("Nenhuma comunicação em 120 segundos, reiniciando o ESP...");
      delay(10000);
      ESP.restart();
    }
if (WiFi.status() == WL_CONNECTED) {
    // Atualizar a hora a partir do servidor NTP
    updateTimeFromNTP();
}
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Verificar a cada 10 minutos
  }
}

void connectToWiFi() {
  tft.fillScreen(TFT_BLACK);
  screenLine = 2;
  clearScreen();
  logToScreen("Tentando conectar ao WiFi: " + ssid, TFT_WHITE, 1);

  Serial.println("Tentando conectar ao WiFi...");

  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 3000; // Tempo limite para tentar conectar ao WiFi (3 segundos)

  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < wifiTimeout) {
    delay(500);
    Serial.print(".");
    tft.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado!");
    logToScreen("WiFi conectado!", TFT_WHITE, 1);
    conectadoweb = true;
    logToScreen("Iniciando mineracao...", TFT_WHITE, 1);

    // Conectar ao servidor Stratum após conexão WiFi
    connectToStratum();
  } else {
    Serial.println("\nFalha ao conectar ao WiFi.");
    logToScreen("Falha ao conectar ao WiFi.", TFT_RED, 1);
    wifiAttempts++;

    if (wifiAttempts >= MAX_WIFI_ATTEMPTS) {
      setupAP();
    }
  }
}

void connectToStratum() {
  if (!client.connect(stratumServer, stratumPort)) {
    Serial.println("Falha ao conectar ao servidor Stratum.");
    logToScreen("Falha ao conectar ao servidor Stratum.", TFT_RED, 1);
    connectionAttempts++;

    // Verificar se atingiu o limite de tentativas
    if (connectionAttempts >= 5) {
      Serial.println("Falhou ao conectar 5 vezes. Reiniciando o ESP...");
      logToScreen("Falhou ao conectar 5 vezes. Reiniciando o ESP...", TFT_RED, 1);
      ESP.restart();
    }
    return;
  }

  // Resetar contador de tentativas após conexão bem-sucedida
  connectionAttempts = 0;

  // Enviar mensagem de autenticação
  String authMessage = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n";
  client.print(authMessage);
  Serial.println("Enviado: " + authMessage);
  logToScreen("Enviado: " + authMessage, TFT_ORANGE, 1);

  delay(1000);

  authMessage = String("{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"") + workerName + "\", \"" + workerPassword + "\"]}\n";
  client.print(authMessage);
  Serial.println("Enviado: " + authMessage);
  logToScreen("Enviado: " + authMessage, TFT_ORANGE, 1);

  delay(1000);

  // Receber extranonce1
  String response = readStratumResponse();
  if (response.length() > 0) {
    Serial.println("Recebido: " + response);
    logToScreen("Recebido dados do servidor", TFT_WHITE, 1);

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      logToScreen("Erro ao analisar JSON: " + String(error.c_str()), TFT_RED, 1);
      return;
    }

    // Verificar se é uma resposta de subscribe
    if (doc.containsKey("result") && doc["result"].is<JsonArray>()) {
      JsonArray result = doc["result"];
      if (result.size() > 1 && result[1].is<String>()) {
        extranonce1 = result[1].as<String>();
      }
    }

    // Verificar se é uma resposta de set_difficulty
    if (doc.containsKey("method") && doc["method"] == "mining.set_difficulty") {
      difficulty = doc["params"][0];
      Serial.print("Nova dificuldade recebida: ");
      Serial.println(difficulty);
      logToScreen("Nova dificuldade: " + String(difficulty), TFT_RED, 2);
    }
  }
  // Atualizar o tempo da última comunicação
  lastCommunicationMillis = millis();
}

void mineBitcoin() {
  esp_task_wdt_reset();

  // Receber dados do servidor Stratum
  String response = readStratumResponse();
  if (response.length() > 0) {
    Serial.println("Recebido: " + response);
    logToScreen("Recebido dados do servidor", TFT_WHITE, 1);

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      logToScreen("Erro ao analisar JSON: " + String(error.c_str()), TFT_RED, 1);
      return;
    }

    // Verificar se é um trabalho de mineração
    if (doc.containsKey("method") && doc["method"] == "mining.notify") {
      jobId = doc["params"][0].as<String>();
      previousBlockHash = doc["params"][1].as<String>();
      coinbase1 = doc["params"][2].as<String>();
      coinbase2 = doc["params"][3].as<String>();
      merkleBranches = doc["params"][4].as<JsonArray>();
      version = doc["params"][5].as<String>();
      nbits = doc["params"][6].as<String>();
      ntime = doc["params"][7].as<String>();

      // Imprimir os parâmetros recebidos
      Serial.println("Job ID: " + jobId);
      Serial.println("Previous Block Hash: " + previousBlockHash);
      Serial.println("Coinbase1: " + coinbase1);
      Serial.println("Coinbase2: " + coinbase2);
      Serial.println("Version: " + version);
      Serial.println("Nbits: " + nbits);
      Serial.println("Ntime: " + ntime);
      Serial.println("Difficulty: " + String(difficulty)); // Exibir dificuldade

      logToScreen("Job ID: " + jobId, TFT_WHITE, 1);
      logToScreen("Previous Block Hash: " + previousBlockHash, TFT_WHITE, 1);
      logToScreen("Coinbase1: " + coinbase1, TFT_WHITE, 1);
      logToScreen("Coinbase2: " + coinbase2, TFT_WHITE, 1);
      logToScreen("Version: " + version, TFT_WHITE, 1);
      logToScreen("Nbits: " + nbits, TFT_WHITE, 1);
      logToScreen("Ntime: " + ntime, TFT_WHITE, 1);
      logToScreen("Difficulty: " + String(difficulty), TFT_RED, 1); // Exibir dificuldade

      // Gerar extranonce2
      extranonce2 = String(random(0xFFFFFFFF), HEX); // Extranonce2 gerado aleatoriamente

      // Realizar mineração com os dados recebidos
      mineBlock();
    } else if (doc.containsKey("method") && doc["method"] == "mining.set_difficulty") {
      difficulty = doc["params"][0];
      Serial.print("Nova dificuldade recebida: ");
      Serial.println(difficulty);
      logToScreen("Nova dificuldade: " + String(difficulty), TFT_RED, 2);
    }
  }
}

String readStratumResponse() {
  String response = "";
  while (client.available()) {
    response += client.readStringUntil('\n');
  }
  return response;
}

void mineBlock() {
  uint32_t nonce = 0;
  String hash = "";
  uint8_t hashBin[32]; // SHA-256 produces a 32-byte hash

  unsigned long startTime = millis();
  unsigned long iterationStartTime = millis();
  unsigned long totalIterations = 0;
  hashCount = 0;

  while (true) {
    esp_task_wdt_reset();

    String coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2;
    String coinbaseHash = calculateSha256(coinbase);
    String merkleRoot = coinbaseHash;

    for (int i = 0; i < merkleBranches.size(); i++) {
      merkleRoot = calculateSha256(merkleRoot + merkleBranches[i].as<String>());
    }

    SHA256 sha256;
    sha256.reset();
    sha256.update(hexToBytes(version), 4);
    sha256.update(hexToBytes(previousBlockHash), 32);
    sha256.update(hexToBytes(merkleRoot), 32);
    sha256.update(hexToBytes(ntime), 4);
    sha256.update(hexToBytes(nbits), 4);
    sha256.update((uint8_t*)&nonce, sizeof(nonce));
    sha256.finalize(hashBin, sizeof(hashBin));

    hash = "";
    for (int i = 0;  i < 32; i++) {
      char hex[3];
      sprintf(hex, "%02x", hashBin[i]);
      hash += String(hex);
    }

    // Incrementar contagem de hashes
    hashCount++;
    totalIterations++;

    // Calcular o hashrate a cada segundo
    if (millis() - startTime >= 1000) {
      hashRate = hashCount / ((millis() - startTime) / 1000.0);
      startTime = millis();
      hashCount = 0;
      displayHashRate();
    }

    // Exibir o hash gerado se o tempo desde a última atualização da tela for maior que o intervalo definido
    if (millis() - lastScreenUpdateMillis >= 4000) { // Atualiza a cada 1 segundo
      logToScreen("Hash: " + hash + " Nonce: " + String(nonce), random(0xFFFF), 1);
      lastScreenUpdateMillis = millis();
    }

    Serial.print("Hash gerado: ");
    Serial.println(hash);
    lastCommunicationMillis = millis();

    // Verificar se o hash gerado é válido
    if (isValidHash(hashBin, nbits)) {
      logToScreen("Bloco encontrado! Hora: " + timeStamp, TFT_GREEN, 3);
      submitBlock(nonce);
      break;
    }

    // Incrementar nonce
    nonce++;

    // Medir o tempo de 100 iterações para calcular o tempo médio por iteração
    if (totalIterations % 100 == 0) {
      unsigned long iterationEndTime = millis();
      unsigned long elapsedTime = iterationEndTime - iterationStartTime;
      float averageIterationTime = (float)elapsedTime / 100.0;
      Serial.print("Tempo médio por iteração (ms): ");
      Serial.println(averageIterationTime);
      logToScreen("Tempo de 100 hashes (ms): " + String(averageIterationTime), TFT_WHITE, 1);
      iterationStartTime = iterationEndTime;
    }
  }
}

String calculateSha256(String data) {
  SHA256 sha256;
  uint8_t hashBin[32];
  sha256.reset();
  sha256.update(data.c_str(), data.length());
  sha256.finalize(hashBin, sizeof(hashBin));

  String hash = "";
  for (int i = 0; i < 32; i++) {
    char hex[3];
    sprintf(hex, "%02x", hashBin[i]);
    hash += String(hex);
  }

  return hash;
}

uint8_t* hexToBytes(String hex) {
  static uint8_t bytes[32];
  for (int i = 0; i < 32; i++) {
    sscanf(&hex[i * 2], "%2hhx", &bytes[i]);
  }
  return bytes;
}

bool isValidHash(uint8_t* hash, String target) {
  // Converte o alvo compactado para sua representação completa de 256 bits
  uint32_t targetBits;
  sscanf(target.c_str(), "%8x", &targetBits);
  
  uint8_t exponent = (targetBits >> 24) & 0xFF;
  uint32_t coefficient = targetBits & 0xFFFFFF;
  uint8_t targetFull[32] = {0};

  if (exponent <= 3) {
    coefficient >>= 8 * (3 - exponent);
    memcpy(targetFull, &coefficient, 3);
  } else {
    memcpy(targetFull + (exponent - 3), &coefficient, 3);
  }

  // Comparar o hash gerado com o alvo de dificuldade
  for (int i = 0; i < 32; i++) {
    if (hash[31 - i] < targetFull[31 - i]) {
      return true;
    } else if (hash[31 - i] > targetFull[31 - i]) {
      return false;
    }
  }
  return true; // Hash igual ao target
}

void submitBlock(uint32_t nonce) {
  // Garantir que extranonce2, ntime e nonce estejam no formato hexadecimal correto e com o comprimento correto
  String formattedExtranonce2 = String("00000000" + extranonce2).substring(extranonce2.length());
  String formattedNtime = String("00000000" + ntime).substring(ntime.length());
  String formattedNonce = String("00000000" + String(nonce, HEX)).substring(String(nonce, HEX).length());

  String submitMessage = String("{\"id\": 3, \"method\": \"mining.submit\", \"params\": [\"") + workerName + "\", \"" + jobId + "\", \"" + formattedExtranonce2 + "\", \"" + formattedNtime + "\", \"" + formattedNonce + "\"]}\n";
  client.print(submitMessage);
  Serial.println("Enviado: " + submitMessage);
  logToScreen("Enviado: " + submitMessage, TFT_ORANGE, 1);

  // Atualizar o tempo da última comunicação
  lastCommunicationMillis = millis();
  esp_task_wdt_reset();

  // Ler resposta do servidor
  String response = readStratumResponse();
  if (response.length() > 0) {
    Serial.println("Recebido: " + response);
    logToScreen("Recebido: " + response, TFT_WHITE, 1);

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      logToScreen("Erro ao analisar JSON: " + String(error.c_str()), TFT_RED, 1);
      return;
    }

    // Verificar se a submissão foi aceita
    if (doc.containsKey("result") && doc["result"] == true) {
      Serial.println("Submissão aceita pela pool!");
      logToScreen("Submissão aceita pela pool!", TFT_GREEN, 2);
    } else if (doc.containsKey("error") && doc["error"].is<JsonObject>()) {
      String errorReason = doc["error"]["reject-reason"].as<String>();
      Serial.println("Submissão rejeitada pela pool: " + errorReason);
      logToScreen("Submissão rejeitada: " + errorReason, TFT_RED, 2);
    }
  }
}

void logToScreen(String message, uint16_t color, uint8_t textSize) {
  for (int i = 0; i < MAX_LINES - 1; i++) {
    lines[i] = lines[i + 1];
  }
  lines[MAX_LINES - 1].text = message;
  lines[MAX_LINES - 1].color = color;
  lines[MAX_LINES - 1].textSize = textSize;
  redrawScreen();
}

void clearScreen() {
  for (int i = 0; i < MAX_LINES; i++) {
    lines[i].text = "";
    lines[i].color = TFT_WHITE;
    lines[i].textSize = 1;
  }
  redrawScreen();
}

void redrawScreen() {
  tft.fillScreen(TFT_BLACK);

  // Desenha o logo
  tft.fillRect(0, 0, 145, 30, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(6, 8);
  tft.println("GrANA MINER");

  // Desenha o hashrate, temperatura e memória livre com espaçamento de 50 pixels
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(150, 10); // Ajustado com espaçamento de 50 pixels
  tft.setTextSize(2);
  tft.print("Hashrate:" + String(hashRate, 2) + "H/s");
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print(" T:" + String(temperatureRead(), 1) + "C ");
  tft.print("MEM:" + String(ESP.getFreeHeap()) + "B");

  // Exibir hora atual e uso da CPU
  timeStamp = String(hour()) + ":" + String(minute()) + ":" + String(second());
  uint32_t freeHeap = ESP.getFreeHeap();
  tft.setCursor(380, 21); // Ajuste de acordo com o tamanho da tela
  tft.println("Hora:" + timeStamp);

  for (int i = 0; i < MAX_LINES; i++) {
    tft.setTextColor(lines[i].color, TFT_BLACK);
    tft.setTextSize(lines[i].textSize);
    tft.setCursor(0, (i + 2) * 16);
    tft.println(lines[i].text);
  }
}

void displayHashRate() {
  redrawScreen();
}

void showLogo() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);

  // Dimensões do texto "GrANA MINER"
  int logoWidth = 220; // Aproximadamente 10 caracteres em tamanho 4 (cada caractere tem cerca de 17 pixels de largura)
  int logoHeight = 32; // Aproximadamente 8 pixels por tamanho de texto (4 * 8)

  // Centralizar o texto horizontalmente e verticalmente
  int x = (tft.width() - logoWidth) / 2;
  int y = (tft.height() - logoHeight) / 2 - 20; // Ajustar para a metade da altura e um pouco acima
  tft.setCursor(x, y);
  tft.setTextSize(4);
  tft.println("GrANA MINER");

  // Dimensões do texto "by SantoCyber"
  int subtextWidth = 132; // Aproximadamente 11 caracteres em tamanho 2 (cada caractere tem cerca de 12 pixels de largura)
  int subtextHeight = 16; // Aproximadamente 8 pixels por tamanho de texto (2 * 8)

  x = (tft.width() - subtextWidth) / 2;
  y = (tft.height() - subtextHeight) / 2 + 20; // Ajustar para a metade da altura e um pouco abaixo
  tft.setCursor(x, y);
  tft.setTextSize(2);
  tft.println("by SantoCyber");
  // Carrega o patternIndex do SPIFFS
  int patternIndex = loadPatternIndex();
  drawPattern(patternIndex);

  // Incrementa o índice e salva no SPIFFS
  patternIndex = (patternIndex + 1) % 5;
  savePatternIndex(patternIndex);
  delay(500);
  tft.fillScreen(TFT_BLACK);
}

void updateTimeFromNTP() {
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  setTime(timeClient.getEpochTime());
  timeStamp = String(hour()) + ":" + String(minute()) + ":" + String(second());
  Serial.println("Hora atualizada do servidor NTP.");
}
