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



#define CONFIG_FILE "/wifi_config.txt"
#define MAX_WIFI_ATTEMPTS 3

// Configurações do display
TFT_eSPI tft = TFT_eSPI();
#define MAX_LINES 16
String lines[MAX_LINES]; // Buffer para linhas de texto
int screenLine = 0; // Linha inicial para exibição no display

// Configurações de rede WiFi
String ssid, password, username, botname;
bool conectadoweb = false;
int wifiAttempts = 0;
int connectionAttempts = 0;

// Configurações da Antpool
const char* stratumServer = "ss.antpool.com";
const int stratumPort = 3333;
const char* workerName = "santocyber";
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

// Variáveis para configuração web
String networksList;
bool loopweb = false;
unsigned long previousMillis = 0;

WebServer server(80);


void setup() {
  Serial.begin(115200);
  
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
    return;
  }

  // Iniciar o display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true);

  // Mostrar logo na inicialização
  showLogo();
  setupWEB();

    //esp_task_wdt_init(&config);


   // Redefinir o timeout do watchdog timer para 10 segundos
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 20000, // Timeout de 10000 milissegundos (10 segundos)
    .idle_core_mask = 1,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt_config);

  // Adicionar a tarefa principal ao watchdog timer, se ainda não estiver adicionada

      xTaskCreatePinnedToCore(task_feed_wdt, "task_feed_wdt_core0", configMINIMAL_STACK_SIZE * 2, NULL, 5, NULL, 1);

}


void task_feed_wdt(void *pvParameter) {
    esp_task_wdt_add(NULL);

    while (1) {
        vTaskDelay(15000 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        Serial.println("WDT alimentado pelo core 0");
    }
}

void loop() {
  // Reiniciar o Watchdog Timer
  loopWEB();

  // Executar lógica de mineração se conectado ao servidor Stratum
  if (conectadoweb) {
    mineBitcoin();
    delay(1000);
  }

}

void connectToWiFi() {
  tft.fillScreen(TFT_BLACK);
  screenLine = 2;
  clearScreen();
  logToScreen("Tentando conectar ao WiFi: " + ssid);

  Serial.println("Tentando conectar ao WiFi...");

  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 10000; // Tempo limite para tentar conectar ao WiFi (10 segundos)

  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < wifiTimeout) {
    delay(500);
    Serial.print(".");
    tft.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado!");
    logToScreen("WiFi conectado!");
    conectadoweb = true;
    logToScreen("Iniciando mineracao...");

    // Conectar ao servidor Stratum após conexão WiFi
    connectToStratum();
  } else {
    Serial.println("\nFalha ao conectar ao WiFi.");
    logToScreen("Falha ao conectar ao WiFi.");
    wifiAttempts++;

    if (wifiAttempts >= MAX_WIFI_ATTEMPTS) {
      setupAP();
    }
  }
}

