void scanNetworks() {
  Serial.println("Scanning WiFi networks");
  //WiFi.disconnect(true); // Desativa a reconexão automática

  int n = WiFi.scanNetworks();
  DynamicJsonDocument jsonBuffer(1000);
  JsonArray networks = jsonBuffer.createNestedArray("networks");

  for (int i = 0; i < n && i < 5; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
  }

  networksList = "";
  serializeJson(jsonBuffer, networksList);
  Serial.println(networksList);
  //WiFi.disconnect(true); // Desativa a reconexão automática
}

void setupWEB() {
  Serial.println("INICIANDO CONFIG WiFi");
  loadCredentials();

  if (ssid.length() > 0) {
    tft.fillScreen(TFT_BLACK);
    logToScreen("Arquivo de configuração encontrado.", TFT_WHITE, 1);
    logToScreen("Tentando conectar ao WiFi...", TFT_WHITE, 1);
    delay(500);
    connectToWiFi();
  } else {
    setupAP();
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }
}

void setupAP() {
  //wifiMulti.run();
  WiFi.mode(WIFI_AP_STA);
  //WiFi.disconnect(true); // Desativa a reconexão automática
  delay(200);
  
  bool apStarted = WiFi.softAP("GrANACONFIG");  // Inicia o AP
  if (apStarted) {
  //WiFi.softAP("GrANACONFIG");
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);

  setupWebServer();
  logToScreen("Modo AP ativado. Conecte-se ao SSID 'GrANACONFIG'.", TFT_WHITE, 1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);
  tft.setTextSize(4);
  tft.setTextColor(TFT_GREEN);
  tft.println("GrANA MINER");
  tft.setTextColor(TFT_WHITE);

  tft.println("MODO AP");
  tft.println("ENTRE NO IP:");
  tft.println(apIP);

  conectadoweb = false;
  loopweb = true;
      
}
else{
    Serial.print("ERRO AP NAO INICIADO");

  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_GET, handleDeleteCredentials);
  server.on("/restart", HTTP_GET, restartEsp);
  server.on("/scan", HTTP_GET, scanNetworks);
  server.begin();
}

void loopWEB() {
  if (loopweb) {
    server.handleClient();
  }
}

void restartEsp() {
  server.send(200, "text/plain", "restarting...");
  delay(1000);
  ESP.restart();
}

void handleRoot() {

  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<title>GrANA WiFi Config</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; text-align: center; background-color: black; color: white; }"
                "input[type='checkbox'], input[type='password'], input[type='text'] { width: 50%; padding: 10px; margin: 10px 0; }"
                "button { padding: 5px 10px; font-size: 10px; margin: 10px 5px; }"
                "table { margin: 20px auto; border-collapse: collapse; width: 60%; }"
                "th, td { border: 1px solid white; padding: 10px; }"
                "th { background-color: #333; }"
                "</style>"
                "</head>"
                "<body>"
                "<h1>GrANA WiFi Config</h1>"
                "<table>"
                "<thead>"
                "<tr>"
                "<th>Select</th>"
                "<th>SSID</th>"
                "<th>Signal Strength</th>"
                "</tr>"
                "</thead>"
                "<tbody>";
  html += "<form method='post' action='/save'>";

  // Adicionando as redes WiFi à tabela
  DynamicJsonDocument jsonBuffer(1000);
  deserializeJson(jsonBuffer, networksList);
  JsonArray networks = jsonBuffer["networks"].as<JsonArray>();
  int n = 5; // Limitar a 5 redes

  for (int i = 0; i < n && i < networks.size(); i++) {
    html += "<tr>";
    html += "<td><input type='checkbox' name='ssid' value='" + networks[i]["ssid"].as<String>() + "'></td>";
    html += "<td>" + networks[i]["ssid"].as<String>() + "</td>";
    html += "<td>" + String(networks[i]["rssi"].as<int>()) + " dBm</td>";
    html += "</tr>";
  }
  html += "</tbody>"
          "</table>";
  html += "<label for='password'>Password:</label><input type='text' name='password'><br>";
  html += "<label for='nomedobot'>NomeDoBOT:</label><input type='text' name='nomedobot'><br>";
  html += "<label for='usuario'>Usuario:</label><input type='text' name='usuario'><br>";
  html += "<input type='submit' value='CONECTAR'>";
  html += "</form>";
  html += "<a href='/delete'><button>Delete WiFi</button></a>"
          "<a href='/restart'><button>Restart</button></a>"
          "<a href='/scan'><button>Scan</button></a>"
          "</body>"
          "</html>";

  server.send(200, "text/html", html);
}

void handleSave() {
  ssid = server.arg("ssid");
  password = server.arg("password");
  username = server.arg("username");
  botname = server.arg("botname");

  ssid.trim();
  password.trim();
  username.trim();
  botname.trim();

  saveCredentials(ssid.c_str(), password.c_str(), username.c_str(), botname.c_str());
  server.send(200, "text/plain", "Credentials saved, restarting...");
  delay(1000);
  ESP.restart();
}

void handleDeleteCredentials() {
  if (SPIFFS.remove(CONFIG_FILE)) {
    server.send(200, "text/plain", "WiFi credentials deleted, restarting...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(500, "text/plain", "Failed to delete WiFi credentials");
  }
}

void loadCredentials() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }

  fs::File file = SPIFFS.open(CONFIG_FILE, FILE_READ);
  if (!file) {
    Serial.println("Failed to open config file for reading");
    return;
  }

  ssid = file.readStringUntil('\n');
  password = file.readStringUntil('\n');
  username = file.readStringUntil('\n');
  botname = file.readStringUntil('\n');

  ssid.trim();
  password.trim();
  username.trim();
  botname.trim();

  file.close();
}

void saveCredentials(const char* ssid, const char* password, const char* username, const char* botname) {
  fs::File file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  file.println(ssid);
  file.println(password);
  file.println(username);
  file.println(botname);
  file.close();
}

void deleteCredentials() {
  if (SPIFFS.remove(CONFIG_FILE)) {
    Serial.println("WiFi credentials deleted");
  } else {
    Serial.println("Failed to delete WiFi credentials");
  }
}
