#include <WiFi.h>
#include <Secrets.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ESP32-C3 SuperMini Configuration
#define LED_PIN 8
#define SERVO_PIN 9

// Serial buffer
#define BUFFER_SIZE 2048
#define CONSOLE_BUFFER_SIZE 100

// UART Configuration
#define PRINTER_RX_PIN 20
#define PRINTER_TX_PIN 21
HardwareSerial PrinterSerial(1);

// Servo configuration
Servo clawServo;
int SERVO_OPEN_ANGLE = 0;
int SERVO_CLOSE_ANGLE = 60;

// WiFi credentials
const char* ssid = MYSSIDIOT;
const char* password = MYPSKIOT;
const char* hostname = "crealityclaw";

// Position limits
const float MIN_X = -10.0;
const float MIN_Y = -10.0;
const float MIN_Z = 0.0;
const float MAX_X = 233.0;
const float MAX_Y = 232.0;
const float MAX_Z = 237.0;

// Wall avoidance for drop zone (lower-left corner)
const float WALL_X_THRESHOLD = 90.0;   // Wall at X=82 (corner)
const float WALL_Y_THRESHOLD = 95.0;   // Wall at Y=90 (corner)
const float WALL_Z_TRIGGER = 153.0;    // Wall only matters above Z=153

// Individual axis limits when other axis is safe
const float WALL_Y_MIN_SAFE = 15.0;    // Y minimum when X is safe
const float WALL_X_MIN_SAFE = 15.0;     // X minimum when Y is safe (what value?)

bool commandInProgress = false;

// DEBUG LOGGING CONTROL
bool DEBUG_LOGGING = false;

// Twitch Integration
WiFiClient twitchClient;
bool twitchEnabled = false;
String twitchChannel = "";
String twitchOauth = "";
String twitchNick = "";
unsigned long lastTwitchReconnect = 0;

// Twitch Command Structure
#define MAX_TWITCH_COMMANDS 20
struct TwitchCommand {
  String trigger;    // Main trigger word (e.g., "!claw")
  String args[5];    // 5 flexible argument patterns (e.g., "x{X}", "y{Y}")
  String actions;    // Actions to execute
  bool enabled;
};

TwitchCommand twitchCommands[MAX_TWITCH_COMMANDS];
int twitchCommandCount = 0;

// State variables
struct Position {
  float X = 0.0;
  float Y = 0.0;
  float Z = 0.0;
  float E = 0.0;
};

struct MoveResult {
  float distance;
  String warning;
  bool blocked;
};

struct ConsoleMessage {
  String time;
  String type;
  String text;
};

Position currentPos;
bool positionUpdatePending = false;
bool lightState = false;
unsigned long ledBlinkUntil = 0;
unsigned long lastWiFiCheck = 0;

ConsoleMessage consoleBuffer[CONSOLE_BUFFER_SIZE];
int consoleIndex = 0;
int consoleCount = 0;

WebServer server(80);
Preferences prefs;

// Forward declarations
void readPrinterBus();
void sendGcodeRaw(String command);
void processTwitchMessage(String message, String username);
void connectToTwitch();
void loadTwitchSettings();
void saveTwitchSettings();

// ============ LOGGING FUNCTIONS ============
String getLogTimestamp() {
  unsigned long now = millis();
  unsigned long seconds = now / 1000;
  unsigned long ms = now % 1000;
  int hours = (seconds / 3600) % 24;
  int minutes = (seconds / 60) % 60;
  int secs = seconds % 60;
  
  char buf[16];
  sprintf(buf, "[%02d:%02d:%02d.%03lu]", hours, minutes, secs, ms);
  return String(buf);
}

void logDebug(String category, String message) {
  if (DEBUG_LOGGING) {
    Serial.print(getLogTimestamp());
    Serial.print(" [");
    Serial.print(category);
    Serial.print("] ");
    Serial.println(message);
  }
}

void logInfo(String message) {
  if (DEBUG_LOGGING) {
    Serial.print(getLogTimestamp());
    Serial.print(" [INFO] ");
    Serial.println(message);
  }
}

void logWarning(String message) {
  if (DEBUG_LOGGING) {
    Serial.print(getLogTimestamp());
    Serial.print(" [WARN] ");
    Serial.println(message);
  }
}

void logError(String message) {
  if (DEBUG_LOGGING) {
    Serial.print(getLogTimestamp());
    Serial.print(" [ERROR] ");
    Serial.println(message);
  }
}

void logPosition() {
  if (DEBUG_LOGGING) {
    Serial.print(getLogTimestamp());
    Serial.print(" [POS] X:");
    Serial.print(currentPos.X, 2);
    Serial.print(" Y:");
    Serial.print(currentPos.Y, 2);
    Serial.print(" Z:");
    Serial.print(currentPos.Z, 2);
    Serial.print(" E:");
    Serial.println(currentPos.E, 2);
  }
}

