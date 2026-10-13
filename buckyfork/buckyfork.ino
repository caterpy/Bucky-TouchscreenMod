#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <BleKeyboard.h>
#include <Preferences.h>
#include <esp_gap_ble_api.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <esp_system.h>

#define TFT_BACKLIGHT_PIN 21
#define TOUCH_IRQ_PIN 36
#define TOUCH_MOSI_PIN 32
#define TOUCH_MISO_PIN 39
#define TOUCH_CLK_PIN 25
#define TOUCH_CS_PIN 33

#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3900
#define TOUCH_RAW_Y_MIN 240
#define TOUCH_RAW_Y_MAX 3800

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define COLOR_BACKGROUND 0x0861
#define COLOR_HEADER 0x10A2
#define COLOR_BUTTON 0x249F
#define COLOR_SECONDARY 0x4416
#define COLOR_SUCCESS 0x0600
#define COLOR_WARNING 0xFD20
#define COLOR_DANGER 0xD904
#define COLOR_TEXT TFT_WHITE

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
Preferences preferences;
BleKeyboard* bleKeyboard = nullptr;

struct GuiButton {
  int16_t x, y, width, height;
  const char* label;
  uint16_t color;
  uint8_t id;
};

enum ScreenID {
  SCREEN_HOME,
  SCREEN_TEXT,
  SCREEN_KEYBOARD,
  SCREEN_QUICK_KEYS,
  SCREEN_SCRIPTS,
  SCREEN_RENAME
};

enum KeyboardMode {
  KEYBOARD_TEXT,
  KEYBOARD_NAME
};

ScreenID currentScreen = SCREEN_HOME;
KeyboardMode keyboardMode = KEYBOARD_TEXT;

String deviceName = "Keyboard";
String textBuffer = "";
String nameBuffer = "";
String statusText = "Starting...";

bool previousBluetoothState = false;
bool previousTouchState = false;
bool scriptRunning = false;
bool stopRequested = false;
bool keyboardUppercase = false;

unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 140;

bool screenIsOff = false;
bool screenOffTouchHeld = false;
uint8_t wakeTapCount = 0;
unsigned long firstWakeTapTime = 0;
unsigned long lastWakeTapTime = 0;
const unsigned long WAKE_TAP_WINDOW_MS = 4000;
const unsigned long WAKE_TAP_DEBOUNCE_MS = 130;

const char* keyboardRowsLower[] = {
  "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"
};
const char* keyboardRowsUpper[] = {
  "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"
};

const char* SCRIPT_TYPE_HELLO =
  "STRING Hello this is a test\nENTER\n";
const char* SCRIPT_OPEN_NOTEPAD =
  "WIN r\nDELAY 500\nSTRING notepad\nENTER\nDELAY 1000\n"
  "STRING Touchscreen is working!\n";
const char* SCRIPT_OPEN_CMD =
  "WIN r\nDELAY 500\nSTRING cmd.exe\nENTER\n";
const char* SCRIPT_SHOW_DESKTOP = "WIN d\n";
const char* SCRIPT_OPEN_EXPLORER = "WIN e\n";
const char* SCRIPT_LOCK_COMPUTER = "WIN l\n";

