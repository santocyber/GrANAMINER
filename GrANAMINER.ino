#include <TFT_eSPI.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Crypto.h>
#include <SHA256.h>

// Configurações do display TFT_eSPI
TFT_eSPI tft = TFT_eSPI();
#define MAX_LINES 16
String lines[MAX_LINES]; // Buffer para linhas de texto
int screenLine = 0; // Linha inicial para exibição no display

// Configurações de rede WiFi
const char* ssid = "InternetSA";
const char* password = "cadebabaca";

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

void setup() {
  Serial.begin(115200);

  // Iniciar o display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true);

  // Mostrar logo na inicialização
  showLogo();

  // Conectar ao WiFi
  connectToWiFi();
}

void loop() {
  // Verificar o status da conexão WiFi e reconectar se necessário
  if (WiFi.status() != WL_CONNECTED) {
    tft.fillScreen(TFT_BLACK);
    screenLine = 2;
    clearScreen();
    logToScreen("WiFi desconectado!");
    logToScreen("Tentando reconectar...");
    Serial.println("WiFi desconectado! Tentando reconectar...");
    connectToWiFi();
  } else if (!client.connected()) {
    // Conectar ao servidor Stratum se desconectado
    connectToStratum();
  } else {
    // Executar mineração se conectado ao servidor Stratum
    mineBitcoin();
  }
  delay(1000);
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE); // Reset IP configuration
  WiFi.setDNS(IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4)); // Set DNS servers
  WiFi.begin(ssid, password);

  tft.fillScreen(TFT_BLACK);
  screenLine = 2;
  clearScreen();
  logToScreen("Conectando ao WiFi...");

  Serial.println("Conectando ao WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    tft.print(".");
  }

  Serial.println("\nConectado!");
  logToScreen("WiFi conectado!");
  logToScreen("Iniciando mineracao...");

  // Conectar ao servidor Stratum após conexão WiFi
  connectToStratum();
}

void connectToStratum() {
  if (!client.connect(stratumServer, stratumPort)) {
    Serial.println("Falha ao conectar ao servidor Stratum.");
    logToScreen("Falha ao conectar ao servidor Stratum.");
    return;
  }

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
    for (int i = 0; i < 32; i++) {
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
  tft.fillRect(0, 0, 130, 30, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("GrANA MINER");
  
  // Desenha o hashrate com espaçamento de 50 pixels
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(180, 0); // Ajustado com espaçamento de 50 pixels
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
  int logoWidth = 170; // Aproximadamente 10 caracteres em tamanho 4 (cada caractere tem cerca de 17 pixels de largura)
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
  
  // Centralizar o efeito animado
  for (int i = 0; i < tft.height(); i += 10) {
    tft.drawLine((tft.width() / 2) - i, 0, (tft.width() / 2) + i, tft.height(), TFT_GREEN);
    delay(50);
  }
  
  delay(2000);
  tft.fillScreen(TFT_BLACK);
}
