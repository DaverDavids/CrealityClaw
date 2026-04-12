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
int SERVO_CLOSE_ANGLE = 54;

// WiFi credentials
const char* ssid = MYSSID;
const char* password = MYPSK;
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
        
        // --- FIX: Update targetZ so subsequent checks know we are safe ---
        if (!hasAxis(cmdUpper, 'Z')) {
           targetZ = currentPos.Z;
        }
        // ---------------------------------------------------------------

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
// Helper to extract a variable from a specific pattern found anywhere in the message
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
  
  // --- FIX: Stop at space OR comma ---
  int spaceIdx = message.indexOf(' ', valStart);
  int commaIdx = message.indexOf(',', valStart);
  
  int valEnd = message.length();
  
  // If we found a space, and it's closer than the end, use it
  if (spaceIdx >= 0 && spaceIdx < valEnd) {
    valEnd = spaceIdx;
  }
  
  // If we found a comma, and it's closer than the current end (or space), use it
  if (commaIdx >= 0 && commaIdx < valEnd) {
    valEnd = commaIdx;
  }
  // ------------------------------------
  
  String val = message.substring(valStart, valEnd);
  val.trim();
  
  // STRICT CHECK: Ensure we didn't capture empty string
  if (val.length() == 0) return false;
  
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
           }
        }
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
          // --- RESTORED BLOCK START ---
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
              float xPct = tok[0].toFloat();
              float yPct = tok[1].toFloat();
              
              // Handle "x50" or "y50" inputs by stripping letters
              if(tok[0].length() > 0 && isAlpha(tok[0].charAt(0))) xPct = tok[0].substring(1).toFloat();
              if(tok[1].length() > 0 && isAlpha(tok[1].charAt(0))) yPct = tok[1].substring(1).toFloat();

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
            }
          }
          // --- RESTORED BLOCK END ---
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
  html += "var cmd='G1 X'+targetX.toFixed(2)+' Y'+targetY.toFixed(2)+' F3000';";  // ✅ Absolute
  html += "sendCommand(cmd);";
  html += "currentX=targetX;";  // Update for drawing
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