// ============ LED FUNCTIONS ============
void ledOn() { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void ledBlinkFast() {
  ledBlinkUntil = millis() + 50;
  ledOn();
}

void handleLEDBlink() {
  if (ledBlinkUntil > 0 && millis() >= ledBlinkUntil) {
    ledOff();
    ledBlinkUntil = 0;
  }
}

void ledSuccess() {
  ledBlinkUntil = millis() + 500;
  ledOn();
  logDebug("LED", "Success pattern");
}

void ledError(int times) {
  ledBlinkUntil = millis() + 1000;
  ledOn();
  logDebug("LED", "Error pattern x" + String(times));
}

void heartbeatLED() {
  static unsigned long last = 0;
  static bool ledState = false;
  
  if (millis() - last > 3000) {
    if (ledBlinkUntil == 0) {
      if (!ledState) {
        ledOn();
        ledState = true;
        last = millis();
      }
    }
  } else if (millis() - last > 75 && ledState) {
    ledOff();
    ledState = false;
  }
}

// ============ WiFi RECONNECTION ============
void checkWiFi() {
  if (millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      logWarning("WiFi disconnected! Attempting reconnection...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts++ < 20) {
        delay(500);
        yield();
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        logInfo("WiFi reconnected! IP: " + WiFi.localIP().toString());
      } else {
        logError("WiFi reconnection failed!");
      }
    } else {
      logDebug("WiFi", "Connection OK, RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
  }
}

// ============ CONSOLE FUNCTIONS ============
String getTimeString() {
  unsigned long now = millis() / 1000;
  int hours = (now / 3600) % 24;
  int minutes = (now / 60) % 60;
  int seconds = now % 60;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hours, minutes, seconds);
  return String(buf);
}

String getUptimeString() {
  unsigned long totalSeconds = millis() / 1000;
  int days = totalSeconds / 86400;
  int hours = (totalSeconds % 86400) / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  
  String uptime = "";
  if (days > 0) uptime += String(days) + "d ";
  uptime += String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
  return uptime;
}

void addToConsole(String message, String msgType = "tx") {
  consoleBuffer[consoleIndex].time = getTimeString();
  consoleBuffer[consoleIndex].type = msgType;
  consoleBuffer[consoleIndex].text = message;
  
  consoleIndex = (consoleIndex + 1) % CONSOLE_BUFFER_SIZE;
  if (consoleCount < CONSOLE_BUFFER_SIZE) {
    consoleCount++;
  }
}

// ============ POSITION PARSING ============
bool parsePosition(String response) {
  int xPos = response.indexOf("X:");
  int yPos = response.indexOf("Y:");
  int zPos = response.indexOf("Z:");
  int ePos = response.indexOf("E:");
  
  if (xPos >= 0 && yPos >= 0 && zPos >= 0 && ePos >= 0) {
    currentPos.X = response.substring(xPos + 2, yPos).toFloat();
    currentPos.Y = response.substring(yPos + 2, zPos).toFloat();
    currentPos.Z = response.substring(zPos + 2, ePos).toFloat();
    currentPos.E = response.substring(ePos + 2).toFloat();
    positionUpdatePending = false;
    logPosition();
    return true;
  }
  return false;
}

void updatePositionInternal() {
  logDebug("GCODE", "Requesting position (M114)");
  positionUpdatePending = true;
  PrinterSerial.print("M114\r\n");
  PrinterSerial.flush();
}

// ============ WALL AVOIDANCE ============
bool isInWallDangerZone(float x, float y, float z) {
  if (z <= WALL_Z_TRIGGER) return false;
  
  // Corner danger (BOTH axes too low)
  if (x < WALL_X_THRESHOLD && y < WALL_Y_THRESHOLD) {
    return true;
  }
  
  // Individual axis limits when other is safe
  if (x >= WALL_X_THRESHOLD && y < WALL_Y_MIN_SAFE) {
    return true; // X is safe but Y is too low
  }
  
  if (y >= WALL_Y_THRESHOLD && x < WALL_X_MIN_SAFE) {
    return true; // Y is safe but X is too low
  }
  
  return false;
}

void loadTwitchSettings() {
  prefs.begin("twitch", true);
  
  twitchEnabled = prefs.getBool("enabled", false);
  twitchChannel = prefs.getString("channel", "");
  twitchOauth = prefs.getString("oauth", "");
  twitchNick = prefs.getString("nick", "clawbot");
  
  twitchCommandCount = prefs.getInt("cmdCount", 0);
  for (int i = 0; i < twitchCommandCount && i < MAX_TWITCH_COMMANDS; i++) {
    String key = "cmd" + String(i);
    String data = prefs.getString(key.c_str(), "");
    
    // Format: trigger|arg0|arg1|arg2|arg3|arg4|actions|enabled
    if (data.length() > 0) {
      int idx = 0;
      int nextSep = data.indexOf('|');
      if (nextSep > 0) {
        twitchCommands[i].trigger = data.substring(idx, nextSep);
        
        // Load 5 args
        for(int k=0; k<5; k++) {
          idx = nextSep + 1;
          nextSep = data.indexOf('|', idx);
          if (nextSep > idx) {
            twitchCommands[i].args[k] = data.substring(idx, nextSep);
          } else {
            twitchCommands[i].args[k] = ""; // Handle empty or malformed
          }
        }
        
        // Load actions
        idx = nextSep + 1;
        nextSep = data.indexOf('|', idx);
        if (nextSep > idx) {
          twitchCommands[i].actions = data.substring(idx, nextSep);
          twitchCommands[i].enabled = data.substring(nextSep + 1) == "1";
        }
      }
    }
  }
  
  prefs.end();
  logInfo("Loaded " + String(twitchCommandCount) + " Twitch commands");
}

void saveTwitchSettings() {
  prefs.begin("twitch", false);
  
  prefs.putBool("enabled", twitchEnabled);
  prefs.putString("channel", twitchChannel);
  prefs.putString("oauth", twitchOauth);
  prefs.putString("nick", twitchNick);
  prefs.putInt("cmdCount", twitchCommandCount);
  
  for (int i = 0; i < twitchCommandCount && i < MAX_TWITCH_COMMANDS; i++) {
    String key = "cmd" + String(i);
    String data = twitchCommands[i].trigger + "|";
    
    // Save 5 args
    for(int k=0; k<5; k++) {
      data += twitchCommands[i].args[k] + "|";
    }
    
    data += twitchCommands[i].actions + "|" + 
            (twitchCommands[i].enabled ? "1" : "0");
            
    prefs.putString(key.c_str(), data);
  }
  
  prefs.end();
  logInfo("Saved Twitch settings");
}

void moveToSafePosition() {
  logWarning("Wall avoidance triggered!");
  logDebug("WALL", "Current pos: X=" + String(currentPos.X, 2) + 
           " Y=" + String(currentPos.Y, 2) + " Z=" + String(currentPos.Z, 2));
  
  // Case 1: Both in corner danger - escape via nearest edge
  if (currentPos.X < WALL_X_THRESHOLD && currentPos.Y < WALL_Y_THRESHOLD) {
    float adjustX = WALL_X_THRESHOLD - currentPos.X;
    float adjustY = WALL_Y_THRESHOLD - currentPos.Y;
    
    if (adjustX <= adjustY) {
      // Escape via X
      logInfo("Moving X to " + String(WALL_X_THRESHOLD, 2));
      addToConsole("WALL AVOID: Moving X to " + String(WALL_X_THRESHOLD, 2), "blocked");
      String cmd = "G1 X" + String(WALL_X_THRESHOLD, 2) + " F7000";
      sendGcodeRaw(cmd);
      currentPos.X = WALL_X_THRESHOLD;
      
      // Now X is safe, check if Y meets secondary threshold
      if (currentPos.Y < WALL_Y_MIN_SAFE) {
        logInfo("After X move, Y still too low. Moving Y to " + String(WALL_Y_MIN_SAFE, 2));
        addToConsole("WALL AVOID: Moving Y to " + String(WALL_Y_MIN_SAFE, 2), "blocked");
        String cmdY = "G1 Y" + String(WALL_Y_MIN_SAFE, 2) + " F7000";
        sendGcodeRaw(cmdY);
        currentPos.Y = WALL_Y_MIN_SAFE;
      }
    } else {
      // Escape via Y
      logInfo("Moving Y to " + String(WALL_Y_THRESHOLD, 2));
      addToConsole("WALL AVOID: Moving Y to " + String(WALL_Y_THRESHOLD, 2), "blocked");
      String cmd = "G1 Y" + String(WALL_Y_THRESHOLD, 2) + " F7000";
      sendGcodeRaw(cmd);
      currentPos.Y = WALL_Y_THRESHOLD;
      
      // Now Y is safe, check if X meets secondary threshold
      if (currentPos.X < WALL_X_MIN_SAFE) {
        logInfo("After Y move, X still too low. Moving X to " + String(WALL_X_MIN_SAFE, 2));
        addToConsole("WALL AVOID: Moving X to " + String(WALL_X_MIN_SAFE, 2), "blocked");
        String cmdX = "G1 X" + String(WALL_X_MIN_SAFE, 2) + " F7000";
        sendGcodeRaw(cmdX);
        currentPos.X = WALL_X_MIN_SAFE;
      }
    }
  }
  // Case 2: X is safe, Y too low
  else if (currentPos.X >= WALL_X_THRESHOLD && currentPos.Y < WALL_Y_MIN_SAFE) {
    logInfo("Moving Y to " + String(WALL_Y_MIN_SAFE, 2));
    addToConsole("WALL AVOID: Moving Y to " + String(WALL_Y_MIN_SAFE, 2), "blocked");
    String cmd = "G1 Y" + String(WALL_Y_MIN_SAFE, 2) + " F7000";
    sendGcodeRaw(cmd);
    currentPos.Y = WALL_Y_MIN_SAFE;
  }
  // Case 3: Y is safe, X too low
  else if (currentPos.Y >= WALL_Y_THRESHOLD && currentPos.X < WALL_X_MIN_SAFE) {
    logInfo("Moving X to " + String(WALL_X_MIN_SAFE, 2));
    addToConsole("WALL AVOID: Moving X to " + String(WALL_X_MIN_SAFE, 2), "blocked");
    String cmd = "G1 X" + String(WALL_X_MIN_SAFE, 2) + " F7000";
    sendGcodeRaw(cmd);
    currentPos.X = WALL_X_MIN_SAFE;
  }
  
  delay(100);
  updatePositionInternal();
  unsigned long start = millis();
  while (positionUpdatePending && (millis() - start) < 1000) {
    readPrinterBus();
    yield();
    delay(10);
  }
  
  logInfo("Wall cleared. New position: X=" + String(currentPos.X, 2) + 
          " Y=" + String(currentPos.Y, 2));
}

void adjustTargetForWall(float &targetX, float &targetY, float &targetZ) {
  if (!isInWallDangerZone(targetX, targetY, targetZ)) {
    return;
  }
  
  logWarning("Target in danger zone - adjusting to nearest safe position");
  
  // Case 1: Both in corner danger
  if (targetX < WALL_X_THRESHOLD && targetY < WALL_Y_THRESHOLD) {
    float adjustX = WALL_X_THRESHOLD - targetX;
    float adjustY = WALL_Y_THRESHOLD - targetY;
    
    if (adjustX <= adjustY) {
      float originalX = targetX;
      targetX = WALL_X_THRESHOLD;
      logInfo("Corner escape via X: " + String(originalX, 2) + " → " + String(targetX, 2));
      addToConsole("WALL: Adjusted X to " + String(WALL_X_THRESHOLD, 2), "blocked");
    } else {
      float originalY = targetY;
      targetY = WALL_Y_THRESHOLD;
      logInfo("Corner escape via Y: " + String(originalY, 2) + " → " + String(targetY, 2));
      addToConsole("WALL: Adjusted Y to " + String(WALL_Y_THRESHOLD, 2), "blocked");
    }
  }
  // Case 2: X is safe, but Y too low
  else if (targetX >= WALL_X_THRESHOLD && targetY < WALL_Y_MIN_SAFE) {
    float originalY = targetY;
    targetY = WALL_Y_MIN_SAFE;
    logInfo("Y adjusted (X safe): " + String(originalY, 2) + " → " + String(targetY, 2));
    addToConsole("WALL: Adjusted Y to " + String(WALL_Y_MIN_SAFE, 2), "blocked");
  }
  // Case 3: Y is safe, but X too low
  else if (targetY >= WALL_Y_THRESHOLD && targetX < WALL_X_MIN_SAFE) {
    float originalX = targetX;
    targetX = WALL_X_MIN_SAFE;
    logInfo("X adjusted (Y safe): " + String(originalX, 2) + " → " + String(targetX, 2));
    addToConsole("WALL: Adjusted X to " + String(WALL_X_MIN_SAFE, 2), "blocked");
  }
}

// ============ G-CODE PARSING ============
float parseAxisValue(String cmd, char axis) {
  int idx = cmd.indexOf(axis);
  if (idx < 0) return 0.0;
  
  if (idx > 0) {
    char before = cmd.charAt(idx - 1);
    if (before != ' ' && before != '\t') return 0.0;
  }
  
  String valueStr = "";
  idx++;
  while (idx < cmd.length()) {
    char c = cmd.charAt(idx);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
    if (isAlpha(c)) break;
    valueStr += c;
    idx++;
  }
  
  return valueStr.toFloat();
}

bool hasAxis(String cmd, char axis) {
  int idx = cmd.indexOf(axis);
  while (idx >= 0) {
    if (idx == 0 || cmd.charAt(idx - 1) == ' ' || cmd.charAt(idx - 1) == '\t') {
      if (idx + 1 < cmd.length()) {
        char next = cmd.charAt(idx + 1);
        if (isDigit(next) || next == '-' || next == '+' || next == '.') {
          return true;
        }
      }
    }
    idx = cmd.indexOf(axis, idx + 1);
  }
  return false;
}

// ============ LIMIT CHECKING (ABSOLUTE) ============
MoveResult getClampedMove(char axis, float targetPosition) {
  MoveResult result;
  result.distance = 0.0;  // Not used in absolute mode
  result.warning = "";
  result.blocked = false;
  
  float current = 0.0;
  float minLimit = 0.0;
  float maxLimit = 0.0;
  
  switch (axis) {
    case 'X':
      current = currentPos.X;
      minLimit = MIN_X;
      maxLimit = MAX_X;
      break;
    case 'Y':
      current = currentPos.Y;
      minLimit = MIN_Y;
      maxLimit = MAX_Y;
      break;
    case 'Z':
      current = currentPos.Z;
      minLimit = MIN_Z;
      maxLimit = MAX_Z;
      break;
    default:
      result.distance = targetPosition;
      return result;
  }
  
  logDebug("LIMIT", String(axis) + " target: " + String(targetPosition, 2) + 
           " (current: " + String(current, 2) + ", limits: " + String(minLimit, 2) + 
           " to " + String(maxLimit, 2) + ")");
  
  if (targetPosition > maxLimit) {
    if (abs(current - maxLimit) < 0.1) {
      result.distance = maxLimit;
      result.warning = "Already at Max Limit (" + String(maxLimit) + ")";
      result.blocked = true;
      logWarning("BLOCKED: " + String(axis) + " already at max limit " + String(maxLimit));
    } else {
      result.distance = maxLimit;
      result.warning = "Clamped to Max " + String(maxLimit);
      result.blocked = false;
      logWarning("CLAMPED: " + String(axis) + " from " + String(targetPosition, 2) + 
                " to " + String(maxLimit, 2));
    }
    return result;
  }
  
  if (targetPosition < minLimit) {
    if (abs(current - minLimit) < 0.1) {
      result.distance = minLimit;
      result.warning = "Already at Min Limit (" + String(minLimit) + ")";
      result.blocked = true;
      logWarning("BLOCKED: " + String(axis) + " already at min limit " + String(minLimit));
    } else {
      result.distance = minLimit;
      result.warning = "Clamped to Min " + String(minLimit);
      result.blocked = false;
      logWarning("CLAMPED: " + String(axis) + " from " + String(targetPosition, 2) + 
                " to " + String(minLimit, 2));
    }
    return result;
  }
  
  // Within limits
  result.distance = targetPosition;
  return result;
}

// ============ G-CODE SENDING ============
void sendGcodeRaw(String command) {
  if (!command.endsWith("\n")) {
    command += "\r\n";
  }

  // Add to web console (strip the \r\n for display)
  String displayCmd = command;
  displayCmd.trim();
  addToConsole(displayCmd, "tx");

  logDebug("TX->PRINTER", command.substring(0, command.length() - 2));
  PrinterSerial.print(command);
  PrinterSerial.flush();
  ledBlinkFast();
}

void sendGcode(String command, bool echo = true) {
  String cmd = command;
  cmd.trim();
  String cmdUpper = cmd;
  cmdUpper.toUpperCase();
  
  logDebug("GCODE", "Processing: " + cmd);
  
  if (echo) {
    addToConsole(cmd, "tx");
  }
  
  bool isG0 = cmdUpper.startsWith("G0 ") || cmdUpper.startsWith("G0") || cmdUpper == "G0";
  bool isG1 = cmdUpper.startsWith("G1 ") || cmdUpper.startsWith("G1") || cmdUpper == "G1";
  
  if (isG0 || isG1) {
    logDebug("MOVE", "Detected movement command, validating...");
    
    updatePositionInternal();
    unsigned long start = millis();
    while (positionUpdatePending && (millis() - start) < 500) {
      readPrinterBus();
      yield();
      delay(10);
    }
    
    if (positionUpdatePending) {
      logWarning("Position update timed out!");
    }
    
    // Get target positions (default to current if axis not specified)
    float targetX = currentPos.X;
    float targetY = currentPos.Y;
    float targetZ = currentPos.Z;

    if (hasAxis(cmdUpper, 'X')) targetX = parseAxisValue(cmdUpper, 'X');
    if (hasAxis(cmdUpper, 'Y')) targetY = parseAxisValue(cmdUpper, 'Y');
    if (hasAxis(cmdUpper, 'Z')) targetZ = parseAxisValue(cmdUpper, 'Z');

    // ENHANCED WALL AVOIDANCE
    // Case 1: Currently Z is high, trying to move XY into danger
    if (currentPos.Z >= WALL_Z_TRIGGER && (hasAxis(cmdUpper, 'X') || hasAxis(cmdUpper, 'Y'))) {
      if (isInWallDangerZone(targetX, targetY, currentPos.Z)) {
        logWarning("Cannot move XY into danger zone while Z is high - lowering Z first");
        addToConsole("WALL AVOID: Lowering Z to " + String(WALL_Z_TRIGGER - 5, 1), "blocked");
        String lowerCmd = "G1 Z" + String(WALL_Z_TRIGGER - 5.0, 2) + " F5000";
        sendGcodeRaw(lowerCmd);
        currentPos.Z = WALL_Z_TRIGGER - 5.0;
        
        // Update targetZ so subsequent checks know we are safe
        if (!hasAxis(cmdUpper, 'Z')) {
           targetZ = currentPos.Z;
        }

        delay(100);
        updatePositionInternal();
        unsigned long waitstart = millis();
        while (positionUpdatePending && (millis() - waitstart) < 1000) {
          readPrinterBus();
          yield();
          delay(10);
        }
        logInfo("Z lowered. Now safe to move XY");
      }
    }

    // Case 2: Trying to raise Z while in danger zone
    if (targetZ >= WALL_Z_TRIGGER && targetZ > currentPos.Z) {
      if (isInWallDangerZone(currentPos.X, currentPos.Y, targetZ)) {
        logWarning("Cannot raise Z while in danger zone - moving XY to safety first");
        moveToSafePosition();
        updatePositionInternal();
        start = millis();
        while (positionUpdatePending && (millis() - start) < 500) {
          readPrinterBus();
          yield();
          delay(10);
        }
      }
    }

    // Case 3: Adjust target coordinates if they would end up in danger zone
    adjustTargetForWall(targetX, targetY, targetZ);
    // END WALL AVOIDANCE

    bool hasMovement = false;
    int blockedAxes = 0;
    int totalAxes = 0;
    String modifiedCmd = "";
    
    if (isG0) {
      modifiedCmd = "G0";
    } else {
      modifiedCmd = "G1";
    }
    
    // Process X axis
    if (hasAxis(cmdUpper, 'X')) {
      totalAxes++;
      MoveResult result = getClampedMove('X', targetX);
      
      if (result.blocked) {
        addToConsole("BLOCKED: X " + result.warning, "blocked");
        blockedAxes++;
        logWarning("Skipping blocked axis X");
      } else {
        if (result.warning.length() > 0) {
          addToConsole("LIMIT: X " + result.warning, "blocked");
        }
        modifiedCmd += " X" + String(result.distance, 2);
        hasMovement = true;
        targetX = result.distance; // Update for position tracking
      }
    }
    
    // Process Y axis
    if (hasAxis(cmdUpper, 'Y')) {
      totalAxes++;
      MoveResult result = getClampedMove('Y', targetY);
      
      if (result.blocked) {
        addToConsole("BLOCKED: Y " + result.warning, "blocked");
        blockedAxes++;
        logWarning("Skipping blocked axis Y");
      } else {
        if (result.warning.length() > 0) {
          addToConsole("LIMIT: Y " + result.warning, "blocked");
        }
        modifiedCmd += " Y" + String(result.distance, 2);
        hasMovement = true;
        targetY = result.distance;
      }
    }
    
    // Process Z axis
    if (hasAxis(cmdUpper, 'Z')) {
      totalAxes++;
      MoveResult result = getClampedMove('Z', targetZ);
      
      if (result.blocked) {
        addToConsole("BLOCKED: Z " + result.warning, "blocked");
        blockedAxes++;
        logWarning("Skipping blocked axis Z");
      } else {
        if (result.warning.length() > 0) {
          addToConsole("LIMIT: Z " + result.warning, "blocked");
        }
        modifiedCmd += " Z" + String(result.distance, 2);
        hasMovement = true;
        targetZ = result.distance;
      }
    }
    
    // Add E axis (no limits)
    if (hasAxis(cmdUpper, 'E')) {
      float eValue = parseAxisValue(cmdUpper, 'E');
      modifiedCmd += " E" + String(eValue, 2);
      hasMovement = true;
    }
    
    // Add feedrate if present
    if (hasAxis(cmdUpper, 'F')) {
      float feedrate = parseAxisValue(cmdUpper, 'F');
      modifiedCmd += " F" + String((int)feedrate);
    }
    
    // Only block if ALL axes are blocked
    if (totalAxes > 0 && blockedAxes == totalAxes) {
      logError("All axes blocked - no movement possible");
      return;
    }
    
    if (hasMovement) {
      if (blockedAxes > 0) {
        logInfo("Partial move: " + String(blockedAxes) + "/" + String(totalAxes) + " axes blocked");
      }
      logInfo("Sending validated move: " + modifiedCmd);
      sendGcodeRaw(modifiedCmd);
      
      // Update our known position immediately (optimistic)
      if (hasAxis(cmdUpper, 'X')) currentPos.X = targetX;
      if (hasAxis(cmdUpper, 'Y')) currentPos.Y = targetY;
      if (hasAxis(cmdUpper, 'Z')) currentPos.Z = targetZ;
      
      logPosition();
      return;
    }
  }
  
  sendGcodeRaw(cmd);
}

// ============ SERVO CONTROL ============
void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  logDebug("SERVO", "Setting angle to " + String(angle) + "°");
  clawServo.write(angle);
}

