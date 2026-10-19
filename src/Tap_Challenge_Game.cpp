#include <Arduino.h>
#include <SPI.h>
#include <bb_spi_lcd.h>
#include <XPT2046_Touchscreen.h>
#include <EEPROM.h>
#include <math.h>

#define XPT2046_IRQ 36
#define XPT2046_CS 33
#define EEPROM_SIZE 128

BB_SPI_LCD lcd;
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Game States
enum GameState { STATE_MENU, STATE_PLAYING, STATE_GAMEOVER, STATE_INITIALS, STATE_RESET_CONFIRM };
GameState currentState = STATE_MENU;

// Game Variables
int score = 0;
unsigned long gameStartTime = 0;
const unsigned long GAME_DURATION = 20000; // 20 seconds

// Target Properties
int targetX = 160, targetY = 120;
int targetRadius = 18;
int targetSides = 3; 
uint16_t targetColor = TFT_GREEN;

// Hazard Properties
int hazardX = -50, hazardY = -50;
int hazardSize = 30;
bool hasActiveHazard = false;
bool wasTouched = false;

// Touch Hold Timing Variables
unsigned long touchStartTime = 0;
bool isHoldingHighScore = false;
int lastDrawnSecond = -1;

// High Score Data Structure (Top 3 Scores)
struct HighScoreEntry {
  char initials[4];
  int score;
};

HighScoreEntry highScores[3] = {
  {"AAA", 0},
  {"BBB", 0},
  {"CCC", 0}
};

// Initials Entry Variables
char currentInitials[4] = "AAA";
int activeLetterIdx = 0;

void loadHighScores() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, highScores);
  for (int i = 0; i < 3; i++) {
    if (highScores[i].score < 0 || highScores[i].score > 999) {
      strcpy(highScores[i].initials, "---");
      highScores[i].score = 0;
    }
  }
}

void resetHighScores() {
  for (int i = 0; i < 3; i++) {
    strcpy(highScores[i].initials, "---");
    highScores[i].score = 0;
  }
  EEPROM.put(0, highScores);
  EEPROM.commit();
}

bool checkAndSaveHighScore() {
  for (int i = 0; i < 3; i++) {
    if (score > highScores[i].score) {
      for (int j = 2; j > i; j--) {
        highScores[j] = highScores[j - 1];
      }
      highScores[i].score = score;
      highScores[i].initials[0] = currentInitials[0];
      highScores[i].initials[1] = currentInitials[1];
      highScores[i].initials[2] = currentInitials[2];
      highScores[i].initials[3] = '\0';
      EEPROM.put(0, highScores);
      EEPROM.commit();
      return true;
    }
  }
  return false;
}

void updateHUD() {
  lcd.fillRect(10, 5, 300, 16, TFT_BLACK);
  
  lcd.setCursor(10, 5);
  lcd.setFont(FONT_12x16);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.printf("Score: %d", score);
  
  int timeLeft = 20 - ((millis() - gameStartTime) / 1000);
  if (timeLeft < 0) timeLeft = 0;
  
  lcd.setCursor(200, 5);
  lcd.printf("Time: %02ds", timeLeft);
}

void fillRegularPolygon(int cx, int cy, int r, int sides, uint16_t color) {
  float angleStep = 2.0 * 3.14159265 / sides;
  for (int i = 0; i < sides; i++) {
    float a1 = i * angleStep - 3.14159265 / 2;
    float a2 = (i + 1) * angleStep - 3.14159265 / 2;
    int x1 = cx + (int)(r * cos(a1));
    int y1 = cy + (int)(r * sin(a1));
    int x2 = cx + (int)(r * cos(a2));
    int y2 = cy + (int)(r * sin(a2));
    lcd.fillTriangle(cx, cy, x1, y1, x2, y2, color);
  }
}

void drawMenuScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(FONT_12x16);
  
  // Title "CYD TAP CHALLENGE" (17 chars * 12px = 204px wide -> X: (320-204)/2 = 58)
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setCursor(58, 15);
  lcd.print("CYD TAP CHALLENGE");
  
  // Header "HIGH SCORES:" (12 chars * 12px = 144px wide -> X: (320-144)/2 = 88)
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.setCursor(88, 45);
  lcd.print("HIGH SCORES:");
  
  // Centered High Score Rows (~14 chars average -> ~168px wide -> X: 76)
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 3; i++) {
    lcd.setCursor(76, 68 + (i * 20));
    lcd.printf("%d. %s - %d pts", i + 1, highScores[i].initials, highScores[i].score);
  }
  
  // Start Button
  lcd.fillRect(80, 155, 160, 40, TFT_BLUE);
  lcd.setTextColor(TFT_WHITE, TFT_BLUE);
  lcd.setCursor(88, 167);
  lcd.print("TAP TO START");
}

void drawResetConfirmScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(FONT_12x16);
  
  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.setCursor(52, 35);
  lcd.print("RESET HIGH SCORES?");
  
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(58, 75);
  lcd.print("Hold Reset for 2s");
  
  lcd.fillRect(40, 150, 110, 40, TFT_GREY);
  lcd.setTextColor(TFT_WHITE, TFT_GREY);
  lcd.setCursor(59, 162);
  lcd.print("CANCEL");
  
  lcd.fillRect(170, 150, 110, 40, TFT_RED);
  lcd.setTextColor(TFT_WHITE, TFT_RED);
  lcd.setCursor(195, 162);
  lcd.print("RESET");
}

void spawnTarget() {
  lcd.fillRect(targetX - targetRadius - 5, targetY - targetRadius - 5, (targetRadius * 2) + 10, (targetRadius * 2) + 10, TFT_BLACK);
  
  if (hasActiveHazard) {
    lcd.fillRect(hazardX - 5, hazardY - 5, hazardSize + 10, hazardSize + 10, TFT_BLACK);
    hasActiveHazard = false;
  }
  
  if (random(0, 100) < 25) {
    hazardX = random(30, 260);
    hazardY = random(45, 185);
    hasActiveHazard = true;
    lcd.fillCircle(hazardX + hazardSize/2, hazardY + hazardSize/2, hazardSize/2, TFT_RED);
  }
  
  targetSides = random(3, 7);
  targetRadius = random(15, 25);
  
  uint16_t goodColors[] = {TFT_GREEN, TFT_BLUE, TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_ORANGE};
  targetColor = goodColors[random(0, 6)];
  
  targetX = random(40, 280);
  targetY = random(45, 195);
  
  fillRegularPolygon(targetX, targetY, targetRadius, targetSides, targetColor);
}

void startNewGame() {
  score = 0;
  hasActiveHazard = false;
  gameStartTime = millis();
  lastDrawnSecond = 20;
  currentState = STATE_PLAYING;
  lcd.fillScreen(TFT_BLACK);
  
  updateHUD();
  spawnTarget();
}

void triggerGameOver(bool hitHazard) {
  lcd.fillScreen(TFT_BLACK);
  delay(1000);
  
  currentState = STATE_GAMEOVER;
  lcd.fillScreen(TFT_BLACK);
  
  lcd.setFont(FONT_12x16);
  if (hitHazard) {
    lcd.setCursor(94, 30);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.print("HIT HAZARD!");
  } else {
    lcd.setCursor(108, 30);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.print("TIME UP!");
  }
  
  lcd.setCursor(94, 75);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.print("Final Score:");
  
  char scoreBuf[10];
  sprintf(scoreBuf, "%d", score);
  int scoreX = 160 - (strlen(scoreBuf) * 6);
  lcd.setCursor(scoreX, 100);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.print(scoreBuf);
  
  lcd.fillRect(60, 155, 200, 40, TFT_BLUE);
  lcd.setTextColor(TFT_WHITE, TFT_BLUE);
  lcd.setCursor(70, 167);
  lcd.print("TAP TO CONTINUE");
}

void drawInitialsScreen() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(FONT_12x16);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.setCursor(65, 25);
  lcd.print("NEW HIGH SCORE!");
  
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(95, 60);
  lcd.printf("Score: %d", score);
  
  lcd.setCursor(60, 95);
  lcd.print("Enter Initials:");
  
  for (int i = 0; i < 3; i++) {
    int boxX = 95 + (i * 48);
    if (i == activeLetterIdx) {
      lcd.fillRect(boxX, 125, 35, 35, TFT_GREY);
    } else {
      lcd.drawRect(boxX, 125, 35, 35, TFT_WHITE);
    }
    lcd.setCursor(boxX + 11, 134);
    lcd.setTextColor(TFT_GREEN, i == activeLetterIdx ? TFT_GREY : TFT_BLACK);
    lcd.printf("%c", currentInitials[i]);
  }
  
  lcd.fillRect(30, 180, 75, 32, TFT_RED);
  lcd.setCursor(50, 189);
  lcd.setTextColor(TFT_WHITE, TFT_RED);
  lcd.print("PREV");
  
  lcd.fillRect(122, 180, 75, 32, TFT_RED);
  lcd.setCursor(140, 189);
  lcd.setTextColor(TFT_WHITE, TFT_RED);
  lcd.print("NEXT");
  
  lcd.fillRect(215, 180, 75, 32, TFT_GREEN);
  lcd.setCursor(233, 189);
  lcd.setTextColor(TFT_BLACK, TFT_GREEN);
  lcd.print("DONE");
}

void setup() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH); // Force backlight on

  Serial.begin(115200);
  randomSeed(analogRead(0));
  loadHighScores();
  
  lcd.begin(DISPLAY_CYD_24R);
  lcd.setRotation(1);
  lcd.fillScreen(TFT_BLACK);
  
  ts.begin();
  ts.setRotation(1);
  
  drawMenuScreen();
}