// ============ TWITCH CONFIG PAGE ============
void handleConfig() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Twitch Config</title>";
  html += "<style>";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: #1a1a1a; color: #e0e0e0; margin: 0; padding: 20px; }";
  html += ".container { max-width: 900px; margin: 0 auto; }";
  html += ".header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 30px; border-bottom: 1px solid #333; padding-bottom: 20px; }";
  html += "h2 { margin: 0; color: #a970ff; }"; /* Twitch Purple */
  html += "a { color: #fff; text-decoration: none; background: #333; padding: 8px 16px; border-radius: 4px; }";
  html += ".panel { background: #252525; padding: 20px; border-radius: 8px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.3); }";
  html += "h3 { margin-top: 0; color: #fff; border-bottom: 1px solid #444; padding-bottom: 10px; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: 500; color: #aaa; }";
  html += "input[type=text], input[type=password], textarea { width: 100%; padding: 10px; background: #333; border: 1px solid #444; color: #fff; border-radius: 4px; box-sizing: border-box; }";
  html += "input:focus, textarea:focus { border-color: #a970ff; outline: none; }";
  html += "button { cursor: pointer; padding: 10px 20px; border: none; border-radius: 4px; font-weight: bold; color: #fff; transition: opacity 0.2s; }";
  html += "button:hover { opacity: 0.9; }";
  html += ".btn-primary { background: #a970ff; }";
  html += ".btn-danger { background: #d32f2f; }";
  html += ".btn-success { background: #388e3c; }";
  html += ".row { display: flex; gap: 10px; }";
  html += ".col { flex: 1; }";
  
  // Styles for the argument inputs
  html += ".args-container { display: flex; gap: 5px; margin-bottom: 10px; }";
  html += ".args-container input { flex: 1; min-width: 0; font-size: 0.9em; text-align: center; }";
  html += ".cmd-item { border: 1px solid #444; padding: 15px; margin-bottom: 15px; border-radius: 6px; background: #2a2a2a; }";
  html += ".cmd-header { display: flex; justify-content: space-between; margin-bottom: 10px; }";
  html += "</style></head><body><div class=\"container\">";

  // --- Header ---
  html += "<div class=\"header\">";
  html += "<h2>👾 Twitch Configuration</h2>";
  html += "<a href=\"/\">← Back to Dashboard</a>";
  html += "</div>";

  // --- Connection Settings ---
  html += "<div class=\"panel\"><h3>📡 Connection Details</h3>";
  html += "<div class=\"row\">";
  html += "<div class=\"col\"><label>Channel</label><input type=\"text\" id=\"channel\" value=\"" + String(twitchChannel) + "\"></div>";
  html += "<div class=\"col\"><label>Bot Username</label><input type=\"text\" id=\"nick\" value=\"" + String(twitchNick) + "\"></div>";
  html += "</div><br>";
  html += "<label>OAuth Token (oauth:xxxx)</label><input type=\"password\" id=\"oauth\" value=\"" + String(twitchOauth) + "\"><br><br>";
  html += "<div class=\"row\">";
  html += "<div class=\"col\"><label><input type=\"checkbox\" id=\"enabled\" " + String(twitchEnabled ? "checked" : "") + "> Enable Twitch Bot</label></div>";
  html += "</div><br>";
  html += "<button class=\"btn-primary\" onclick=\"saveConn()\">Save Connection</button> ";
  html += "<button class=\"btn-success\" onclick=\"testConn()\">Test Connection</button>";
  html += "</div>";

  // --- Add New Command ---
  html += "<div class=\"panel\"><h3>➕ Add Command</h3>";
  html += "<div class=\"form-group\"><label>Trigger Word (e.g. !claw)</label>";
  html += "<input type=\"text\" id=\"newTrigger\" placeholder=\"!claw\"></div>";
  
  html += "<div class=\"form-group\"><label>Arguments (Context Variables)</label>";
  html += "<div class=\"args-container\">";
  for(int i=0; i<5; i++) {
    html += "<input type=\"text\" id=\"newArg" + String(i) + "\" placeholder=\"x{X}\">";
  }
  html += "</div><small style=\"color:#666\">Enter patterns like <b>x{X}</b> or <b>y{Y}</b>. The system finds these anywhere in the message.</small></div>";
  
  html += "<div class=\"form-group\"><label>Actions (G-code, CHAT:msg, DELAY:ms, CLAW:open)</label>";
  html += "<textarea id=\"newActions\" rows=\"2\" placeholder=\"G1 X{X} Y{Y} F3000\"></textarea></div>";
  html += "<button class=\"btn-success\" onclick=\"addCmd()\">Add Command</button>";
  html += "</div>";

  // --- Command List ---
  html += "<div class=\"panel\"><h3>📋 Active Commands</h3>";
  html += "<div id=\"cmdList\">Loading...</div>";
  html += "</div>";

  // --- JavaScript ---
  html += "<script>";
  html += "function saveConn() {";
  html += "  var e = document.getElementById('enabled').checked ? 1 : 0;";
  html += "  var c = document.getElementById('channel').value;";
  html += "  var n = document.getElementById('nick').value;";
  html += "  var o = document.getElementById('oauth').value;";
  html += "  fetch('/twitch-save-conn?enabled='+e+'&channel='+encodeURIComponent(c)+'&nick='+encodeURIComponent(n)+'&oauth='+encodeURIComponent(o))";
  html += "    .then(r => r.text()).then(t => alert(t));";
  html += "}";

  html += "function testConn() {";
  html += "  fetch('/twitch-test').then(r => r.text()).then(t => alert(t));";
  html += "}";

  html += "function addCmd() {";
  html += "  var t = document.getElementById('newTrigger').value;";
  html += "  var a = document.getElementById('newActions').value;";
  html += "  var q = '';";
  html += "  for(var i=0; i<5; i++) q += '&arg'+i+'=' + encodeURIComponent(document.getElementById('newArg'+i).value);";
  html += "  fetch('/twitch-add-cmd?trigger='+encodeURIComponent(t)+'&actions='+encodeURIComponent(a)+q)";
  html += "    .then(r => r.text()).then(t => { alert(t); location.reload(); });";
  html += "}";

  html += "function saveCmd(idx) {";
  html += "  var t = document.getElementById('trig_'+idx).value;";
  html += "  var a = document.getElementById('act_'+idx).value;";
  html += "  var q = '';";
  html += "  for(var i=0; i<5; i++) q += '&arg'+i+'=' + encodeURIComponent(document.getElementById('arg_'+idx+'_'+i).value);";
  html += "  fetch('/twitch-edit-cmd?index='+idx+'&trigger='+encodeURIComponent(t)+'&actions='+encodeURIComponent(a)+q)";
  html += "    .then(r => r.text()).then(t => alert(t));";
  html += "}";

  html += "function delCmd(idx) {";
  html += "  if(confirm('Are you sure?')) fetch('/twitch-delete-cmd?index='+idx).then(() => location.reload());";
  html += "}";

  html += "function toggleCmd(idx, checkbox) {";
  html += "  fetch('/twitch-toggle-cmd?index='+idx+'&enabled='+(checkbox.checked?1:0));";
  html += "}";

  html += "fetch('/twitch-get-cmds').then(r => r.json()).then(data => {";
  html += "  var h = '';";
  html += "  data.forEach((c, i) => {";
  html += "    h += '<div class=\"cmd-item\">';";
  html += "    h += '<div class=\"cmd-header\">';";
  html += "    h += '  <div><input type=\"text\" id=\"trig_'+i+'\" value=\"'+c.trigger+'\" style=\"width:150px;font-weight:bold\"></div>';";
  html += "    h += '  <div><label><input type=\"checkbox\" '+(c.enabled?'checked':'')+' onchange=\"toggleCmd('+i+',this)\"> Enabled</label></div>';";
  html += "    h += '</div>';";
  
  // Render the 5 argument boxes for editing
  html += "    h += '<div class=\"args-container\">';";
  html += "    for(var k=0; k<5; k++) {";
  html += "      var val = (c.args && c.args[k]) ? c.args[k] : '';";
  html += "      h += '<input type=\"text\" id=\"arg_'+i+'_'+k+'\" value=\"'+val+'\" placeholder=\"Arg '+(k+1)+'\">';";
  html += "    }";
  html += "    h += '</div>';";
  
  html += "    h += '<textarea id=\"act_'+i+'\" rows=\"2\">'+c.actions+'</textarea>';";
  html += "    h += '<div style=\"margin-top:10px;text-align:right\">';";
  html += "    h += '  <button class=\"btn-primary\" style=\"padding:5px 15px\" onclick=\"saveCmd('+i+')\">Save</button> ';";
  html += "    h += '  <button class=\"btn-danger\" style=\"padding:5px 15px\" onclick=\"delCmd('+i+')\">Delete</button>';";
  html += "    h += '</div></div>';";
  html += "  });";
  html += "  document.getElementById('cmdList').innerHTML = h || '<p>No commands configured.</p>';";
  html += "});";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