void showHomeScreen();
void showTextScreen();
void showKeyboardScreen();
void showQuickKeysScreen();
void showScriptsScreen();
void showRenameScreen();
void redrawCurrentScreen();
void drawHeader(const char* title);
void drawBluetoothIndicator();
void drawFooter();
void drawStatus(const String& message);
void drawButton(const GuiButton& button);
void drawTextPreview();
bool pointInside(int x, int y, const GuiButton& button);
bool readTouch(int& screenX, int& screenY);
void handleTouch();
void handleHomeTouch(int x, int y);
void handleTextTouch(int x, int y);
void handleKeyboardTouch(int x, int y);
void handleQuickKeysTouch(int x, int y);
void handleScriptsTouch(int x, int y);
void handleRenameTouch(int x, int y);
void executeCommand(String command);
void executeSingleCommand(String command);
void executeScript(const char* scriptText);
void pressKey(uint8_t key);
void pressCombination(uint8_t modifier, uint8_t key);
void pressTripleCombination(uint8_t modifier1, uint8_t modifier2, uint8_t key);
void appendKeyboardCharacter(char character);
void removeLastCharacter();
void waitWithTouch(unsigned long milliseconds);
void applySavedBluetoothAddress();
void generateNewBluetoothAddress(uint8_t* address);
bool saveNewBluetoothAddress();
void printBluetoothAddress(const char* label, const uint8_t* address);
void unpairAndChangeAddress();
void loadSettings();
bool saveDeviceName(const String& newName);
void turnScreenOff();
void turnScreenOn();
void handleScreenOffTouch();

GuiButton homeButton = {5, 203, 62, 32, "HOME", COLOR_SECONDARY, 250};
GuiButton stopButton = {245, 203, 70, 32, "STOP", COLOR_DANGER, 251};

bool bluetoothConnected() {
  return bleKeyboard != nullptr && bleKeyboard->isConnected();
}

void printBluetoothAddress(const char* label, const uint8_t* address) {
  Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n", label,
    address[0], address[1], address[2], address[3], address[4], address[5]);
}

void generateNewBluetoothAddress(uint8_t* address) {
  for (int i = 0; i < 6; i++) address[i] = (uint8_t)(esp_random() & 0xFF);
  address[0] = (address[0] & 0xFC) | 0x02;
  if (address[1] == 0 && address[2] == 0 && address[3] == 0 &&
      address[4] == 0 && address[5] == 0) address[5] = 1;
}

bool saveNewBluetoothAddress() {
  uint8_t address[6];
  generateNewBluetoothAddress(address);
  printBluetoothAddress("New Bluetooth address:", address);
  preferences.begin("bucky-bt", false);
  size_t written = preferences.putBytes("bt-mac", address, sizeof(address));
  preferences.end();
  return written == sizeof(address);
}

void applySavedBluetoothAddress() {
  uint8_t address[6];
  preferences.begin("bucky-bt", true);
  size_t length = preferences.getBytesLength("bt-mac");
  if (length == sizeof(address)) {
    preferences.getBytes("bt-mac", address, sizeof(address));
    preferences.end();
    printBluetoothAddress("Applying Bluetooth address:", address);
    esp_err_t result = esp_iface_mac_addr_set(address, ESP_MAC_BT);
    Serial.printf("Bluetooth address result: %s\n", esp_err_to_name(result));
  } else {
    preferences.end();
    Serial.println("Using factory-derived Bluetooth address");
  }
}

void loadSettings() {
  preferences.begin("bucky-cfg", true);
  deviceName = preferences.getString("name", "Keyboard");
  preferences.end();
  deviceName.trim();
  if (deviceName.length() == 0) deviceName = "Keyboard";
  if (deviceName.length() > 15) deviceName = deviceName.substring(0, 15);
}

bool saveDeviceName(const String& newName) {
  String cleaned = newName;
  cleaned.trim();
  if (cleaned.length() == 0) return false;
  if (cleaned.length() > 15) cleaned = cleaned.substring(0, 15);
  preferences.begin("bucky-cfg", false);
  size_t written = preferences.putString("name", cleaned);
  preferences.end();
  if (written > 0) {
    deviceName = cleaned;
    return true;
  }
  return false;
}

void turnScreenOff() {
  screenIsOff = true;
  screenOffTouchHeld = false;
  wakeTapCount = 0;
  firstWakeTapTime = 0;
  lastWakeTapTime = 0;
  unsigned long started = millis();
  while (touchscreen.touched() && millis() - started < 1500) delay(10);
  previousTouchState = false;
  digitalWrite(TFT_BACKLIGHT_PIN, LOW);
  Serial.println("Screen off. Tap five times to wake.");
}