void controlClaw(String action, int angle = -1) {
  if (angle >= 0) {
    angle = constrain(angle, 0, 180);
    addToConsole("CLAW -> " + String(angle) + "°", "tx");
    setServoAngle(angle);
  } else if (action == "open") {
    addToConsole("CLAW OPEN (" + String(SERVO_OPEN_ANGLE) + "°)", "tx");
    logInfo("Opening claw");
    setServoAngle(SERVO_OPEN_ANGLE);
  } else if (action == "close") {
    addToConsole("CLAW CLOSE (" + String(SERVO_CLOSE_ANGLE) + "°)", "tx");
    logInfo("Closing claw");
    setServoAngle(SERVO_CLOSE_ANGLE);
  }
}

// ============ SERIAL READER ============
void readPrinterBus() {
  static String partialLine = "";
  int readCount = 0;
  
  while (PrinterSerial.available() && readCount++ < 100) {
    char c = PrinterSerial.read();
    
    if (c == '\n') {
      String line = partialLine;
      line.trim();
      partialLine = "";
      
      if (line.length() == 0) continue;
      
      logDebug("RX<-PRINTER", line);
      
      if (line.startsWith("X:") && line.indexOf("Y:") >= 0) {
        if (parsePosition(line)) {
          addToConsole(line, "rx");
          continue;
        }
      }
      
      if (line == "ok") {
        continue;
      }
      
      addToConsole(line, "rx");
      ledBlinkFast();
    } else if (c != '\r') {
      partialLine += c;
      if (partialLine.length() > 256) {
        logWarning("Serial buffer overflow, clearing");
        partialLine = "";
      }
    }
  }
}

