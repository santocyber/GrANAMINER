
void drawPattern(int patternIndex) {
  // Limpa a tela antes de desenhar o próximo padrão
 // tft.fillScreen(TFT_BLACK);
  
  // Chama uma função de desenho diferente com base no índice atual
  switch (patternIndex) {
    case 0:
      drawComplexPattern();
      break;
    case 1:
      drawToro3D();
      break;
    case 2:
      drawWaveCone();
      break;
    case 3:
      drawSineWavePattern();
      break;
    case 4:
      drawRotatingBars3D();
      break;
  }
}


int loadPatternIndex() {
  File file = SPIFFS.open("/patternIndex.txt", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return 0;
  }

  int patternIndex = file.parseInt();
  file.close();
  return patternIndex;
}

void savePatternIndex(int patternIndex) {
  File file = SPIFFS.open("/patternIndex.txt", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  file.println(patternIndex);
  file.close();
}



void drawComplexPattern() {
  int width = tft.width();
  int height = tft.height();
  int numStars = 100;  // Número de estrelas

  // Estrutura para armazenar a posição e velocidade das estrelas
  struct Star {
    float x, y, z;
  };

  Star stars[numStars];

  // Inicializa as estrelas em posições aleatórias
  for (int i = 0; i < numStars; i++) {
    stars[i].x = random(-width, width);
    stars[i].y = random(-height, height);
    stars[i].z = random(1, width);
  }

  // Inicializa a posição e velocidade do cometa
  float cometX = random(-width, width);
  float cometY = random(-height, height);
  float cometZ = width;
  float cometSpeed = 5.0;

  unsigned long startTime = millis();

  while (millis() - startTime < 10000) {  // Termina a função em 10 segundos
    // Desenha estrelas
    for (int i = 0; i < numStars; i++) {
      // Calcula a posição das estrelas em 3D
      float sx = stars[i].x / stars[i].z;
      float sy = stars[i].y / stars[i].z;

      // Projeção perspectiva
      int xPos = (width / 2) + sx * width;
      int yPos = (height / 2) + sy * height;

      // Gera uma cor baseada na profundidade
      uint16_t color = tft.color565(255 - stars[i].z * 255 / width, 255 - stars[i].z * 255 / width, 255);

      // Desenha a estrela na tela
      if (xPos >= 0 && xPos < width && yPos >= 0 && yPos < height) {
        tft.drawPixel(xPos, yPos, color);
      }

      // Move a estrela em direção à tela com aceleração
      stars[i].z -= 0.1 + (width - stars[i].z) / 100.0;

      // Reinicializa a estrela se sair da tela
      if (stars[i].z < 1) {
        stars[i].x = random(-width, width);
        stars[i].y = random(-height, height);
        stars[i].z = width;
      }
    }

    // Desenha o cometa
    float cometSX = cometX / cometZ;
    float cometSY = cometY / cometZ;
    int cometXPos = (width / 2) + cometSX * width;
    int cometYPos = (height / 2) + cometSY * height;

    // Gera uma cor para o cometa
    uint16_t cometColor = tft.color565(255, 255, 255);

    // Desenha o cometa na tela
    if (cometXPos >= 0 && cometXPos < width && cometYPos >= 0 && cometYPos < height) {
      tft.drawPixel(cometXPos, cometYPos, cometColor);
      for (int j = 1; j < 10; j++) {
        int trailXPos = cometXPos - j * (cometSX * width / cometZ);
        int trailYPos = cometYPos - j * (cometSY * height / cometZ);
        if (trailXPos >= 0 && trailXPos < width && trailYPos >= 0 && trailYPos < height) {
          uint16_t trailColor = tft.color565(255 - j * 25, 255 - j * 25, 255 - j * 25);
          tft.drawPixel(trailXPos, trailYPos, trailColor);
        }
      }
    }

    // Move o cometa
    cometZ -= cometSpeed;

    // Reinicializa o cometa se sair da tela
    if (cometZ < 1) {
      cometX = random(-width, width);
      cometY = random(-height, height);
      cometZ = width;
    }

    delay(30);  // Delay para criar o efeito de animação
  }
}



void drawToro3D() {
  float R = 100;  // Raio maior do toro triplicado
  float r = 40;  // Raio menor do toro triplicado
  int numSteps = 100;  // Número de passos para incrementar u e v
  float uStep = TWO_PI / numSteps;
  float vStep = TWO_PI / numSteps;
  
  float angleX = 0.5;  // Ângulo de rotação em torno do eixo X
  float angleY = 0.5;  // Ângulo de rotação em torno do eixo Y

  for (int uStepIndex = 0; uStepIndex < numSteps; uStepIndex++) {
    for (int vStepIndex = 0; vStepIndex < numSteps; vStepIndex++) {
      float u = uStepIndex * uStep;
      float v = vStepIndex * vStep;

      // Calcula as coordenadas 3D do toro
      float x = (R + r * cos(v)) * cos(u);
      float y = (R + r * cos(v)) * sin(u);
      float z = r * sin(v);

      // Aplica a rotação em torno do eixo X
      float yRot = y * cos(angleX) - z * sin(angleX);
      float zRot = y * sin(angleX) + z * cos(angleX);
      y = yRot;
      z = zRot;

      // Aplica a rotação em torno do eixo Y
      float xRot = x * cos(angleY) + z * sin(angleY);
      z = -x * sin(angleY) + z * cos(angleY);
      x = xRot;

      // Projeção perspectiva (ajuste a constante de projeção conforme necessário)
      float distance = 300;  // Distância do observador à tela ajustada
      float scale = distance / (distance + z);
      int xPos = (tft.width() / 2) + scale * x;
      int yPos = (tft.height() / 2) - scale * y;

      // Gera cor randômica
      uint16_t randomColor = random(0xFFFF);

      // Desenha o ponto na tela
      tft.drawPixel(xPos, yPos, randomColor);
    }
  }
}
void drawWaveCone() {
  int numSteps = 100;  // Número de passos para a grade
  float stepSize = 4.0;  // Tamanho de cada passo na grade

  float angleX = 0.5;  // Ângulo de rotação em torno do eixo X
  float angleY = 0.5;  // Ângulo de rotação em torno do eixo Y

  int width = tft.width();
  int height = tft.height();
  float distance = 200;  // Distância do observador à tela

  // Limpa a tela antes de desenhar o terreno

  // Desenhar múltiplos padrões de ondas
  for (int pattern = 0; pattern < 5; pattern++) {
    float offsetX = random(-width / 2, width / 2);
    float offsetY = random(-height / 2, height / 2);

    for (int xStep = 0; xStep < numSteps; xStep++) {
      for (int yStep = 0; yStep < numSteps; yStep++) {
        // Coordenadas na grade com deslocamento
        float x = xStep * stepSize - (numSteps * stepSize) / 2 + offsetX;
        float y = yStep * stepSize - (numSteps * stepSize) / 2 + offsetY;
        float z = 30 * sin(sqrt(x * x + y * y) / 10);  // Aumenta a função para maior profundidade

        // Aplica a rotação em torno do eixo X
        float yRot = y * cos(angleX) - z * sin(angleX);
        float zRot = y * sin(angleX) + z * cos(angleX);
        y = yRot;
        z = zRot;

        // Aplica a rotação em torno do eixo Y
        float xRot = x * cos(angleY) + z * sin(angleY);
        z = -x * sin(angleY) + z * cos(angleY);
        x = xRot;

        // Projeção perspectiva
        float scale = distance / (distance + z);
        int xPos = (width / 2) + scale * x;
        int yPos = (height / 2) - scale * y;

        // Gera uma cor baseada na altura para destacar picos e vales
        uint16_t color;
        if (z > 0) {
          color = tft.color565(255, 255 - (int)(z * 8.5), 0);  // Tons de amarelo para picos
        } else {
          color = tft.color565(0, 0, 255 + (int)(z * 8.5));  // Tons de azul para vales
        }

        // Desenha o ponto na tela
        tft.drawPixel(xPos, yPos, color);
      }
    }
    delay(500);  // Aumenta o delay para prolongar a duração do desenho
  }
}







void drawSineWavePattern() {
  int width = tft.width();
  int height = tft.height();
  float A = height / 4.0;  // Amplitude
  float f = 0.01;  // Frequência
  float phi = 0;  // Fase inicial

  // Desenha linhas senoidais em todo o display
  for (int x = 0; x < width; x++) {
    for (int t = 0; t < height; t++) {
      // Calcula a posição y com base na função senoidal
      float y = A * sin(2 * PI * f * t + phi);
      
      // Ajusta a coordenada y para a tela
      int yPos = height / 2 + y;
      
      // Gera cor randômica
      uint16_t randomColor = random(0xFFFF);
      
      // Desenha o ponto na tela
      tft.drawPixel(x, yPos, randomColor);
    }
  }
}
void drawRotatingBars3D() {
  int width = tft.width();
  int height = tft.height();
  float length = 100;  // Comprimento das barras
  float angles[5] = {0, 0.2, 0.4, 0.6, 0.8};  // Ângulos de rotação iniciais diferentes para cada barra

  // Inicializa as posições das barras em diferentes locais, principalmente nas laterais da tela
  float x[5] = {width / 4, width - width / 4, width / 4, width - width / 4, width / 2};
  float y[5] = {height / 4, height / 4, height - height / 4, height - height / 4, height / 2};

  for (int i = 0; i < 360; i += 5) {
    for (int j = 0; j < 5; j++) {
      angles[j] += 0.025;  // Incrementa o ângulo de rotação mais devagar para dobrar o tempo de giro

      float x1 = length * cos(angles[j]);
      float y1 = length * sin(angles[j]);
      float x2 = -x1;
      float y2 = -y1;

      // Movimenta a posição da barra aleatoriamente com maior amplitude
      x[j] += random(-10, 11);
      y[j] += random(-10, 11);

      // Mantém a barra dentro dos limites da tela
      if (x[j] < length / 2) x[j] = length / 2;
      if (x[j] > width - length / 2) x[j] = width - length / 2;
      if (y[j] < length / 2) y[j] = length / 2;
      if (y[j] > height - length / 2) y[j] = height - length / 2;

      // Translada as coordenadas para a posição atual
      int x1Pos = x[j] + x1;
      int y1Pos = y[j] + y1;
      int x2Pos = x[j] + x2;
      int y2Pos = y[j] + y2;

      // Gera uma cor randômica
      uint16_t randomColor = random(0xFFFF);

      // Desenha a barra com a cor randômica
      tft.drawLine(x1Pos, y1Pos, x2Pos, y2Pos, randomColor);
    }

    // Delay para criar o efeito de animação
    delay(100);
  }
}