void turnScreenOn() {
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  screenIsOff = false;
  screenOffTouchHeld = false;
  wakeTapCount = 0;
  firstWakeTapTime = 0;
  lastWakeTapTime = 0;
  previousTouchState = false;
  lastTouchTime = millis();
  statusText = bluetoothConnected() ? "Bluetooth connected" : "Waiting for Bluetooth";
  showHomeScreen();
}

void handleScreenOffTouch() {
  bool touched = touchscreen.touched();
  if (touched) {
    if (screenOffTouchHeld) return;
    screenOffTouchHeld = true;
    unsigned long now = millis();
    if (wakeTapCount == 0 || now - firstWakeTapTime > WAKE_TAP_WINDOW_MS) {
      wakeTapCount = 1;
      firstWakeTapTime = now;
      lastWakeTapTime = now;
      return;
    }
    if (now - lastWakeTapTime < WAKE_TAP_DEBOUNCE_MS) return;
    wakeTapCount++;
    lastWakeTapTime = now;
    if (wakeTapCount >= 5) turnScreenOn();
    return;
  }
  screenOffTouchHeld = false;
  if (wakeTapCount > 0 && millis() - firstWakeTapTime > WAKE_TAP_WINDOW_MS) {
    wakeTapCount = 0;
    firstWakeTapTime = 0;
    lastWakeTapTime = 0;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.setTextWrap(false);
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, COLOR_BACKGROUND);
  tft.drawString(":3", 160, 90);
  tft.setTextFont(2);
  tft.drawString("Starting...", 160, 125);

  touchSPI.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(1);

  loadSettings();
  applySavedBluetoothAddress();
  bleKeyboard = new BleKeyboard(deviceName.c_str(), deviceName.c_str(), 100);
  bleKeyboard->begin();

  previousBluetoothState = bluetoothConnected();
  statusText = previousBluetoothState ? "Bluetooth connected" : "Pair " + deviceName;
  delay(700);
  showHomeScreen();
}

void loop() {
  if (screenIsOff) {
    handleScreenOffTouch();
    delay(10);
    return;
  }

  bool connected = bluetoothConnected();
  if (connected != previousBluetoothState) {
    previousBluetoothState = connected;
    statusText = connected ? "Bluetooth connected" : "Waiting for Bluetooth";
    redrawCurrentScreen();
  }

  handleTouch();
  delay(5);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_WIDTH, 48, COLOR_HEADER);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.drawString(title, 8, 24);
  drawBluetoothIndicator();
}

void drawBluetoothIndicator() {
  bool connected = bluetoothConnected();
  uint16_t color = connected ? COLOR_SUCCESS : COLOR_DANGER;
  tft.fillRoundRect(226, 10, 87, 28, 7, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawString(connected ? "CONNECTED" : "WAITING", 269, 24);
}

void drawFooter() {
  tft.fillRect(72, 198, 168, 42, COLOR_HEADER);
  String visible = statusText;
  if (visible.length() > 23) visible = visible.substring(0, 20) + "...";
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.drawString(visible, 156, 219);
}

void drawStatus(const String& message) {
  statusText = message;
  drawFooter();
}

void drawButton(const GuiButton& button) {
  tft.fillRoundRect(button.x, button.y, button.width, button.height, 8, button.color);
  tft.drawRoundRect(button.x, button.y, button.width, button.height, 8, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, button.color);
  tft.drawString(button.label, button.x + button.width / 2, button.y + button.height / 2);
}

bool pointInside(int x, int y, const GuiButton& button) {
  return x >= button.x && x < button.x + button.width &&
         y >= button.y && y < button.y + button.height;
}

void showHomeScreen() {
  currentScreen = SCREEN_HOME;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader("Meow");

  GuiButton buttons[] = {
    {8,54,148,42,"TYPE TEXT",COLOR_BUTTON,1},
    {164,54,148,42,"QUICK KEYS",COLOR_BUTTON,2},
    {8,104,148,42,"SCRIPTS",COLOR_SECONDARY,3},
    {164,104,148,42,"RENAME",COLOR_SECONDARY,4},
    {8,154,148,42,"RESET",COLOR_DANGER,5},
    {164,154,148,42,"SCREEN OFF",COLOR_SECONDARY,6}
  };
  for (int i = 0; i < 6; i++) drawButton(buttons[i]);

  tft.fillRect(0, 198, SCREEN_WIDTH, 42, COLOR_HEADER);
  String visible = statusText;
  if (visible.length() > 38) visible = visible.substring(0, 35) + "...";
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_HEADER);
  tft.drawString(visible, 160, 219);
}