// ============ TWITCH INTEGRATION ============

// Returns true if 's' is a valid numeric string (optional leading sign, digits, optional single dot).
// Allows an optional leading axis letter (x/y/X/Y) followed by optional whitespace before the number.
// e.g. "50", "50.5", "-3.2", "x50", "x 50", "X 50.0" all pass.
// Returns false and leaves outVal unchanged on any invalid input.
bool parseClawCoord(String s, float &outVal) {
  s.trim();
  if (s.length() == 0) return false;

  // Strip optional leading axis letter (x or y, case-insensitive)
  int start = 0;
  char first = s.charAt(0);
  if (first == 'x' || first == 'X' || first == 'y' || first == 'Y') {
    start = 1;
    // Allow optional whitespace between the axis letter and the number
    while (start < (int)s.length() && s.charAt(start) == ' ') start++;
  }

  if (start >= (int)s.length()) return false;

  // Validate remaining characters: optional leading sign, then digits with at most one dot
  int i = start;
  if (s.charAt(i) == '-' || s.charAt(i) == '+') i++;
  if (i >= (int)s.length()) return false; // sign only is not valid

  bool hasDot = false;
  bool hasDigit = false;
  while (i < (int)s.length()) {
    char c = s.charAt(i);
    if (c == '.') {
      if (hasDot) return false; // two dots → invalid
      hasDot = true;
    } else if (isDigit(c)) {
      hasDigit = true;
    } else {
      return false; // any non-numeric character → invalid
    }
    i++;
  }

  if (!hasDigit) return false;

  outVal = s.substring(start).toFloat();
  return true;
}

// Helper to extract a variable from a specific pattern found anywhere in the message.
// For coordinate-type vars (name length == 1, single uppercase letter X/Y/Z),
// the captured value is additionally validated as a real number.
bool extractArg(String message, String pattern, String& varName, String& varValue) {
  if (pattern.length() == 0) return false;
  
  int openBrace = pattern.indexOf('{');
  int closeBrace = pattern.indexOf('}', openBrace);
  
  // If no variable placeholder, just check if the text exists
  if (openBrace < 0 || closeBrace < 0) {
    return message.indexOf(pattern) >= 0;
  }
  
  String prefix = pattern.substring(0, openBrace);
  String name = pattern.substring(openBrace + 1, closeBrace);
  
  // Find the prefix in the message
  int startIdx = message.indexOf(prefix);
  if (startIdx < 0) return false;
  
  // Value starts after the prefix
  int valStart = startIdx + prefix.length();
  
  // Stop at space OR comma
  int spaceIdx = message.indexOf(' ', valStart);
  int commaIdx = message.indexOf(',', valStart);
  
  int valEnd = message.length();
  if (spaceIdx >= 0 && spaceIdx < valEnd) valEnd = spaceIdx;
  if (commaIdx >= 0 && commaIdx < valEnd) valEnd = commaIdx;
  
  String val = message.substring(valStart, valEnd);
  val.trim();
  
  if (val.length() == 0) return false;

  // For single-letter axis variable names (X, Y, Z), validate the value is numeric.
  // This prevents garbage like "abc" from silently becoming 0.0 via toFloat().
  if (name.length() == 1) {
    char n = name.charAt(0);
    if (n == 'X' || n == 'Y' || n == 'Z') {
      float dummy;
      if (!parseClawCoord(val, dummy)) {
        logWarning("extractArg: non-numeric value '" + val + "' for axis var " + name);
        return false;
      }
    }
  }
  
  varName = name;
  varValue = val;
  return true;
}

