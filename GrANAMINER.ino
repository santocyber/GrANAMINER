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
#define POOL_FILE "/pool_config.txt"
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

uint16_t x, y;
uint8_t z;

// Configurações de rede WiFi
String ssid = "your_SSID";
String password = "your_PASSWORD";
String username, botname;
bool conectadoweb = false;
int wifiAttempts = 0;
int connectionAttempts = 0;

// Configurações da Antpool e CKPool
const char* antpoolServer = "ss.antpool.com";
const char* ckpoolServer = "solo.ckpool.org";
const char* stratumServer;
const int stratumPort = 3333;
const char* antpoolWorkerName = "santocyber";
const char* ckpoolWorkerName = "1EhGEPUDoUHqi9c2TQbwKMq6cnNVjpBqQF";
const char* workerName;
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
uint32_t difficulty;
String lastHash; // Armazenar o último hash gerado

// Variáveis para configuração web
String networksList;
bool loopweb = false;
unsigned long previousMillis = 0;
unsigned long lastCommunicationMillis = 0;
unsigned long lastScreenUpdateMillis = 0; // Tempo da última atualização da tela

WebServer server(80);
TaskHandle_t miningMonitorTaskHandle = NULL;
bool isSoloMining = false;

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
  // Selecionar a pool
  selectPool(); // Chamar a função de seleção da pool que está em outra aba
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
      esp_task_wdt_reset();
    }
     Serial.println("FUNCAO TASK HANDLE");

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
    logToScreen(stratumServer, TFT_YELLOW, 1);

    // Conectar ao servidor Stratum após conexão WiFi
    connectToStratum();
  } else {
    Serial.println("\nFalha ao conectar ao WiFi.");
    logToScreen("Falha ao conectar ao WiFi.", TFT_RED, 2);
    wifiAttempts++;

    if (wifiAttempts >= MAX_WIFI_ATTEMPTS) {
      setupAP();
    }
  }
}

void connectToStratum() {
  if (!client.connect(stratumServer, stratumPort)) {
    Serial.println("Falha ao conectar ao servidor Stratum.");
    logToScreen("Falha ao conectar ao servidor Stratum..", TFT_RED, 2);
    return;
  }

  // Inscrevendo-se no servidor Stratum
  String subscribeMessage = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n";
  client.print(subscribeMessage);
  delay(1000);
  
  if (client.available()) {
    processStratumMessages();
  }

  String authMessage = String("{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"") + workerName + "\", \"" + workerPassword + "\"]}\n";
  client.print(authMessage);
  delay(1000);

  if (client.available()) {
    processStratumMessages();
  }
}

void processStratumMessages() {
  while (client.available()) {
    String response = client.readStringUntil('\n');
    Serial.println("Recebido: " + response);

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
    } else {
      processStratumMessage(doc);
    }
  }
}

void processStratumMessage(DynamicJsonDocument &doc) {
  if (doc.containsKey("method")) {
    String method = doc["method"].as<String>();
    
    if (method == "mining.set_difficulty") {
      difficulty = doc["params"][0].as<uint32_t>();
      Serial.print("Nova dificuldade recebida: ");
      Serial.println(difficulty);
      logToScreen("Nova dificuldade: " + String(difficulty), TFT_RED, 2);
    } else if (method == "mining.notify") {
      jobId = doc["params"][0].as<String>();
      previousBlockHash = doc["params"][1].as<String>();
      coinbase1 = doc["params"][2].as<String>();
      coinbase2 = doc["params"][3].as<String>();
      merkleBranches = doc["params"][4].as<JsonArray>();
      version = doc["params"][5].as<String>();
      nbits = doc["params"][6].as<String>();
      ntime = doc["params"][7].as<String>();

      // Imprimir dados recebidos na Serial
      Serial.println("Dados recebidos:");
      Serial.println("Job ID: " + jobId);
      Serial.println("Previous Block Hash: " + previousBlockHash);
      Serial.println("Coinbase1: " + coinbase1);
      Serial.println("Coinbase2: " + coinbase2);
      Serial.println("Version: " + version);
      Serial.println("Nbits: " + nbits);
      Serial.println("Ntime: " + ntime);
      Serial.println("Difficulty: " + String(difficulty)); // Exibir dificuldade

      // Imprimir dados recebidos na tela TFT
      //logToScreen("Dados recebidos:", TFT_WHITE, 1);
      logToScreen("Job ID: " + jobId, TFT_GREEN, 1);
      logToScreen("Prev Block Hash: " + previousBlockHash, TFT_CYAN, 1);
     // logToScreen("Coinbase1: " + coinbase1, TFT_CYAN, 1);
     // logToScreen("Coinbase2: " + coinbase2, TFT_PINK, 1);
      logToScreen("Version: " + version, TFT_YELLOW, 1);
      logToScreen("Nbits: " + nbits, TFT_GREEN, 1);
      logToScreen("Ntime: " + ntime, TFT_ORANGE, 1);
      logToScreen("Difficulty: " + String(difficulty), TFT_RED, 1);

      extranonce2 = String(random(0xFFFFFFFF), HEX);

      if (isSoloMining) {
        mineBlockSolo();
      } else {
        mineBlockShared();
      }
    }
  }
}

