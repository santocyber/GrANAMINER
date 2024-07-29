






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
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= 1000) {
    previousMillis = currentMillis;

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
  tft.print("H/s:" + String(hashRate, 2) + "H/s");
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("T:" + String(temperatureRead(), 1) + "C ");
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

void selectPool() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 50);
  tft.println("Selecione a Pool:");
  
  // Desenhar botões
  tft.fillRect(10, 100, 300, 50, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 120);
  tft.println("Antpool");

  tft.fillRect(10, 200, 300, 50, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 220);
  tft.println("CKPool");

  unsigned long startMillis = millis();
  while (millis() - startMillis < 3000) {
    if (tft.getTouch(&x, &y)) {
      if (x > 10 && x < 410 && y > 100 && y < 150) { // Antpool
        isSoloMining = false;
        stratumServer = antpoolServer;
        workerName = antpoolWorkerName;
        savePoolSelection(false);
        return;
      } else if (x > 10 && x < 410 && y > 200 && y < 250) { // CKPool
        isSoloMining = true;
        stratumServer = ckpoolServer;
        workerName = ckpoolWorkerName;
        savePoolSelection(true);
        return;
      }
    }
    delay(100);
  }

  // Se não houver seleção, por padrão usar a pool salva no SPIFFS
  loadPoolSelection();
}

void savePoolSelection(bool isSolo) {
  File file = SPIFFS.open(POOL_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open pool config file for writing");
    return;
  }
  file.print(isSolo ? "solo" : "shared");
  file.close();
}

void loadPoolSelection() {
  File file = SPIFFS.open(POOL_FILE, FILE_READ);
  if (!file) {
    Serial.println("Failed to open pool config file for reading");
    // Usar Antpool como padrão se não conseguir ler o arquivo
    isSoloMining = false;
    stratumServer = antpoolServer;
    workerName = antpoolWorkerName;
    return;
  }
  String content = file.readString();
  file.close();

  if (content == "solo") {
    isSoloMining = true;
    stratumServer = ckpoolServer;
    workerName = ckpoolWorkerName;
  } else {
    isSoloMining = false;
    stratumServer = antpoolServer;
    workerName = antpoolWorkerName;
  }
}