void connectToTwitch() {
  if (!twitchEnabled || twitchChannel.length() == 0 || twitchOauth.length() == 0) {
    logInfo("Twitch not configured or disabled");
    return;
  }
  
  logInfo("Connecting to Twitch IRC...");
  
  if (twitchClient.connect("irc.chat.twitch.tv", 6667)) {
    logInfo("✓ Connected to Twitch server");
    
    // Request capabilities
    twitchClient.println("CAP REQ :twitch.tv/tags twitch.tv/commands");
    delay(100);
    
    // Authenticate
    logDebug("TWITCH-AUTH", "Sending PASS (oauth hidden)");
    twitchClient.println("PASS " + twitchOauth);
    delay(100);
    
    logDebug("TWITCH-AUTH", "Sending NICK: " + twitchNick);
    twitchClient.println("NICK " + twitchNick);
    delay(100);
    
    String channelWithHash = twitchChannel;
    if (!channelWithHash.startsWith("#")) {
      channelWithHash = "#" + channelWithHash;
    }
    logDebug("TWITCH-AUTH", "Joining channel: " + channelWithHash);
    twitchClient.println("JOIN " + channelWithHash);

    logInfo("✓ Sent JOIN command for: " + twitchChannel);
    
    // Wait for response and log it
    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {  // Wait 3 seconds for responses
      while (twitchClient.available()) {
        String response = twitchClient.readStringUntil('\n');
        response.trim();
        if (response.length() > 0) {
          logInfo("TWITCH RESPONSE: " + response);
        }
      }
      delay(50);
    }
  } else {
    logError("✗ Failed to connect to Twitch IRC server");
  }
}


void sendTwitchMessage(String message) {
  if (!twitchClient.connected()) {
    logWarning("Cannot send message - not connected to Twitch");
    return;
  }
  
  String channelWithHash = twitchChannel;
  if (!channelWithHash.startsWith("#")) {
    channelWithHash = "#" + channelWithHash;
  }
  
  String fullMsg = "PRIVMSG " + channelWithHash + " :" + message;
  twitchClient.println(fullMsg);
  twitchClient.flush();
  
  logInfo("TWITCH SENT: " + message);
  addToConsole("BOT: " + message, "tx");
}

// ============ ENHANCED TWITCH MESSAGE PROCESSING ============

void flushTwitchBacklog(uint32_t quietMs = 250, uint32_t maxMs = 2000) {
  uint32_t start = millis();
  uint32_t lastRx = millis();

  while ((millis() - lastRx) < quietMs && (millis() - start) < maxMs) {
    if (twitchClient.available()) {
      handleTwitch();          // drains lines; PING->PONG; PRIVMSG dropped by commandInProgress gate
      lastRx = millis();
    } else {
      delay(5);
      yield();
    }
  }
}

void processTwitchMessage(String message, String username) {
  logDebug("TWITCH", "Message: " + message + " from " + username);
  if (commandInProgress) return;

  String lowerMsg = message;
  lowerMsg.toLowerCase();

  for (int i = 0; i < twitchCommandCount; i++) {
    if (!twitchCommands[i].enabled) continue;

    String trigger = twitchCommands[i].trigger;
    trigger.toLowerCase();

    // Check if message STARTS with the trigger word
    if (lowerMsg.startsWith(trigger)) {
      logInfo("✓ Trigger matched: " + twitchCommands[i].trigger);
      
      String vars[10];
      String varNames[10];
      int varCount = 0;
      
      // Add USER variable
      varNames[varCount] = "USER";
      vars[varCount] = username;
      varCount++;

      // Scan for all 5 arguments defined in this command
      bool argsFailed = false;
      for(int k=0; k<5; k++) {
        String pat = twitchCommands[i].args[k];
        if(pat.length() > 0) {
           String vName, vVal;
           if (extractArg(lowerMsg, pat, vName, vVal)) {
             if (varCount < 10) {
               varNames[varCount] = vName;
               vars[varCount] = vVal;
               varCount++;
             }
           } else {
             // A non-empty pattern that failed to extract a valid value means
             // the user's input was malformed — flag it.
             // Only treat as hard failure for axis coordinate patterns
             // (single uppercase letter inside braces).
             int ob = pat.indexOf('{');
             int cb = pat.indexOf('}', ob);
             if (ob >= 0 && cb > ob) {
               String pname = pat.substring(ob + 1, cb);
               if (pname.length() == 1) {
                 char pc = pname.charAt(0);
                 if (pc == 'X' || pc == 'Y' || pc == 'Z') {
                   argsFailed = true;
                 }
               }
             }
           }
        }
      }

      // If coordinate args were missing or invalid, tell the user and bail
      if (argsFailed) {
        logWarning("TWITCH: Invalid coordinate format from " + username);
        sendTwitchMessage("@" + username + " Invalid coordinates! Usage: !claw x<0-100> y<0-100>  e.g. !claw x50 y75 or !claw x50,y75");
        break;
      }
      
      addToConsole("TWITCH: " + twitchCommands[i].trigger, "rx");
      commandInProgress = true;

      // Execute Actions
      String actions = twitchCommands[i].actions;
      int actionPos = 0;
      while (actionPos < actions.length()) {
        int nextSemi = actions.indexOf(';', actionPos);
        if (nextSemi < 0) nextSemi = actions.length();
        String action = actions.substring(actionPos, nextSemi);
        action.trim();
        
        if (action.length() > 0) {
          action = substituteVars(action, varNames, vars, varCount);
          logDebug("TWITCH", "Exec: " + action);
          
          if (action.startsWith("CHAT:")) {
            sendTwitchMessage(action.substring(5));
          } 
          else if (action.startsWith("CLAW:")) {
            String ca = action.substring(5);
            ca.trim(); ca.toLowerCase();
            if (ca == "open") controlClaw("open",SERVO_OPEN_ANGLE);
            else if (ca == "close") controlClaw("close", SERVO_CLOSE_ANGLE);
            else controlClaw("", ca.toInt());
          } 
          else if (action.startsWith("DELAY:")) {
             delay(action.substring(6).toInt());
          }
          else if (action.startsWith("MOVEPCT:")) {
            String params = action.substring(8);
            params.trim();
            params.replace(",", " ");

            String tok[3];
            int tokCount = 0;
            int idx = 0;
            while (idx < params.length() && tokCount < 3) {
              while (idx < params.length() && params.charAt(idx) == ' ') idx++;
              if (idx >= params.length()) break;
              int end = params.indexOf(' ', idx);
              if (end < 0) end = params.length();
              tok[tokCount] = params.substring(idx, end);
              tok[tokCount].trim();
              tokCount++;
              idx = end + 1;
            }

            if (tokCount >= 2) {
              float xPct, yPct;

              // Strict numeric validation — reject if either coord is malformed
              if (!parseClawCoord(tok[0], xPct) || !parseClawCoord(tok[1], yPct)) {
                logWarning("MOVEPCT: invalid coordinate tokens '" + tok[0] + "' '" + tok[1] + "'");
                sendTwitchMessage("@" + username + " Invalid coordinates! Usage: !claw x<0-100> y<0-100>  e.g. !claw x50 y75");
                commandInProgress = false;
                break;
              }

              // Clamp 0-100
              if (xPct < 0) xPct = 0; if (xPct > 100) xPct = 100;
              if (yPct < 0) yPct = 0; if (yPct > 100) yPct = 100;

              float targetX = MIN_X + (MAX_X - MIN_X) * (xPct / 100.0f);
              float targetY = MIN_Y + (MAX_Y - MIN_Y) * (yPct / 100.0f);
              
              int feedrate = 7000;
              if (tokCount >= 3) {
                String f = tok[2];
                if (f.startsWith("F") || f.startsWith("f")) f = f.substring(1);
                feedrate = f.toInt();
              }
              
              String cmd = "G1 X" + String(targetX, 2) + " Y" + String(targetY, 2) + " F" + String(feedrate);
              sendGcode(cmd, true);
            } else {
              // Not enough tokens at all
              logWarning("MOVEPCT: not enough coordinate tokens");
              sendTwitchMessage("@" + username + " Invalid coordinates! Usage: !claw x<0-100> y<0-100>  e.g. !claw x50 y75");
              commandInProgress = false;
              break;
            }
          }
          else {
             sendGcode(action, true);
          }
        }
        actionPos = nextSemi + 1;
      }
      
      flushTwitchBacklog();
      commandInProgress = false;
      break; 
    }
  }
}

String substituteVars(String action, String varNames[], String varValues[], int varCount) {
  String result = action;
  
  for (int i = 0; i < varCount; i++) {
    String placeholder = "{" + varNames[i] + "}";

    // Replace all occurrences
    int pos = 0;
    while ((pos = result.indexOf(placeholder, pos)) >= 0) {
      result = result.substring(0, pos) + varValues[i] + result.substring(pos + placeholder.length());
      pos += varValues[i].length();
    }
  }
  
  return result;
}