// ============ TWITCH API HANDLERS ============
void handleTwitchSaveConn() {
  if (server.hasArg("enabled")) {
    twitchEnabled = server.arg("enabled") == "1";
  }
  if (server.hasArg("channel")) {
    twitchChannel = server.arg("channel");
    twitchChannel.trim();
  }
  if (server.hasArg("nick")) {
    twitchNick = server.arg("nick");
    twitchNick.trim();
  }
  if (server.hasArg("oauth")) {
    twitchOauth = server.arg("oauth");
    twitchOauth.trim();
  }
  
  saveTwitchSettings();
  
  if (twitchEnabled) {
    twitchClient.stop();
    connectToTwitch();
  }
  
  server.send(200, "text/plain", "Connection settings saved!");
}

void handleTwitchTest() {
  if (!twitchEnabled) {
    server.send(200, "text/plain", "Twitch is disabled");
    return;
  }
  
  if (twitchClient.connected()) {
    server.send(200, "text/plain", "✅ Connected to Twitch!\nChannel: #" + twitchChannel);
  } else {
    server.send(200, "text/plain", "❌ Not connected. Check your settings and try again.");
  }
}

void handleTwitchToggleCmd() {
  int index = server.arg("index").toInt();
  bool enabled = server.arg("enabled") == "1";
  
  if (index >= 0 && index < twitchCommandCount) {
    twitchCommands[index].enabled = enabled;
    saveTwitchSettings();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid index");
  }
}

void handleTwitchDeleteCmd() {
  int index = server.arg("index").toInt();
  
  if (index >= 0 && index < twitchCommandCount) {
    // Shift all commands down
    for (int i = index; i < twitchCommandCount - 1; i++) {
      twitchCommands[i] = twitchCommands[i + 1];
    }
    twitchCommandCount--;
    
    saveTwitchSettings();
    server.send(200, "text/plain", "Command deleted");
    logInfo("Deleted Twitch command at index " + String(index));
  } else {
    server.send(400, "text/plain", "Invalid index");
  }
}