void connectToStratum() {
  if (!client.connect(stratumServer, stratumPort)) {
    Serial.println("Falha ao conectar ao servidor Stratum.");
    logToScreen("Falha ao conectar ao servidor Stratum.");
    connectionAttempts++;

    // Verificar se atingiu o limite de tentativas
    if (connectionAttempts >= 5) {
      Serial.println("Falhou ao conectar 5 vezes. Reiniciando o ESP...");
      logToScreen("Falhou ao conectar 5 vezes. Reiniciando o ESP...");
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
  logToScreen("Enviado: " + authMessage);

  delay(1000);

  authMessage = String("{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"") + workerName + "\", \"" + workerPassword + "\"]}\n";
  client.print(authMessage);
  Serial.println("Enviado: " + authMessage);
  logToScreen("Enviado: " + authMessage);

  delay(1000);

  // Receber extranonce1
  String response = readStratumResponse();
  if (response.length() > 0) {
    Serial.println("Recebido: " + response);
    logToScreen("Recebido: " + response);

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      logToScreen("Erro ao analisar JSON: " + String(error.c_str()));
      return;
    }

    // Verificar se é uma resposta de subscribe
    if (doc.containsKey("result") && doc["result"].is<JsonArray>()) {
      JsonArray result = doc["result"];
      if (result.size() > 1 && result[1].is<String>()) {
        extranonce1 = result[1].as<String>();
      }
    }
  }
}

void mineBitcoin() {
  // Receber dados do servidor Stratum
  String response = readStratumResponse();
  if (response.length() > 0) {
    Serial.println("Recebido: " + response);
    logToScreen("Recebido: " + response);

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      logToScreen("Erro ao analisar JSON: " + String(error.c_str()));
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

      // Gerar extranonce2
      extranonce2 = String(random(0xFFFFFFFF), HEX); // Extranonce2 gerado aleatoriamente

      // Realizar mineração com os dados recebidos
      mineBlock();
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
  hashCount = 0;

  while (true) {
    String coinbase = coinbase1 + extranonce1 + extranonce2 + coinbase2;
    String coinbaseHash = sha256(coinbase);
    String merkleRoot = coinbaseHash;

    for (int i = 0; i < merkleBranches.size(); i++) {
      merkleRoot = sha256(merkleRoot + merkleBranches[i].as<String>());
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

    // Calcular o hashrate a cada segundo
    if (millis() - startTime >= 1000) {
      hashRate = hashCount / ((millis() - startTime) / 1000.0);
      startTime = millis();
      hashCount = 0;
      displayHashRate();
    }

    // Exibir o hash gerado
    logToScreen("Hash: " + hash + " Nonce: " + String(nonce));

    Serial.print("Hash gerado: ");
    Serial.println(hash);

    // Verificar se o hash gerado é válido
    if (isValidHash(hashBin, nbits)) {
      logToScreen("Bloco encontrado!");
      submitBlock(nonce);
      break;
    }

    nonce++;
  }
}

String sha256(String data) {
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
  // Garantir que extranonce2, ntime e nonce estejam no formato hexadecimal correto e com o comprimento correto
  String formattedExtranonce2 = String("00000000" + extranonce2).substring(extranonce2.length());
  String formattedNtime = String("00000000" + ntime).substring(ntime.length());
  String formattedNonce = String("00000000" + String(nonce, HEX)).substring(String(nonce, HEX).length());

  String submitMessage = String("{\"id\": 3, \"method\": \"mining.submit\", \"params\": [\"") + workerName + "\", \"" + jobId + "\", \"" + formattedExtranonce2 + "\", \"" + formattedNtime + "\", \"" + formattedNonce + "\"]}\n";
  client.print(submitMessage);
  Serial.println("Enviado: " + submitMessage);
  logToScreen("Enviado: " + submitMessage);
}

void logToScreen(String message) {
  for (int i = 0; i < MAX_LINES - 1; i++) {
    lines[i] = lines[i + 1];
  }
  lines[MAX_LINES - 1] = message;
  redrawScreen();
}

void clearScreen() {
  for (int i = 0; i < MAX_LINES; i++) {
    lines[i] = "";
  }
  redrawScreen();
}

void redrawScreen() {
  tft.fillScreen(TFT_BLACK);

  // Desenha o logo
  tft.fillRect(0, 0, 145, 30, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.println("GrANA MINER");

  // Desenha o hashrate com espaçamento de 50 pixels
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(150, 10); // Ajustado com espaçamento de 50 pixels
  tft.setTextSize(2);
  tft.println("Hashrate: " + String(hashRate, 2) + " H/s");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  for (int i = 0; i < MAX_LINES; i++) {
    tft.setCursor(0, (i + 2) * 16);
    tft.println(lines[i]);
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



  /*
  // Centralizar o efeito animado
  for (int i = 0; i < 900; i += 5) {
    tft.drawLine((tft.width() / 2) - i, 0 , (tft.width() / 2) + i, tft.height() ^ i / 2 , TFT_GREEN);
    delay(50);
  }
*/
  delay(500);
  tft.fillScreen(TFT_BLACK);
}