void handleTwitchAddCmd() {
  if (twitchCommandCount >= MAX_TWITCH_COMMANDS) {
    server.send(200, "text/plain", "Max commands reached");
    return;
  }
  
  String trigger = server.arg("trigger");
  String actions = server.arg("actions");
  trigger.trim(); actions.trim();
  
  if (trigger.length() == 0) {
    server.send(200, "text/plain", "Trigger required");
    return;
  }
  
  twitchCommands[twitchCommandCount].trigger = trigger;
  twitchCommands[twitchCommandCount].actions = actions;
  twitchCommands[twitchCommandCount].enabled = true;
  
  // Capture 5 args
  for(int i=0; i<5; i++) {
    String argKey = "arg" + String(i);
    if(server.hasArg(argKey)) {
      twitchCommands[twitchCommandCount].args[i] = server.arg(argKey);
      twitchCommands[twitchCommandCount].args[i].trim();
    } else {
      twitchCommands[twitchCommandCount].args[i] = "";
    }
  }
  
  twitchCommandCount++;
  saveTwitchSettings();
  server.send(200, "text/plain", "Command added!");
}

void handleTwitchEditCmd() {
  int index = server.arg("index").toInt();
  String trigger = server.arg("trigger");
  String actions = server.arg("actions");
  
  if (index >= 0 && index < twitchCommandCount) {
    twitchCommands[index].trigger = trigger;
    twitchCommands[index].actions = actions;
    
    // Capture 5 args
    for(int i=0; i<5; i++) {
      String argKey = "arg" + String(i);
      if(server.hasArg(argKey)) {
        twitchCommands[index].args[i] = server.arg(argKey);
        twitchCommands[index].args[i].trim();
      }
    }
    
    saveTwitchSettings();
    server.send(200, "text/plain", "Updated!");
  } else {
    server.send(400, "text/plain", "Invalid index");
  }
}