// ============ EXISTING WEB HANDLERS (from original code) ============
void handleState() {
  //logDebug("WEB", "/state request");
  
  DynamicJsonDocument doc(4096);
  
  doc["uptime"] = getUptimeString();

  JsonObject pos = doc.createNestedObject("pos");
  pos["X"] = currentPos.X;
  pos["Y"] = currentPos.Y;
  pos["Z"] = currentPos.Z;
  pos["E"] = currentPos.E;
  
  JsonArray messages = doc.createNestedArray("messages");
  
  int startIdx = (consoleCount < CONSOLE_BUFFER_SIZE) ? 0 : consoleIndex;
  int count = min(consoleCount, CONSOLE_BUFFER_SIZE);
  
  for (int i = 0; i < count; i++) {
    int idx = (startIdx + i) % CONSOLE_BUFFER_SIZE;
    JsonObject msg = messages.createNestedObject();
    msg["time"] = consoleBuffer[idx].time;
    msg["type"] = consoleBuffer[idx].type;
    msg["text"] = consoleBuffer[idx].text;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSend() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    logDebug("WEB", "/send command: " + cmd);
    sendGcode(cmd);
    
    if (cmd.indexOf("M42 P0 S255") >= 0) {
      lightState = true;
    } else if (cmd.indexOf("M42 P0 S0") >= 0) {
      lightState = false;
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleMove() {
  if (server.hasArg("axis") && server.hasArg("distance") && server.hasArg("speed")) {
    String axisStr = server.arg("axis");
    float distance = server.arg("distance").toFloat();
    int speed = server.arg("speed").toInt();
    
    logDebug("WEB", "/move " + axisStr + " delta:" + String(distance, 2) + " F" + String(speed));
    
    if (axisStr.length() > 0) {
      char axis = axisStr.charAt(0);
      
      // Get current position for this axis
      float currentPos_axis = 0;
      switch(axis) {
        case 'X': currentPos_axis = currentPos.X; break;
        case 'Y': currentPos_axis = currentPos.Y; break;
        case 'Z': currentPos_axis = currentPos.Z; break;
        default: currentPos_axis = 0; break;
      }
      
      float targetPosition = currentPos_axis + distance;
      
      // ============ WALL AVOIDANCE ============
      float targetX = currentPos.X;
      float targetY = currentPos.Y;
      float targetZ = currentPos.Z;
      
      switch(axis) {
        case 'X': targetX = targetPosition; break;
        case 'Y': targetY = targetPosition; break;
        case 'Z': targetZ = targetPosition; break;
      }
      
      // Case 1: Z is high and trying to move X/Y into danger
      if (currentPos.Z > WALL_Z_TRIGGER && (axis == 'X' || axis == 'Y')) {
        if (isInWallDangerZone(targetX, targetY, currentPos.Z)) {
          logWarning("Cannot move X/Y into danger zone while Z is high - lowering Z first");
          addToConsole("WALL AVOID: Lowering Z to " + String(WALL_Z_TRIGGER - 5, 1), "blocked");
          
          String lowerCmd = "G1 Z" + String(WALL_Z_TRIGGER - 5.0, 2) + " F5000";
          sendGcodeRaw(lowerCmd);
          currentPos.Z = WALL_Z_TRIGGER - 5.0;
          delay(1000);  // Wait for Z to lower
        }
      }
      
      // Case 2: Raising Z while in danger zone
      if (axis == 'Z' && targetZ > WALL_Z_TRIGGER) {
        if (isInWallDangerZone(currentPos.X, currentPos.Y, targetZ)) {
          logWarning("Cannot raise Z in danger zone - moving X/Y to safety first");
          moveToSafePosition();
        }
      }
      // ============ END WALL AVOIDANCE ============
      
      // Build G-code and send through main handler
      String cmd = "G1 " + String(axis) + String(targetPosition, 2) + " F" + String(speed);
      sendGcode(cmd, false);  // Don't echo, we already logged it
      
      server.send(200, "application/json", "{\"success\":true}");
      return;
    }
  }
  server.send(200, "application/json", "{\"success\":false}");
}

void handleUpdatePos() {
  logDebug("WEB", "/updatepos request");
  updatePositionInternal();
  
  unsigned long start = millis();
  while (positionUpdatePending && (millis() - start) < 500) {
    readPrinterBus();
    yield();
    delay(10);
  }
  
  DynamicJsonDocument doc(256);
  doc["status"] = "ok";
  JsonObject pos = doc.createNestedObject("pos");
  pos["X"] = currentPos.X;
  pos["Y"] = currentPos.Y;
  pos["Z"] = currentPos.Z;
  pos["E"] = currentPos.E;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleClaw() {
  if (server.hasArg("angle")) {
    int angle = server.arg("angle").toInt();
    logDebug("WEB", "/claw angle=" + String(angle));
    controlClaw("", angle);
  } else if (server.hasArg("action")) {
    String action = server.arg("action");
    logDebug("WEB", "/claw action=" + action);
    controlClaw(action);
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleClear() {
  logDebug("WEB", "/clear console");
  consoleIndex = 0;
  consoleCount = 0;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleToggleLight() {
  lightState = !lightState;
  logInfo("Light toggled: " + String(lightState ? "ON" : "OFF"));
  String cmd = lightState ? "M42 P0 S255 I1" : "M42 P0 S0 I1";
  sendGcode(cmd);
  
  DynamicJsonDocument doc(128);
  doc["status"] = "ok";
  doc["lighton"] = lightState;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}
void handleSaveMacro() {
  String name = server.arg("name");
  String cmd = server.arg("cmd");
  
  logInfo("Saving macro '" + name + "': " + cmd);
  
  prefs.begin("claw-machine", false);  // ✅ OPEN
  
  for (int i = 0; i < 50; i++) {
    String key = "macro" + String(i);
    if (!prefs.isKey(key.c_str())) {
      prefs.putString(key.c_str(), name + "|" + cmd);
      prefs.end();  // ✅ CLOSE
      server.send(200, "text/plain", "OK");
      logDebug("MACRO", "Saved to slot " + String(i));
      return;
    } else {
      String value = prefs.getString(key.c_str(), "");
      int sep = value.indexOf('|');
      if (sep > 0) {
        String storedName = value.substring(0, sep);
        if (storedName == name) {
          prefs.putString(key.c_str(), name + "|" + cmd);
          prefs.end();  // ✅ CLOSE
          server.send(200, "text/plain", "OK");
          logDebug("MACRO", "Updated slot " + String(i));
          return;
        }
      }
    }
  }
  
  prefs.end();  // ✅ CLOSE
  logError("No space for macro!");
  server.send(500, "text/plain", "No space");
}

void handleGetMacros() {
  logDebug("WEB", "/get-macros request");
  
  prefs.begin("claw-machine", true);  // ✅ OPEN (read-only)
  
  DynamicJsonDocument doc(2048);
  
  for (int i = 0; i < 50; i++) {
    String key = "macro" + String(i);
    if (prefs.isKey(key.c_str())) {
      String value = prefs.getString(key.c_str(), "");
      if (value.length() > 0) {
        int sep = value.indexOf('|');
        if (sep > 0) {
          String name = value.substring(0, sep);
          String cmd = value.substring(sep + 1);
          doc[name] = cmd;
        }
      }
    }
  }
  
  prefs.end();  // ✅ CLOSE
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleDeleteMacro() {
  String name = server.arg("name");
  logInfo("Deleting macro: " + name);
  
  prefs.begin("claw-machine", false);  // ✅ OPEN
  
  for (int i = 0; i < 50; i++) {
    String key = "macro" + String(i);
    if (prefs.isKey(key.c_str())) {
      String value = prefs.getString(key.c_str(), "");
      int sep = value.indexOf('|');
      if (sep > 0) {
        String storedName = value.substring(0, sep);
        if (storedName == name) {
          prefs.remove(key.c_str());
          logDebug("MACRO", "Deleted from slot " + String(i));
          break;
        }
      }
    }
  }
  
  prefs.end();  // ✅ CLOSE
  server.send(200, "text/plain", "OK");
}

void setupWebServer() {
  // Main control page
  server.on("/", HTTP_GET, handleRoot);
  server.on("/state", HTTP_GET, handleState);
  server.on("/send", HTTP_GET, handleSend);
  server.on("/move", HTTP_GET, handleMove);
  server.on("/updatepos", HTTP_GET, handleUpdatePos);
  server.on("/claw", HTTP_GET, handleClaw);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/togglelight", HTTP_GET, handleToggleLight);
  server.on("/save-macro", HTTP_GET, handleSaveMacro);
  server.on("/get-macros", HTTP_GET, handleGetMacros);
  server.on("/delete-macro", HTTP_GET, handleDeleteMacro);
  server.on("/twitch-edit-cmd", HTTP_GET, handleTwitchEditCmd);
  
  // Twitch config page
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/twitch-save-conn", HTTP_GET, handleTwitchSaveConn);
  server.on("/twitch-test", HTTP_GET, handleTwitchTest);
  server.on("/twitch-add-cmd", HTTP_GET, handleTwitchAddCmd);
  server.on("/twitch-toggle-cmd", HTTP_GET, handleTwitchToggleCmd);
  server.on("/twitch-delete-cmd", HTTP_GET, handleTwitchDeleteCmd);
  server.on("/twitch-get-cmds", HTTP_GET, handleTwitchGetCmds);
  
  server.begin();
  logInfo("Web server started");
}

// ============ SETUP ============
void setup() {
  pinMode(LED_PIN, OUTPUT);
  ledOn();
  
  Serial.begin(115200);
  delay(1000);
  
  logInfo("=================================");
  logInfo("Creality Claw Machine Controller");
  logInfo("=================================");
  logInfo("Debug logging: " + String(DEBUG_LOGGING ? "ENABLED" : "DISABLED"));
  
  PrinterSerial.begin(115200, SERIAL_8N1, PRINTER_RX_PIN, PRINTER_TX_PIN);
  delay(100);
  while (PrinterSerial.available()) {
    PrinterSerial.read();
  }
  logInfo("Printer serial initialized");
  
  clawServo.attach(SERVO_PIN);
  setServoAngle(SERVO_OPEN_ANGLE);
  
  // Load Twitch settings
  loadTwitchSettings();
  
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_18_5dBm);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, password);
  
  logInfo("Connecting to WiFi: " + String(ssid));
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts++ < 60) {
    delay(500);
    digitalWrite(LED_PIN, attempts % 2 == 0 ? LOW : HIGH);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    ledSuccess();
    logInfo("WiFi connected!");
    logInfo("IP address: " + WiFi.localIP().toString());
    logInfo("RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    if (MDNS.begin(hostname)) {
      MDNS.addService("http", "tcp", 80);
      logInfo("mDNS started: http://" + String(hostname) + ".local");
    }
  } else {
    WiFi.softAP("ClawMachineAP", "12345678");
    ledError(3);
    logWarning("WiFi failed, started AP mode");
    logInfo("AP IP: " + WiFi.softAPIP().toString());
  }
  
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() { 
    ledOn(); 
    logInfo("OTA update started");
  });
  ArduinoOTA.onEnd([]() { 
    ledOff(); 
    logInfo("OTA update complete");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    digitalWrite(LED_PIN, (progress / 1000) % 2);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logError("OTA error: " + String(error));
  });
  ArduinoOTA.begin();
  logInfo("OTA enabled");
  
  setupWebServer();
  
    delay(500);
  logInfo("Initializing printer...");
  PrinterSerial.print("\n\n\n");
  addToConsole("G28 Z", "tx");
  sendGcodeRaw("G28 Z\r\n");
  delay(2000);
  addToConsole("G28 X Y", "tx");
  sendGcodeRaw("G28 X Y\r\n");
  delay(2000);
  addToConsole("G28 Z", "tx");
  sendGcodeRaw("G28 Z\r\n");
  delay(1000);
  addToConsole("G90", "tx");  // ✅ CHANGED: Absolute positioning
  sendGcodeRaw("G90\r\n");
  updatePositionInternal();
  addToConsole("M203 Z30", "tx");
  sendGcodeRaw("M203 Z30\r\n");

  // Connect to Twitch if enabled
  if (twitchEnabled) {
    connectToTwitch();
  }
  
  ledOff();
  logInfo("=================================");
  logInfo("Setup complete - Ready!");
  logInfo("=================================");
}

// ============ LOOP ============
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  readPrinterBus();
  handleTwitch();  // NEW: Process Twitch messages
  heartbeatLED();
  handleLEDBlink();
  checkWiFi();
  yield();
}