void showTextScreen() {
  currentScreen = SCREEN_TEXT;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader("TYPE TEXT");
  drawTextPreview();
  GuiButton buttons[] = {
    {8,108,95,38,"KEYBOARD",COLOR_BUTTON,1},
    {112,108,95,38,"SPACE",COLOR_SECONDARY,2},
    {216,108,96,38,"BACKSPACE",COLOR_SECONDARY,3},
    {8,154,95,38,"CLEAR",COLOR_DANGER,4},
    {112,154,95,38,"SEND",COLOR_SUCCESS,5},
    {216,154,96,38,"SEND+ENTER",COLOR_SUCCESS,6}
  };
  for (int i = 0; i < 6; i++) drawButton(buttons[i]);
  drawFooter();
  drawButton(homeButton);
}

void drawTextPreview() {
  tft.fillRoundRect(8, 55, 304, 45, 7, TFT_WHITE);
  String preview = textBuffer.length() ? textBuffer : "Tap KEYBOARD to enter text";
  if (preview.length() > 40) preview = "..." + preview.substring(preview.length() - 37);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(preview, 14, 77);
}

void showRenameScreen() {
  currentScreen = SCREEN_RENAME;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader("RENAME");
  tft.fillRoundRect(8, 58, 304, 45, 7, TFT_WHITE);
  String preview = nameBuffer.length() ? nameBuffer : deviceName;
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(preview, 14, 80);
  GuiButton buttons[] = {
    {8,116,95,45,"KEYBOARD",COLOR_BUTTON,1},
    {112,116,95,45,"CLEAR",COLOR_DANGER,2},
    {216,116,96,45,"SAVE",COLOR_SUCCESS,3}
  };
  for (int i = 0; i < 3; i++) drawButton(buttons[i]);
  drawFooter();
  drawButton(homeButton);
}