void mineBitcoin() {

  // Receber dados do servidor Stratum
  if (client.available()) {
    processStratumMessages();
  }
}

uint8_t* hexToBytes(String hex) {
  static uint8_t bytes[32];
  for (int i = 0; i < 32; i++) {
    sscanf(&hex[i * 2], "%2hhx", &bytes[i]);
  }
  return bytes;
}

bool isValidHash(uint8_t* hash, String target) {
  uint8_t targetBin[32];
  for (int i = 0; i < 32; i++) {
    sscanf(&target[i * 2], "%2hhx", &targetBin[i]);
  }

  for (int i = 0; i < 32; i++) {
    if (hash[i] < targetBin[i]) {
      return true;
    } else if (hash[i] > targetBin[i]) {
      return false;
    }
  }
  return true; // Hash igual ao target
}

void submitBlock(uint32_t nonce) {
  String formattedExtranonce2 = String("00000000" + extranonce2).substring(extranonce2.length());
  String formattedNtime = String("00000000" + ntime).substring(ntime.length() - 8);
  String formattedNonce = String("00000000" + String(nonce, HEX)).substring(String(nonce, HEX).length());

  Serial.println("Dados antes do envio:");
  Serial.println("jobId: " + jobId);
  Serial.println("extranonce2: " + formattedExtranonce2);
  Serial.println("ntime: " + formattedNtime);
  Serial.println("nonce: " + formattedNonce);

  String submitMessage = String("{\"id\": 3, \"method\": \"mining.submit\", \"params\": [\"") + workerName + "\", \"" + jobId + "\", \"" + formattedExtranonce2 + "\", \"" + formattedNtime + "\", \"" + formattedNonce + "\"]}\n";
  client.print(submitMessage);
  Serial.println("Enviado: " + submitMessage);

  lastCommunicationMillis = millis();

  if (client.available()) {
    processStratumMessages();
  }
}

void mineBlockSolo() {
  uint32_t nonce = 0;
  String hash = "";
  uint8_t hashBin[32]; // SHA-256 produces a 32-byte hash

  unsigned long startTime = millis();
  unsigned long iterationStartTime = millis();
  unsigned long totalIterations = 0;
  hashCount = 0;

  while (true) {
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

    lastHash = hash; // Armazenar o último hash gerado

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

    // Verificar se o hash gerado é válido
    if (isValidHash(hashBin, nbits)) {
      logToScreen("Hash valido encontrado. Nonce: " + String(nonce), TFT_YELLOW, 2);
      submitBlock(nonce); // Enviar o bloco apenas se o hash for válido
      break;
    }

    // Incrementar nonce
    nonce++;

 
    // Exibir o nonce, jobId e último hash gerado
    if (millis() - lastScreenUpdateMillis >= 1000) { // Atualiza a cada 1 segundo
      logToScreen("Nonce: " + String(nonce), random(0xFFFF), 1);
      logToScreen("Job ID: " + jobId, TFT_GREEN, 1);
      logToScreen("Last Hash: " + lastHash, random(0xFFFF), 1); // Exibir o último hash gerado
      lastScreenUpdateMillis = millis();
      lastCommunicationMillis = millis();
    }
  }
}

void mineBlockShared() {
  uint32_t nonce = 0;
  String hash = "";
  uint8_t hashBin[32]; // SHA-256 produces a 32-byte hash

  unsigned long startTime = millis();
  unsigned long iterationStartTime = millis();
  unsigned long totalIterations = 0;
  hashCount = 0;

  while (true) {
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

    lastHash = hash; // Armazenar o último hash gerado

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

    // Verificar se o hash gerado é válido
    if (isValidHash(hashBin, nbits)) {
      logToScreen("Hash valido encontrado. Nonce: " + String(nonce), TFT_YELLOW, 2);
      submitBlock(nonce); // Enviar o bloco apenas se o hash for válido
      break;
    }

    // Incrementar nonce
    nonce++;


    // Exibir o nonce, jobId e último hash gerado
    if (millis() - lastScreenUpdateMillis >= 1000) { // Atualiza a cada 1 segundo
      logToScreen("Nonce: " + String(nonce), random(0xFFFF), 1);
      logToScreen("Job ID: " + jobId, TFT_GREEN, 1);
      logToScreen("Last Hash: " + lastHash, random(0xFFFF), 1); // Exibir o último hash gerado
      lastScreenUpdateMillis = millis();
      lastCommunicationMillis = millis();
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