void loop() {
  bool currentTouched = ts.touched();
  TS_Point p;
  int screenX = 0, screenY = 0;
  
  if (currentTouched) {
    p = ts.getPoint();
    screenX = constrain(map(p.y, 275, 3700, 0, 319), 0, 319);
    screenY = constrain(map(p.x, 280, 3800, 0, 239), 0, 239);
  }

  if (currentState == STATE_MENU) {
    if (currentTouched) {
      if (!isHoldingHighScore) {
        if (screenY >= 40 && screenY <= 130 && screenX >= 50 && screenX <= 270) {
          isHoldingHighScore = true;
          touchStartTime = millis();
        }
      } else {
        if (millis() - touchStartTime >= 2000) {
          isHoldingHighScore = false;
          currentState = STATE_RESET_CONFIRM;
          drawResetConfirmScreen();
        }
      }
    } else {
      isHoldingHighScore = false;
    }
  }
  else if (currentState == STATE_RESET_CONFIRM) {
    if (currentTouched) {
      if (!wasTouched) {
        wasTouched = true;
        if (screenX >= 40 && screenX <= 150 && screenY >= 150 && screenY <= 190) {
          currentState = STATE_MENU;
          drawMenuScreen();
          isHoldingHighScore = false;
          return;
        }
      }
      
      if (screenX >= 170 && screenX <= 280 && screenY >= 150 && screenY <= 190) {
        if (!isHoldingHighScore) {
          isHoldingHighScore = true;
          touchStartTime = millis();
        } else if (millis() - touchStartTime >= 2000) {
          isHoldingHighScore = false;
          resetHighScores();
          currentState = STATE_MENU;
          drawMenuScreen();
        }
      } else {
        isHoldingHighScore = false;
      }
    } else {
      isHoldingHighScore = false;
    }
  }

  if (currentTouched && !wasTouched) {
    wasTouched = true;

    if (currentState == STATE_MENU) {
      if (screenX >= 80 && screenX <= 240 && screenY >= 155 && screenY <= 195) {
        startNewGame();
      }
    } 
    else if (currentState == STATE_PLAYING) {
      bool hitHazard = false;
      if (hasActiveHazard) {
        int hCenterX = hazardX + hazardSize/2;
        int hCenterY = hazardY + hazardSize/2;
        int distSq = (screenX - hCenterX) * (screenX - hCenterX) + (screenY - hCenterY) * (screenY - hCenterY);
        if (distSq <= (hazardSize/2) * (hazardSize/2)) {
          hitHazard = true;
        }
      }

      if (hitHazard) {
        triggerGameOver(true);
      }
      else {
        int distSq = (screenX - targetX) * (screenX - targetX) + (screenY - targetY) * (screenY - targetY);
        if (distSq <= targetRadius * targetRadius) {
          score++;
          updateHUD();
          spawnTarget();
        }
      }
    } 
    else if (currentState == STATE_GAMEOVER) {
      if (screenX >= 60 && screenX <= 260 && screenY >= 155 && screenY <= 195) {
        if (score > highScores[2].score) { 
          currentInitials[0] = 'A'; currentInitials[1] = 'A'; currentInitials[2] = 'A'; currentInitials[3] = '\0';
          activeLetterIdx = 0;
          currentState = STATE_INITIALS;
          drawInitialsScreen();
        } else {
          currentState = STATE_MENU;
          drawMenuScreen();
        }
      }
    } 
    else if (currentState == STATE_INITIALS) {
      if (screenY >= 125 && screenY <= 160) {
        currentInitials[activeLetterIdx]++;
        if (currentInitials[activeLetterIdx] > 'Z') currentInitials[activeLetterIdx] = 'A';
        drawInitialsScreen();
      } 
      else if (screenY >= 180 && screenY <= 212) {
        if (screenX >= 30 && screenX <= 105) { // PREV
          activeLetterIdx = (activeLetterIdx - 1 + 3) % 3;
          drawInitialsScreen();
        } 
        else if (screenX >= 122 && screenX <= 197) { // NEXT
          activeLetterIdx = (activeLetterIdx + 1 + 3) % 3;
          drawInitialsScreen();
        } 
        else if (screenX >= 215 && screenX <= 290) { // DONE
          checkAndSaveHighScore();
          currentState = STATE_MENU;
          drawMenuScreen();
        }
      }
    }
  } else if (!currentTouched) {
    wasTouched = false;
  }

  // Handle Timer & HUD updates synchronously
  if (currentState == STATE_PLAYING) {
    unsigned long elapsed = millis() - gameStartTime;
    if (elapsed >= GAME_DURATION) {
      triggerGameOver(false);
    } else {
      int timeLeft = 20 - (elapsed / 1000);
      if (timeLeft != lastDrawnSecond) {
        lastDrawnSecond = timeLeft;
        updateHUD();
      }
    }
  }

  delay(20);
}