void showKeyboardScreen() {
  currentScreen = SCREEN_KEYBOARD;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader(keyboardMode == KEYBOARD_NAME ? "NAME" : "KEYBOARD");

  String& active = keyboardMode == KEYBOARD_NAME ? nameBuffer : textBuffer;
  String preview = active;
  if (preview.length() > 38) preview = "..." + preview.substring(preview.length() - 35);
  tft.fillRect(4, 50, 312, 24, TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(preview.length() ? preview : "Text is empty", 8, 62);

  const char** rows = keyboardUppercase ? keyboardRowsUpper : keyboardRowsLower;
  for (int row = 0; row < 2; row++) {
    int y = 78 + row * 31;
    for (int col = 0; col < 10; col++) {
      int x = 5 + col * 31;
      char label[2] = {rows[row][col], '\0'};
      GuiButton b = {(int16_t)x,(int16_t)y,30,27,label,COLOR_BUTTON,0};
      drawButton(b);
    }
  }
  for (int col = 0; col < 9; col++) {
    int x = 20 + col * 32;
    char label[2] = {rows[2][col], '\0'};
    GuiButton b = {(int16_t)x,140,30,27,label,COLOR_BUTTON,0};
    drawButton(b);
  }
  for (int col = 0; col < 7; col++) {
    int x = 50 + col * 32;
    char label[2] = {rows[3][col], '\0'};
    GuiButton b = {(int16_t)x,171,30,27,label,COLOR_BUTTON,0};
    drawButton(b);
  }

  GuiButton lower[] = {
    {4,203,55,32,"HOME",COLOR_SECONDARY,1},
    {63,203,55,32,"SHIFT",keyboardUppercase ? COLOR_WARNING : COLOR_SECONDARY,2},
    {122,203,55,32,"SPACE",COLOR_SECONDARY,3},
    {181,203,55,32,"DEL",COLOR_DANGER,4},
    {240,203,76,32,keyboardMode == KEYBOARD_NAME ? "DONE" : "SEND",COLOR_SUCCESS,5}
  };
  for (int i = 0; i < 5; i++) drawButton(lower[i]);
}

void showQuickKeysScreen() {
  currentScreen = SCREEN_QUICK_KEYS;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader("QUICK KEYS");
  GuiButton buttons[] = {
    {7,55,72,38,"WIN",COLOR_BUTTON,1},{86,55,72,38,"WIN+R",COLOR_BUTTON,2},
    {165,55,72,38,"WIN+D",COLOR_BUTTON,3},{244,55,69,38,"WIN+L",COLOR_BUTTON,4},
    {7,101,72,38,"ENTER",COLOR_SECONDARY,5},{86,101,72,38,"SPACE",COLOR_SECONDARY,6},
    {165,101,72,38,"TAB",COLOR_SECONDARY,7},{244,101,69,38,"ESC",COLOR_SECONDARY,8},
    {7,147,72,38,"CTRL+A",COLOR_BUTTON,9},{86,147,72,38,"CTRL+C",COLOR_BUTTON,10},
    {165,147,72,38,"CTRL+V",COLOR_BUTTON,11},{244,147,69,38,"ALT+TAB",COLOR_BUTTON,12}
  };
  for (int i = 0; i < 12; i++) drawButton(buttons[i]);
  drawFooter();
  drawButton(homeButton);
}

void showScriptsScreen() {
  currentScreen = SCREEN_SCRIPTS;
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader("SCRIPTS");
  GuiButton buttons[] = {
    {8,56,96,48,"HELLO",COLOR_BUTTON,1},{112,56,96,48,"NOTEPAD",COLOR_BUTTON,2},
    {216,56,96,48,"CMD",COLOR_BUTTON,3},{8,113,96,48,"DESKTOP",COLOR_SECONDARY,4},
    {112,113,96,48,"EXPLORER",COLOR_SECONDARY,5},{216,113,96,48,"LOCK PC",COLOR_WARNING,6}
  };
  for (int i = 0; i < 6; i++) drawButton(buttons[i]);
  drawFooter();
  drawButton(homeButton);
  drawButton(stopButton);
}

bool readTouch(int& screenX, int& screenY) {
  if (!touchscreen.touched()) return false;
  TS_Point point = touchscreen.getPoint();
  if (point.z < 200) return false;
  screenX = constrain(map(point.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
  screenY = constrain(map(point.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
  return true;
}

void handleTouch() {
  int x, y;
  bool touched = readTouch(x, y);
  if (!touched) {
    previousTouchState = false;
    return;
  }
  if (previousTouchState || millis() - lastTouchTime < TOUCH_DEBOUNCE_MS) return;
  previousTouchState = true;
  lastTouchTime = millis();

  switch (currentScreen) {
    case SCREEN_HOME: handleHomeTouch(x,y); break;
    case SCREEN_TEXT: handleTextTouch(x,y); break;
    case SCREEN_KEYBOARD: handleKeyboardTouch(x,y); break;
    case SCREEN_QUICK_KEYS: handleQuickKeysTouch(x,y); break;
    case SCREEN_SCRIPTS: handleScriptsTouch(x,y); break;
    case SCREEN_RENAME: handleRenameTouch(x,y); break;
  }
}

void handleHomeTouch(int x, int y) {
  if (y >= 54 && y <= 96) {
    if (x < 160) showTextScreen(); else showQuickKeysScreen();
    return;
  }
  if (y >= 104 && y <= 146) {
    if (x < 160) showScriptsScreen(); else showRenameScreen();
    return;
  }
  if (y >= 154 && y <= 196) {
    if (x < 160) unpairAndChangeAddress(); else turnScreenOff();
  }
}

void handleTextTouch(int x, int y) {
  if (pointInside(x,y,homeButton)) { showHomeScreen(); return; }
  if (y >= 108 && y <= 146) {
    if (x < 104) {
      keyboardMode = KEYBOARD_TEXT;
      showKeyboardScreen();
    } else if (x < 212) {
      textBuffer += " ";
      drawTextPreview();
    } else {
      removeLastCharacter();
      drawTextPreview();
    }
    return;
  }
  if (y >= 154 && y <= 192) {
    if (x < 104) {
      textBuffer = "";
      drawTextPreview();
      drawStatus("Text cleared");
    } else if (!bluetoothConnected()) {
      drawStatus("Bluetooth not connected");
    } else if (!textBuffer.length()) {
      drawStatus("Text is empty");
    } else if (x < 212) {
      bleKeyboard->print(textBuffer);
      drawStatus("Text sent");
    } else {
      bleKeyboard->print(textBuffer);
      bleKeyboard->write(KEY_RETURN);
      drawStatus("Text and Enter sent");
    }
  }
}

void handleRenameTouch(int x, int y) {
  if (pointInside(x,y,homeButton)) { showHomeScreen(); return; }
  if (y >= 116 && y <= 161) {
    if (x < 104) {
      keyboardMode = KEYBOARD_NAME;
      if (nameBuffer.length() == 0) nameBuffer = deviceName;
      showKeyboardScreen();
    } else if (x < 212) {
      nameBuffer = "";
      showRenameScreen();
    } else {
      if (saveDeviceName(nameBuffer)) {
        statusText = "Name saved. Restarting...";
        showHomeScreen();
        delay(1500);
        ESP.restart();
      } else {
        drawStatus("Enter a valid name");
      }
    }
  }
}

void appendKeyboardCharacter(char character) {
  String& active = keyboardMode == KEYBOARD_NAME ? nameBuffer : textBuffer;
  uint16_t limit = keyboardMode == KEYBOARD_NAME ? 15 : 200;
  if (active.length() < limit) active += character;
  if (keyboardUppercase) keyboardUppercase = false;
}

void removeLastCharacter() {
  String& active = keyboardMode == KEYBOARD_NAME ? nameBuffer : textBuffer;
  if (active.length() > 0) active.remove(active.length() - 1);
}

void handleKeyboardTouch(int x, int y) {
  const char** rows = keyboardUppercase ? keyboardRowsUpper : keyboardRowsLower;
  if (y >= 78 && y <= 105) { int c=(x-5)/31; if(x>=5&&c>=0&&c<10){appendKeyboardCharacter(rows[0][c]);showKeyboardScreen();} return; }
  if (y >= 109 && y <= 136) { int c=(x-5)/31; if(x>=5&&c>=0&&c<10){appendKeyboardCharacter(rows[1][c]);showKeyboardScreen();} return; }
  if (y >= 140 && y <= 167) { int c=(x-20)/32; if(x>=20&&c>=0&&c<9){appendKeyboardCharacter(rows[2][c]);showKeyboardScreen();} return; }
  if (y >= 171 && y <= 198) { int c=(x-50)/32; if(x>=50&&c>=0&&c<7){appendKeyboardCharacter(rows[3][c]);showKeyboardScreen();} return; }
  if (y >= 203 && y <= 235) {
    if (x < 59) showHomeScreen();
    else if (x < 118) { keyboardUppercase=!keyboardUppercase; showKeyboardScreen(); }
    else if (x < 177) { appendKeyboardCharacter(' '); showKeyboardScreen(); }
    else if (x < 236) { removeLastCharacter(); showKeyboardScreen(); }
    else if (keyboardMode == KEYBOARD_NAME) showRenameScreen();
    else if (!bluetoothConnected()) { statusText="Bluetooth not connected"; showKeyboardScreen(); }
    else if (!textBuffer.length()) { statusText="Text is empty"; showKeyboardScreen(); }
    else { bleKeyboard->print(textBuffer); statusText="Text sent"; showHomeScreen(); }
  }
}

void handleQuickKeysTouch(int x, int y) {
  if (pointInside(x,y,homeButton)) { showHomeScreen(); return; }
  if (!bluetoothConnected()) { drawStatus("Bluetooth not connected"); return; }
  stopRequested = false;
  if (y>=55&&y<=93) { if(x<82)executeSingleCommand("WIN"); else if(x<161)executeSingleCommand("WIN r"); else if(x<240)executeSingleCommand("WIN d"); else executeSingleCommand("WIN l"); return; }
  if (y>=101&&y<=139) { if(x<82)executeSingleCommand("ENTER"); else if(x<161)executeSingleCommand("SPACE"); else if(x<240)executeSingleCommand("TAB"); else executeSingleCommand("ESC"); return; }
  if (y>=147&&y<=185) { if(x<82)executeSingleCommand("CTRL a"); else if(x<161)executeSingleCommand("CTRL c"); else if(x<240)executeSingleCommand("CTRL v"); else executeSingleCommand("ALT TAB"); }
}

void handleScriptsTouch(int x, int y) {
  if (pointInside(x,y,homeButton)) { showHomeScreen(); return; }
  if (pointInside(x,y,stopButton)) {
    stopRequested=true;
    scriptRunning=false;
    if(bluetoothConnected()) bleKeyboard->releaseAll();
    drawStatus("Stop requested");
    return;
  }
  if (!bluetoothConnected()) { drawStatus("Bluetooth not connected"); return; }
  if (y>=56&&y<=104) { if(x<108)executeScript(SCRIPT_TYPE_HELLO); else if(x<212)executeScript(SCRIPT_OPEN_NOTEPAD); else executeScript(SCRIPT_OPEN_CMD); return; }
  if (y>=113&&y<=161) { if(x<108)executeScript(SCRIPT_SHOW_DESKTOP); else if(x<212)executeScript(SCRIPT_OPEN_EXPLORER); else executeScript(SCRIPT_LOCK_COMPUTER); }
}

void pressKey(uint8_t key) {
  if (bluetoothConnected()) bleKeyboard->write(key);
}

void pressCombination(uint8_t modifier, uint8_t key) {
  if (!bluetoothConnected()) return;
  bleKeyboard->press(modifier);
  bleKeyboard->press(key);
  delay(100);
  bleKeyboard->releaseAll();
}

void pressTripleCombination(uint8_t m1, uint8_t m2, uint8_t key) {
  if (!bluetoothConnected()) return;
  bleKeyboard->press(m1);
  bleKeyboard->press(m2);
  bleKeyboard->press(key);
  delay(120);
  bleKeyboard->releaseAll();
}

void executeCommand(String command) {
  command.trim();
  int start = 0;
  while (start < command.length() && !stopRequested) {
    int sep = command.indexOf(';', start);
    String one = sep == -1 ? command.substring(start) : command.substring(start, sep);
    start = sep == -1 ? command.length() : sep + 1;
    executeSingleCommand(one);
    if (!stopRequested) waitWithTouch(50);
  }
}

void executeSingleCommand(String command) {
  command.trim();
  if (!command.length()) return;
  if (!bluetoothConnected()) { drawStatus("Bluetooth not connected"); return; }
  drawStatus("Running: " + command);
  if (command.startsWith("STRING ")) bleKeyboard->print(command.substring(7));
  else if (command.startsWith("DELAY ")) waitWithTouch(command.substring(6).toInt());
  else if (command == "ENTER") pressKey(KEY_RETURN);
  else if (command == "SPACE") pressKey(' ');
  else if (command == "TAB") pressKey(KEY_TAB);
  else if (command == "ESC") pressKey(KEY_ESC);
  else if (command == "WIN" || command == "META") { bleKeyboard->press(KEY_LEFT_GUI); delay(100); bleKeyboard->releaseAll(); }
  else if (command.startsWith("WIN ") || command.startsWith("META ")) { int p=command.indexOf(' '); if(p>=0&&p+1<command.length())pressCombination(KEY_LEFT_GUI,command.charAt(p+1)); }
  else if (command == "CTRL ALT DELETE") pressTripleCombination(KEY_LEFT_CTRL,KEY_LEFT_ALT,KEY_DELETE);
  else if (command == "ALT TAB") pressCombination(KEY_LEFT_ALT,KEY_TAB);
  else if (command.startsWith("CTRL ")) { String k=command.substring(5);k.trim();if(k.length())pressCombination(KEY_LEFT_CTRL,k.charAt(0)); }
  else if (command.startsWith("ALT ")) { String k=command.substring(4);k.trim();if(k.length())pressCombination(KEY_LEFT_ALT,k.charAt(0)); }
  else if (command.startsWith("SHIFT ")) { String k=command.substring(6);k.trim();if(k.length())pressCombination(KEY_LEFT_SHIFT,k.charAt(0)); }
  else drawStatus("Unknown: " + command);
}

void executeScript(const char* scriptText) {
  if (!bluetoothConnected()) { drawStatus("Bluetooth not connected"); return; }
  scriptRunning=true;
  stopRequested=false;
  String script(scriptText);
  int start=0;
  while(start<script.length()&&!stopRequested){
    int end=script.indexOf('\n',start);
    if(end==-1)end=script.length();
    String line=script.substring(start,end);
    line.trim();
    if(line.length()&&!line.startsWith("//")&&!line.startsWith("REM "))executeCommand(line);
    start=end+1;
    if(!stopRequested)waitWithTouch(50);
  }
  scriptRunning=false;
  bleKeyboard->releaseAll();
  drawStatus(stopRequested ? "Script stopped" : "Script completed");
}

void waitWithTouch(unsigned long milliseconds) {
  unsigned long started=millis();
  while(millis()-started<milliseconds&&!stopRequested){
    if(screenIsOff)handleScreenOffTouch();else handleTouch();
    delay(5);
  }
}

void unpairAndChangeAddress() {
  statusText="Resetting Bluetooth identity...";
  showHomeScreen();
  if(bluetoothConnected()){bleKeyboard->releaseAll();delay(200);}

  int count=esp_ble_get_bond_device_num();
  int removed=0;
  if(count>0){
    esp_ble_bond_dev_t* list=(esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t)*count);
    if(!list){statusText="Bond-list allocation failed";showHomeScreen();return;}
    int size=count;
    if(esp_ble_get_bond_device_list(&size,list)==ESP_OK){
      for(int i=0;i<size;i++){
        if(esp_ble_remove_bond_device(list[i].bd_addr)==ESP_OK)removed++;
        delay(100);
      }
    }
    free(list);
  }

  bool saved=saveNewBluetoothAddress();
  statusText=saved ? "Reset complete. Restarting..." : "Could not save new identity";
  showHomeScreen();
  Serial.printf("Removed %d bonds; new address saved: %s\n",removed,saved?"yes":"no");
  if(saved){delay(1800);ESP.restart();}
}

void redrawCurrentScreen() {
  switch(currentScreen){
    case SCREEN_HOME:showHomeScreen();break;
    case SCREEN_TEXT:showTextScreen();break;
    case SCREEN_KEYBOARD:showKeyboardScreen();break;
    case SCREEN_QUICK_KEYS:showQuickKeysScreen();break;
    case SCREEN_SCRIPTS:showScriptsScreen();break;
    case SCREEN_RENAME:showRenameScreen();break;
  }
}