void handleTwitchGetCmds() {
  DynamicJsonDocument doc(8192); // Increased size
  JsonArray cmds = doc.to<JsonArray>();
  
  for (int i = 0; i < twitchCommandCount; i++) {
    JsonObject cmd = cmds.createNestedObject();
    cmd["trigger"] = twitchCommands[i].trigger;
    cmd["actions"] = twitchCommands[i].actions;
    cmd["enabled"] = twitchCommands[i].enabled;
    
    JsonArray args = cmd.createNestedArray("args");
    for(int k=0; k<5; k++) {
      args.add(twitchCommands[i].args[k]);
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleTwitch() {
  if (!twitchEnabled) return;

  if (!twitchClient.connected()) {
    if (millis() - lastTwitchReconnect > 10000) {
      lastTwitchReconnect = millis();
      connectToTwitch();
    }
    return;
  }

  while (twitchClient.available()) {
    String line = twitchClient.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    logDebug("TWITCH-RAW", line);

    // Handle PING
    if (line.startsWith("PING")) {
      twitchClient.println("PONG :tmi.twitch.tv");
      logDebug("TWITCH", "Sent PONG");
      continue;
    }

    // **CHECK FOR ERROR MESSAGES**
    if (line.indexOf("NOTICE") >= 0) {
      logError("TWITCH NOTICE: " + line);
      addToConsole("TWITCH ERROR: " + line, "blocked");
      continue;
    }

    // **CHECK FOR SUCCESSFUL JOIN**
    if (line.indexOf("JOIN") >= 0 && line.indexOf(twitchNick) >= 0) {
      logInfo("✓ Bot successfully joined channel");
      continue;
    }

    // **CHECK FOR MODE CHANGES (permissions)**
    if (line.indexOf("MODE") >= 0) {
      logInfo("TWITCH MODE: " + line);
      continue;
    }

    // Handle PRIVMSG
    int privmsgIdx = line.indexOf("PRIVMSG");
    if (privmsgIdx > 0) {
      logDebug("TWITCH-PARSE", "Full line length: " + String(line.length()));

      // Extract username (prefer display-name tag for proper case)
      String displayName = "";
      String loginName = "";
      String username = "";

      // 1) Try display-name from IRCv3 tags: "@...;display-name=Foo;... <space> :login!login@..."
      if (line.startsWith("@")) {
        int tagsEnd = line.indexOf(' ');
        if (tagsEnd > 0) {
          String tags = line.substring(1, tagsEnd); // strip leading '@'
          int dn = tags.indexOf("display-name=");
          if (dn >= 0) {
            int valStart = dn + String("display-name=").length();
            int valEnd = tags.indexOf(';', valStart);
            if (valEnd < 0) valEnd = tags.length();
            displayName = tags.substring(valStart, valEnd);
            displayName.trim();
          }
        }
      }

      // 2) Always extract loginName from the prefix ":login!login@..." (your old behavior)
      int colonBeforeUser = line.lastIndexOf(':', privmsgIdx);
      if (colonBeforeUser >= 0) {
        int exclamIdx = line.indexOf('!', colonBeforeUser);
        logDebug("TWITCH-PARSE",
                 "Colon at: " + String(colonBeforeUser) + ", Exclaim at: " + String(exclamIdx));

        if (exclamIdx > colonBeforeUser && exclamIdx < privmsgIdx) {
          loginName = line.substring(colonBeforeUser + 1, exclamIdx);
          loginName.trim();
          logDebug("TWITCH-PARSE", "Extracted login: '" + loginName + "'");
        }
      }

      // 3) Prefer display-name if set; fallback to loginName
      if (displayName.length() > 0) {
        username = displayName;
      } else {
        username = loginName;
      }

      logDebug("TWITCH", "Username: " + username);

      // Extract message text - Find colon AFTER PRIVMSG
      int msgStart = line.indexOf(':', privmsgIdx);
      logDebug("TWITCH-PARSE", "Message start index: " + String(msgStart));

      if (msgStart > 0 && msgStart + 1 < line.length()) {
        String messageText = line.substring(msgStart + 1);
        messageText.trim();

        logDebug("TWITCH", "Message: " + messageText);
        processTwitchMessage(messageText, username);
      }
    }
  }
}

// ============ WEB SERVER HANDLERS ============

// Main control page (same as before, with config link added)
void handleRoot() {
  logDebug("WEB", "/ request from " + server.client().remoteIP().toString());
  
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
  html += "<title>🎮 Claw Controller</title><style>";
  html += ".uptime{font-size:13px;color:#95a5a6;margin-top:5px;}";
  html += "*{margin:0;padding:0;box-sizing:border-box;}";
  html += "body{font-family:Arial,sans-serif;background:#2c3e50;color:#ecf0f1;padding:20px;}";
  html += ".container{max-width:1000px;margin:0 auto;}";
  html += ".header-bar{background:#34495e;padding:15px;border-radius:8px;margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
  html += ".pos-display{font-family:'Courier New',monospace;font-size:18px;color:#f1c40f;font-weight:bold;}";
  html += ".nav-link{color:#3498db;text-decoration:none;margin-left:15px;font-size:14px;}";
  html += ".nav-link:hover{text-decoration:underline;}";
  html += ".panel{background:#34495e;border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
  html += "#console{background:#000;color:#2ecc71;font-family:'Courier New',monospace;padding:10px;height:250px;overflow-y:auto;margin-bottom:10px;font-size:13px;border:1px solid #7f8c8d;}";
  html += ".tx-line{color:#3498db;}.rx-line{color:#2ecc71;}.blocked-line{color:#e74c3c;}";
  html += ".controls{display:flex;gap:10px;margin-bottom:10px;flex-wrap:wrap;}";
  html += "button{padding:12px 20px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;transition:0.2s;}";
  html += "button:active{transform:scale(0.98);}";
  html += ".btn-primary{background:#2980b9;color:#fff;}.btn-danger{background:#c0392b;color:#fff;}";
  html += ".btn-success{background:#27ae60;color:#fff;}.btn-warning{background:#f39c12;color:#fff;}";
  html += ".btn-info{background:#8e44ad;color:#fff;}";
  html += "input[type=number],input[type=text]{padding:8px;border-radius:4px;border:none;background:#ecf0f1;color:#2c3e50;}";
  
  html += ".bed-section{text-align:center;}";
  html += ".bed-section h3{margin-bottom:15px;color:#3498db;}";
  html += "#bedCanvas{border:2px solid #7f8c8d;background:#1a1a1a;cursor:crosshair;border-radius:4px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
  html += ".bed-controls{display:flex;gap:10px;justify-content:center;align-items:center;margin-top:15px;flex-wrap:wrap;}";
  html += ".coord-input{display:flex;align-items:center;gap:5px;}";
  html += ".coord-input label{font-weight:bold;color:#f1c40f;}";
  html += ".coord-input input{width:80px;text-align:center;}";
  
  html += ".movement-container{display:flex;gap:20px;justify-content:center;align-items:center;flex-wrap:wrap;}";
  html += ".servo-control{background:#2c3e50;padding:15px;border-radius:6px;width:180px;}";
  html += ".servo-control h4{margin-bottom:10px;text-align:center;color:#27ae60;}";
  html += ".slider{width:100%;height:8px;border-radius:5px;background:#7f8c8d;outline:none;margin:10px 0;}";
  html += ".servo-buttons{display:flex;gap:5px;margin-top:10px;}";
  html += ".servo-buttons button{padding:8px 12px;font-size:12px;flex:1;}";
  html += ".angle-display{text-align:center;font-size:18px;color:#f1c40f;margin:5px 0;}";
  
  html += ".movement-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;max-width:600px;}";
  html += ".movement-grid button{height:60px;font-size:16px;}";
  html += ".pos-1{grid-column:1;}.pos-2{grid-column:2;}.pos-3{grid-column:3;}.pos-4{grid-column:4;}.pos-5{grid-column:5;}";
  
  html += ".macro-section{background:#1a3a1a;padding:15px;border-radius:6px;margin-top:15px;}";
  html += ".macro-item{display:inline-block;margin:5px;}";
  html += ".btn-macro{background:#27ae60;color:#fff;margin:5px;padding:10px 15px;}";
  html += ".btn-macro-edit{background:#f39c12;color:#fff;padding:6px 10px;font-size:11px;margin-left:5px;}";
  html += "</style></head><body><div class=\"container\">";
  
  yield();
  
  html += "<div class=\"header-bar\"><div><h2>🎮 Claw Controller</h2><a href=\"/config\" class=\"nav-link\">⚙️ Twitch Config</a></div><div class=\"uptime\" id=\"uptimeDisplay\">Uptime: --</div>";
  html += "<div class=\"pos-display\" id=\"posDisplay\">X:0.0 Y:0.0 Z:0.0</div></div>";
  
  html += "<div class=\"panel\"><div id=\"console\"></div><div class=\"controls\">";
  html += "<input type=\"text\" id=\"commandInput\" placeholder=\"M119...\" style=\"flex:1;\">";
  html += "<button class=\"btn-primary\" onclick=\"sendCommand()\">📤 Send</button>";
  html += "<button class=\"btn-warning\" onclick=\"clearConsole()\">Clear Log</button></div></div>";
  html += "<div class=\"panel\"><div style=\"text-align:center;margin-bottom:20px;\">";
  html += "<button class=\"btn-danger\" style=\"width:100%;font-size:18px;\" onclick=\"emergencyStop()\">🛑 EMERGENCY STOP</button></div>";
  html += "<div class=\"controls\" style=\"justify-content:center;\">";
  html += "<label>Dist: <input type=\"number\" id=\"distance\" value=\"40\" style=\"width:70px;\"></label>";
  html += "<label>Speed: <input type=\"number\" id=\"speed\" value=\"7000\" style=\"width:80px;\"></label>";
  html += "<label>Z-Dist: <input type=\"number\" id=\"zDistance\" value=\"40\" style=\"width:70px;\"></label></div>";
  
  yield();
  
  html += "<div class=\"movement-container\">";
  html += "<div class=\"servo-control\">";
  html += "<h4>🦾 CLAW</h4>";
  html += "<div class=\"angle-display\"><span id=\"angleDisplay\">90</span>°</div>";
  html += "<input type=\"range\" min=\"0\" max=\"180\" value=\"90\" class=\"slider\" id=\"servoSlider\" oninput=\"updateAngleDisplay(this.value)\">";
  html += "<div class=\"servo-buttons\">";
  html += "<button class=\"btn-success\" onclick=\"moveClaw('open')\">OPEN</button>";
  html += "<button class=\"btn-success\" onclick=\"moveClaw('close')\">CLOSE</button>";
  html += "</div>";
  html += "<button class=\"btn-info\" style=\"width:100%;margin-top:5px;\" onclick=\"moveClawAngle()\">Set Angle</button>";
  html += "</div>";
  
  html += "<div class=\"movement-grid\">";
  html += "<button class=\"btn-warning pos-1\" onclick=\"sendCommand('M42 P0 S255 I1')\">LIGHT ON</button>";
  html += "<button class=\"pos-2\"></button>";
  html += "<button class=\"btn-primary pos-3\" onclick=\"moveAxis('Y',1)\">Y+</button>";
  html += "<button class=\"pos-4\"></button>";
  html += "<button class=\"btn-info pos-5\" onclick=\"moveAxis('Z',1)\">Z+</button>";
  html += "<button class=\"btn-warning pos-1\" onclick=\"toggleLight()\">LIGHT TOGGLE</button>";
  html += "<button class=\"btn-primary pos-2\" onclick=\"moveAxis('X',-1)\">X-</button>";
  html += "<button class=\"btn-warning pos-3\" onclick=\"disableMotors()\">OFF</button>";
  html += "<button class=\"btn-primary pos-4\" onclick=\"moveAxis('X',1)\">X+</button>";
  html += "<button class=\"btn-info pos-5\" onclick=\"sendCommand('G28 Z')\">Z HOME</button>";
  html += "<button class=\"btn-warning pos-1\" onclick=\"sendCommand('M42 P0 S0 I1')\">LIGHT OFF</button>";
  html += "<button class=\"pos-2\"></button>";
  html += "<button class=\"btn-primary pos-3\" onclick=\"moveAxis('Y',-1)\">Y-</button>";
  html += "<button class=\"pos-4\"></button>";
  html += "<button class=\"btn-info pos-5\" onclick=\"moveAxis('Z',-1)\">Z-</button>";
  html += "</div></div>";
  
  html += "<div class=\"macro-section\"><h3>⚡ Macros</h3>";
  html += "<div id=\"macroList\"></div>";
  html += "<div style=\"margin-top:15px;\">";
  html += "<input type=\"text\" id=\"macroName\" placeholder=\"Macro name\" style=\"width:150px;\">";
  html += "<input type=\"text\" id=\"macroCommand\" placeholder=\"Command\" style=\"width:250px;\">";
  html += "<button class=\"btn-success\" onclick=\"saveMacro()\">💾 Save Macro</button>";
  html += "<button class=\"btn-danger\" onclick=\"deleteMacroByName()\">🗑️ Delete Macro</button></div></div>";
  
  html += "<div class=\"controls\" style=\"justify-content:center;margin-top:20px;\">";
  html += "<button class=\"btn-primary\" onclick=\"homeAll()\">Home ALL</button>";
  html += "<button class=\"btn-primary\" onclick=\"homeXY()\">Home XY</button>";
  html += "<button class=\"btn-info\" onclick=\"updatePosition()\">Refresh Pos</button></div>";
  
  html += "<div class=\"panel bed-section\" style=\"margin-top:20px;\">";
  html += "<h3>🗺️ Bed Position Map</h3>";
  html += "<canvas id=\"bedCanvas\" width=\"400\" height=\"380\"></canvas>";
  html += "<div class=\"bed-controls\">";
  html += "<div class=\"coord-input\"><label>X:</label><input type=\"number\" id=\"bedX\" value=\"50\" min=\"0\" max=\"100\" step=\"0.1\"><label>%</label></div>";
  html += "<div class=\"coord-input\"><label>Y:</label><input type=\"number\" id=\"bedY\" value=\"50\" min=\"0\" max=\"100\" step=\"0.1\"><label>%</label></div>";
  html += "<button class=\"btn-primary\" onclick=\"moveToPercent()\">📍 Move To Position</button>";
  html += "</div></div></div></div>";
  
  yield();
  
  // JavaScript continues...
  html += "<script>";
  html += "const MIN_X=" + String(MIN_X, 1) + ";";
  html += "const MIN_Y=" + String(MIN_Y, 1) + ";";
  html += "const MAX_X=" + String(MAX_X, 1) + ";";
  html += "const MAX_Y=" + String(MAX_Y, 1) + ";";
  
  html += "var consoleDiv=document.getElementById('console');";
  html += "var posDiv=document.getElementById('posDisplay');";
  html += "var lastConsoleHTML='';";
  html += "var currentX=0,currentY=0;";
  
  html += "var canvas=document.getElementById('bedCanvas');";
  html += "var ctx=canvas.getContext('2d');";
  html += "function drawBed(){";
  html += "ctx.fillStyle='#1a1a1a';ctx.fillRect(0,0,400,380);";
  html += "ctx.strokeStyle='#7f8c8d';ctx.lineWidth=2;";
  html += "ctx.strokeRect(20,20,360,340);";
  html += "ctx.strokeStyle='#3498db';ctx.lineWidth=1;ctx.setLineDash([5,5]);";
  html += "for(var i=1;i<4;i++){";
  html += "var x=20+i*90;ctx.beginPath();ctx.moveTo(x,20);ctx.lineTo(x,360);ctx.stroke();}";
  html += "for(var i=1;i<4;i++){";
  html += "var y=20+i*85;ctx.beginPath();ctx.moveTo(20,y);ctx.lineTo(380,y);ctx.stroke();}";
  html += "ctx.setLineDash([]);";
  html += "ctx.fillStyle='#ecf0f1';ctx.font='12px Arial';";
  html += "ctx.fillText('0,0',5,375);";
  html += "ctx.fillText('100,0',355,375);";
  html += "ctx.fillText('0,100',5,30);";
  html += "ctx.fillText('100,100',345,30);";
  html += "var px=20+(currentX-MIN_X)/(MAX_X-MIN_X)*360;";
  html += "var py=360-(currentY-MIN_Y)/(MAX_Y-MIN_Y)*340;";
  html += "ctx.fillStyle='#e74c3c';ctx.beginPath();ctx.arc(px,py,8,0,2*Math.PI);ctx.fill();";
  html += "ctx.strokeStyle='#fff';ctx.lineWidth=2;ctx.stroke();";
  html += "}";
  
  html += "canvas.addEventListener('click',function(e){";
  html += "var rect=canvas.getBoundingClientRect();";
  html += "var x=e.clientX-rect.left;";
  html += "var y=e.clientY-rect.top;";
  html += "if(x<20||x>380||y<20||y>360)return;";
  html += "var percentX=((x-20)/360*100).toFixed(1);";
  html += "var percentY=(100-(y-20)/340*100).toFixed(1);";
  html += "document.getElementById('bedX').value=percentX;";
  html += "document.getElementById('bedY').value=percentY;";
  html += "moveToPercent();";
  html += "});";
  
  html += "function moveToPercent(){";
  html += "var xPercent=parseFloat(document.getElementById('bedX').value);";
  html += "var yPercent=parseFloat(document.getElementById('bedY').value);";
  html += "xPercent=Math.max(0,Math.min(100,xPercent));";
  html += "yPercent=Math.max(0,Math.min(100,yPercent));";
  html += "var targetX=MIN_X+(MAX_X-MIN_X)*xPercent/100;";
  html += "var targetY=MIN_Y+(MAX_Y-MIN_Y)*yPercent/100;";
  html += "var cmd='G1 X'+targetX.toFixed(2)+' Y'+targetY.toFixed(2)+' F3000';";
  html += "sendCommand(cmd);";
  html += "currentX=targetX;";
  html += "currentY=targetY;";
  html += "drawBed();";
  html += "}";
  
  html += "document.getElementById('commandInput').addEventListener('keypress',function(e){if(e.key==='Enter')sendCommand();});";
  html += "function updateState(){";
  html += "fetch('/state').then(r=>r.json()).then(d=>{";
  html += "if(d.uptime) {document.getElementById('uptimeDisplay').textContent = 'Uptime: ' + d.uptime;}";
  html += "if(d.pos){";
  html += "posDiv.textContent='X:'+d.pos.X.toFixed(1)+' Y:'+d.pos.Y.toFixed(1)+' Z:'+d.pos.Z.toFixed(1);}";
  html += "if(d.messages){var o='';for(var i=0;i<d.messages.length;i++){var m=d.messages[i];";
  html += "var cls=m.type=='tx'?'tx-line':(m.type=='blocked'?'blocked-line':'rx-line');";
  html += "o+='<div class=\"'+cls+'\">'+m.time+' '+m.text.replace(/</g,'&lt;')+'</div>';}";
  html += "if(lastConsoleHTML!=o){consoleDiv.innerHTML=o;consoleDiv.scrollTop=consoleDiv.scrollHeight;lastConsoleHTML=o;}}";
  html += "}).catch(e=>console.error(e));}";
  html += "function updateAngleDisplay(v){document.getElementById('angleDisplay').textContent=v;}";
  html += "function sendCommand(c){if(!c)c=document.getElementById('commandInput').value;";
  html += "if(c)fetch('/send?cmd='+encodeURIComponent(c));";
  html += "if(!c.includes('M42'))document.getElementById('commandInput').value='';setTimeout(updateState,50);}";
  html += "function moveAxis(a,d){";
  html += "var dist=(a=='Z')?document.getElementById('zDistance').value:document.getElementById('distance').value;";
  html += "var speed=document.getElementById('speed').value;";
  html += "var actualDist=d*parseFloat(dist);";
  html += "fetch('/move?axis='+a+'&distance='+actualDist+'&speed='+parseInt(speed));setTimeout(updateState,50);}";
  html += "function moveClaw(a){fetch('/claw?action='+a);}";
  html += "function moveClawAngle(){var angle=document.getElementById('servoSlider').value;fetch('/claw?angle='+angle);}";
  html += "function clearConsole(){fetch('/clear').then(updateState);}";
  html += "function emergencyStop(){fetch('/send?cmd=M112');setTimeout(function(){fetch('/send?cmd=M999');},100);}";
  html += "function homeAll(){fetch('/send?cmd=G28');}";
  html += "function homeXY(){fetch('/send?cmd=G28 X Y');}";
  html += "function disableMotors(){fetch('/send?cmd=M84');}";
  html += "function updatePosition(){fetch('/updatepos').then(r=>r.json()).then(d=>{if(d.pos){currentX=d.pos.X;currentY=d.pos.Y;drawBed();}});}";
  html += "function toggleLight(){fetch('/togglelight');}";
  html += "function saveMacro(){";
  html += "var n=document.getElementById('macroName').value;";
  html += "var c=document.getElementById('macroCommand').value;";
  html += "if(!n||!c)return;";
  html += "fetch('/save-macro?name='+encodeURIComponent(n)+'&cmd='+encodeURIComponent(c)).then(function(){";
  html += "document.getElementById('macroName').value='';";
  html += "document.getElementById('macroCommand').value='';";
  html += "loadMacros();});}";
  html += "function runMacro(c){sendCommand(c);setTimeout(updateState,50);}";
  html += "function editMacro(n,c){";
  html += "document.getElementById('macroName').value=n;";
  html += "document.getElementById('macroCommand').value=c;}";
  html += "function deleteMacroByName(){";
  html += "var n=document.getElementById('macroName').value;";
  html += "if(!n){alert('Enter macro name to delete');return;}";
  html += "if(confirm('Delete macro '+n+'?')){";
  html += "fetch('/delete-macro?name='+encodeURIComponent(n)).then(function(){";
  html += "document.getElementById('macroName').value='';";
  html += "document.getElementById('macroCommand').value='';";
  html += "loadMacros();});}}";
  html += "function loadMacros(){";
  html += "fetch('/get-macros').then(r=>r.json()).then(function(m){";
  html += "var list=document.getElementById('macroList');";
  html += "list.innerHTML='';";
  html += "for(var key in m){";
  html += "if(m.hasOwnProperty(key)){";
  html += "var name=key;";
  html += "var cmd=m[key];";
  html += "var item=document.createElement('div');";
  html += "item.className='macro-item';";
  html += "var btn=document.createElement('button');";
  html += "btn.className='btn-macro';";
  html += "btn.textContent=name;";
  html += "btn.onclick=(function(c){return function(){runMacro(c);};})(cmd);";
  html += "var editBtn=document.createElement('button');";
  html += "editBtn.className='btn-macro-edit';";
  html += "editBtn.textContent='✏️';";
  html += "editBtn.onclick=(function(n,c){return function(){editMacro(n,c);};})(name,cmd);";
  html += "item.appendChild(btn);";
  html += "item.appendChild(editBtn);";
  html += "list.appendChild(item);";
  html += "}}});}";
  html += "drawBed();";
  html += "setInterval(updateState,500);";
  html += "updateState();";
  html += "loadMacros();";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
  logInfo("Sent HTML page (" + String(html.length()) + " bytes)");
}
