/*
SmartDartboard_v8.5_Playable.ino
Playable 301/501/701 Dartboard
2 Players, Encoder + 3x4 Keypad
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Keypad.h>

#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4

#define ENC_CLK 34
#define ENC_DT 35
#define ENC_SW 32

#define GC9A01A_BLACK 0x0000

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {13,14,27,12};
byte colPins[COLS] = {25,33,15};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum Screen {
  MAIN_MENU,
  GAME_MENU,
  GAME_SCREEN,
  WIN_SCREEN
};

Screen currentScreen = MAIN_MENU;

const char* gameItems[] = {"301","501","701"};

int selectedItem = 0;
int currentGame = 501;

int p1Score = 501;
int p2Score = 501;
int currentPlayer = 1;

String entry = "";

uint8_t lastEncoderState = 0;
unsigned long lastEncoderTime = 0;
unsigned long buttonPressStart = 0;
bool longPressTriggered = false;

void startGame() {
  p1Score = currentGame;
  p2Score = currentGame;
  currentPlayer = 1;
  entry = "";
  currentScreen = GAME_SCREEN;
}

void drawMenu() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(2);
  tft.setCursor(20,20);
  tft.println("Select Game");

  for(int i=0;i<3;i++) {
    tft.setCursor(20,60 + i*30);
    tft.print(i==selectedItem ? "> " : "  ");
    tft.println(gameItems[i]);
  }
}

void drawGame() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(2);

  tft.setCursor(10,10);
  tft.print(currentGame);

  tft.setCursor(10,50);
  tft.print(currentPlayer==1?"> ":"  ");
  tft.print("P1: ");
  tft.println(p1Score);

  tft.setCursor(10,80);
  tft.print(currentPlayer==2?"> ":"  ");
  tft.print("P2: ");
  tft.println(p2Score);

  tft.setCursor(10,140);
  tft.print("Entry:");
  tft.println(entry);
}

void drawWinner() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20,100);
  tft.print("P");
  tft.print(currentPlayer);
  tft.println(" WINS!");
}

void redraw() {
  if(currentScreen == GAME_MENU || currentScreen == MAIN_MENU) drawMenu();
  else if(currentScreen == GAME_SCREEN) drawGame();
  else if(currentScreen == WIN_SCREEN) drawWinner();
}

void submitScore(int score) {
  if(currentPlayer == 1) {
    int remain = p1Score - score;
    if(remain < 0) {
      currentPlayer = 2;
      return;
    }
    p1Score = remain;
    if(remain == 0) { currentScreen = WIN_SCREEN; return; }
    currentPlayer = 2;
  } else {
    int remain = p2Score - score;
    if(remain < 0) {
      currentPlayer = 1;
      return;
    }
    p2Score = remain;
    if(remain == 0) { currentScreen = WIN_SCREEN; return; }
    currentPlayer = 1;
  }
}

void handleKeypad() {
  char key = keypad.getKey();
  if(!key) return;

  if(key >= '0' && key <= '9') entry += key;
  else if(key == '*') entry = "";
  else if(key == '#') {
    if(entry.length()) {
      submitScore(entry.toInt());
      entry = "";
    }
  }
}

void encoderMove(int dir) {
  selectedItem += dir;
  if(selectedItem < 0) selectedItem = 2;
  if(selectedItem > 2) selectedItem = 0;
}

void handleEncoder() {
  if(millis() - lastEncoderTime < 3) return;

  uint8_t state = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  if(state != lastEncoderState) {
    if((lastEncoderState == 0b00 && state == 0b01) ||
       (lastEncoderState == 0b01 && state == 0b11) ||
       (lastEncoderState == 0b11 && state == 0b10) ||
       (lastEncoderState == 0b10 && state == 0b00))
      encoderMove(1);

    if((lastEncoderState == 0b00 && state == 0b10) ||
       (lastEncoderState == 0b10 && state == 0b11) ||
       (lastEncoderState == 0b11 && state == 0b01) ||
       (lastEncoderState == 0b01 && state == 0b00))
      encoderMove(-1);

    lastEncoderState = state;
    lastEncoderTime = millis();
  }
}

void handleButton() {
  bool pressed = digitalRead(ENC_SW) == LOW;

  if(pressed && buttonPressStart == 0) {
    buttonPressStart = millis();
    longPressTriggered = false;
  }

  if(pressed && !longPressTriggered &&
     millis() - buttonPressStart > 1000) {
    currentScreen = MAIN_MENU;
    longPressTriggered = true;
  }

  if(!pressed && buttonPressStart > 0) {
    if(!longPressTriggered) {
      if(currentScreen == MAIN_MENU || currentScreen == GAME_MENU) {
        currentGame = (selectedItem==0)?301:(selectedItem==1)?501:701;
        startGame();
      } else if(currentScreen == WIN_SCREEN) {
        currentScreen = MAIN_MENU;
      }
    }
    buttonPressStart = 0;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT_PULLUP);

  tft.begin();
  currentScreen = MAIN_MENU;
  redraw();
}

void loop() {
  handleEncoder();
  handleButton();

  if(currentScreen == GAME_SCREEN)
    handleKeypad();

  static unsigned long lastDraw = 0;
  if(millis() - lastDraw > 100) {
    redraw();
    lastDraw = millis();
  }
}
