#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servo setup
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVO_FREQ 50
#define FRET_SERVO_COUNT 8
#define STRUM_SERVO_START 8
#define STRUM_SERVO_COUNT 4
#define GROUP_DELAY_SHORT 100

#define SERVO_MIN 8
#define SERVO_MAX 11

// Rotary encoder setup
#define ENCODER_CLK 2
#define ENCODER_DT 3
#define ENCODER_BUTTON 4

// EEPROM setup
#define HEADER_ADDR 0
#define LENGTH_ADDR 4
#define DATA_START_ADDR 10
const uint32_t VALID_HEADER = 0xABCD1234;

// Strum angle storage
#define STRUM_MAGIC 0x5A5A  // 2 bytes
#define STRUM_ANGLES_SIZE (STRUM_SERVO_COUNT * 2 * 2) // 16 bytes
#define STRUM_DATA_TOTAL (2 + STRUM_ANGLES_SIZE + 2) // magic + angles + checksum = 20 bytes
int eepromSize = 0;
int strumBaseAddr = 0; // = eepromSize - STRUM_DATA_TOTAL

// Struct to hold angles
struct StrumAngles {
  uint16_t magic;
  int defaults[STRUM_SERVO_COUNT];
  int actives[STRUM_SERVO_COUNT];
  uint16_t checksum;
};

// Global variables
bool servoStates[16] = {false};
int servoPositions[FRET_SERVO_COUNT] = {0};
String sequences[50];
int sequenceTValues[50] = {60};
int sequenceCount = 0;
int currentSelection = 0;

// Strum servo angles
int strumDefaultAngles[STRUM_SERVO_COUNT] = {100, 88, 87, 98};
int strumActiveAngles[STRUM_SERVO_COUNT] = {110, 99, 96, 108};

// Encoder variables
int lastCLKState;
unsigned long lastEncoderUpdate = 0;
unsigned long lastButtonPress = 0;
#define DEBOUNCE_DELAY 50

// Function prototypes
uint16_t calculateChecksum(int* defaults, int* actives);
bool loadStrumAnglesFromEEPROM();
void saveStrumAnglesToEEPROM();

void setup() {
  Serial.begin(115200);
  
  // Get actual EEPROM size
  eepromSize = EEPROM.length();
  strumBaseAddr = eepromSize - STRUM_DATA_TOTAL;
  
  Serial.print("EEPROM size: ");
  Serial.println(eepromSize);
  Serial.print("Strum storage base address: ");
  Serial.println(strumBaseAddr);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  // Initialize servo driver
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);

  // Load strum angles from EEPROM (or use defaults if invalid)
  if (!loadStrumAnglesFromEEPROM()) {
    // No valid data, save defaults
    saveStrumAnglesToEEPROM();
  }

  // Initialize servos to neutral
  for (int i = 0; i < FRET_SERVO_COUNT; i++) {
    setServoAngle(i, 94);
    servoPositions[i] = 0;
  }
  for (int i = STRUM_SERVO_START; i < STRUM_SERVO_START + STRUM_SERVO_COUNT; i++) {
    setServoAngle(i, strumDefaultAngles[i - STRUM_SERVO_START]);
  }

  // Setup rotary encoder pins
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  
  lastCLKState = digitalRead(ENCODER_CLK);

  // Load sequences from EEPROM
  loadSequencesFromEEPROM();
  
  updateDisplay();
  
  Serial.println("Ready. Send sequence string (format: 'title,T,command*multiplier,...'):");
  Serial.println("Example: 'sun,60,8,-7*4,33*4,-3*4'");
  Serial.println("Send 'CLEAR' to clear all EEPROM data");
  Serial.println("Send 'SETSTRUM <servoIndex> <defaultAngle> <activeAngle>' to set strum angles (index 0-3)");
  Serial.println("Send 'GETSTRUM' to print current strum angles");
  Serial.println("Send 'TESTEEPROM' to test EEPROM read/write for strum storage");
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      if (input.equalsIgnoreCase("CLEAR")) {
        clearEEPROM();
      }
      else if (input.startsWith("SETSTRUM")) {
        handleSetStrumCommand(input);
      }
      else if (input.equalsIgnoreCase("GETSTRUM")) {
        printStrumAngles();
      }
      else if (input.equalsIgnoreCase("TESTEEPROM")) {
        testEEPROM();
      }
      else {
        processSerialInput(input);
      }
    }
  }
  
  handleEncoder();
  handleButton();
  delay(10);
}

// ------------------ Strum Angle Functions ------------------

uint16_t calculateChecksum(int* defaults, int* actives) {
  uint16_t sum = 0;
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    sum += defaults[i];
    sum += actives[i];
  }
  return sum;
}

bool loadStrumAnglesFromEEPROM() {
  StrumAngles data;
  EEPROM.get(strumBaseAddr, data);
  
  // Check magic
  if (data.magic != STRUM_MAGIC) {
    Serial.println("Strum EEPROM: invalid magic, using defaults.");
    return false;
  }
  
  // Validate angles
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    if (data.defaults[i] < 0 || data.defaults[i] > 180 ||
        data.actives[i] < 0 || data.actives[i] > 180) {
      Serial.println("Strum EEPROM: angle out of range, using defaults.");
      return false;
    }
  }
  
  // Verify checksum
  uint16_t calc = calculateChecksum(data.defaults, data.actives);
  if (calc != data.checksum) {
    Serial.println("Strum EEPROM: checksum mismatch, using defaults.");
    return false;
  }
  
  // All good, copy to globals
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    strumDefaultAngles[i] = data.defaults[i];
    strumActiveAngles[i] = data.actives[i];
  }
  Serial.println("Strum angles loaded from EEPROM.");
  return true;
}

void saveStrumAnglesToEEPROM() {
  StrumAngles data;
  data.magic = STRUM_MAGIC;
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    data.defaults[i] = strumDefaultAngles[i];
    data.actives[i] = strumActiveAngles[i];
  }
  data.checksum = calculateChecksum(data.defaults, data.actives);
  
  EEPROM.put(strumBaseAddr, data);
  
  // Commit if needed (ESP8266/ESP32)
  #if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
  #endif
  
  Serial.println("Strum angles saved to EEPROM.");
}

void handleSetStrumCommand(String input) {
  String params = input.substring(8); // remove "SETSTRUM"
  params.trim();
  
  if (params.length() == 0) {
    Serial.println("Usage: SETSTRUM <servoIndex> <defaultAngle> <activeAngle>");
    return;
  }
  
  int firstSpace = params.indexOf(' ');
  if (firstSpace == -1) {
    Serial.println("Error: Missing angles. Usage: SETSTRUM <servoIndex> <defaultAngle> <activeAngle>");
    return;
  }
  int secondSpace = params.indexOf(' ', firstSpace + 1);
  if (secondSpace == -1) {
    Serial.println("Error: Missing active angle. Usage: SETSTRUM <servoIndex> <defaultAngle> <activeAngle>");
    return;
  }
  
  String indexStr = params.substring(0, firstSpace);
  String defaultStr = params.substring(firstSpace + 1, secondSpace);
  String activeStr = params.substring(secondSpace + 1);
  activeStr.trim();
  
  int index = indexStr.toInt();
  int defAngle = defaultStr.toInt();
  int actAngle = activeStr.toInt();
  
  if (index < 0 || index >= STRUM_SERVO_COUNT) {
    Serial.print("Error: servoIndex must be 0-");
    Serial.println(STRUM_SERVO_COUNT - 1);
    return;
  }
  
  if (defAngle < 0 || defAngle > 180 || actAngle < 0 || actAngle > 180) {
    Serial.println("Error: angles must be between 0 and 180.");
    return;
  }
  
  strumDefaultAngles[index] = defAngle;
  strumActiveAngles[index] = actAngle;
  
  // Apply immediately
  int servoNum = STRUM_SERVO_START + index;
  if (servoStates[servoNum]) {
    setServoAngle(servoNum, strumActiveAngles[index]);
  } else {
    setServoAngle(servoNum, strumDefaultAngles[index]);
  }
  
  saveStrumAnglesToEEPROM();
  
  Serial.print("Strum servo ");
  Serial.print(index);
  Serial.print(" updated: default=");
  Serial.print(defAngle);
  Serial.print(", active=");
  Serial.println(actAngle);
}

void printStrumAngles() {
  Serial.println("Current strum servo angles:");
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    Serial.print("Servo ");
    Serial.print(i);
    Serial.print(": default=");
    Serial.print(strumDefaultAngles[i]);
    Serial.print(", active=");
    Serial.println(strumActiveAngles[i]);
  }
}

void testEEPROM() {
  Serial.println("Testing EEPROM read/write for strum storage...");
  // Save current angles
  StrumAngles original;
  EEPROM.get(strumBaseAddr, original);
  
  // Write test pattern
  StrumAngles test;
  test.magic = STRUM_MAGIC;
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    test.defaults[i] = i + 10;
    test.actives[i] = i + 100;
  }
  test.checksum = calculateChecksum(test.defaults, test.actives);
  EEPROM.put(strumBaseAddr, test);
  #if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
  #endif
  
  // Read back
  StrumAngles readback;
  EEPROM.get(strumBaseAddr, readback);
  
  bool success = (readback.magic == test.magic &&
                  readback.checksum == test.checksum);
  for (int i = 0; i < STRUM_SERVO_COUNT; i++) {
    if (readback.defaults[i] != test.defaults[i] || readback.actives[i] != test.actives[i]) {
      success = false;
    }
  }
  
  if (success) {
    Serial.println("EEPROM test PASSED.");
  } else {
    Serial.println("EEPROM test FAILED. Check wiring/board.");
  }
  
  // Restore original data
  EEPROM.put(strumBaseAddr, original);
  #if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
  #endif
}

// ------------------ Sequence Functions (unchanged except for dynamic EEPROM size) ------------------

void processSerialInput(String input) {
  int firstComma = input.indexOf(',');
  int secondComma = (firstComma != -1) ? input.indexOf(',', firstComma + 1) : -1;
  
  if (firstComma != -1 && secondComma != -1) {
    String tStr = input.substring(firstComma + 1, secondComma);
    int tValue = tStr.toInt();
    
    if (tValue <= 0) {
      Serial.println("Error: Invalid T value. Must be greater than 0.");
      return;
    }
    
    saveSequence(input);
    loadSequencesFromEEPROM();
    updateDisplay();
    
    String title = input.substring(0, firstComma);
    String commands = input.substring(secondComma + 1);
    
    Serial.print("Sequence saved! Title: '");
    Serial.print(title);
    Serial.print("', T=");
    Serial.print(tValue);
    Serial.print("ms, Commands: '");
    Serial.print(commands);
    Serial.println("'");
  } else {
    Serial.println("Error: Invalid format. Expected 'title,T,commands...'");
  }
}

void saveSequence(String sequence) {
  uint16_t currentLength;
  EEPROM.get(LENGTH_ADDR, currentLength);
  
  uint16_t newLength = currentLength + sequence.length() + 1;
  
  // Reserve space for strum angles at the end
  if (newLength > eepromSize - DATA_START_ADDR - STRUM_DATA_TOTAL) {
    Serial.println("Error: EEPROM full! Send 'CLEAR' to clear all data.");
    return;
  }
  
  Serial.print("Saving... ");
  
  for (uint16_t i = 0; i < sequence.length(); i++) {
    EEPROM.write(DATA_START_ADDR + currentLength + i, sequence[i]);
    if (i % 20 == 0) {
      Serial.print(".");
    }
  }
  
  EEPROM.write(DATA_START_ADDR + currentLength + sequence.length(), '\0');
  
  EEPROM.put(LENGTH_ADDR, newLength);
  EEPROM.put(HEADER_ADDR, VALID_HEADER);
  
  #if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
  #endif
  
  Serial.println(" Done!");
}

void loadSequencesFromEEPROM() {
  uint32_t storedHeader;
  EEPROM.get(HEADER_ADDR, storedHeader);
  
  if (storedHeader != VALID_HEADER) {
    sequenceCount = 0;
    EEPROM.put(HEADER_ADDR, VALID_HEADER);
    uint16_t zero = 0;
    EEPROM.put(LENGTH_ADDR, zero);
    #if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
    #endif
    return;
  }
  
  uint16_t dataLength;
  EEPROM.get(LENGTH_ADDR, dataLength);
  
  if (dataLength == 0) {
    sequenceCount = 0;
    return;
  }
  
  sequenceCount = 0;
  String currentSequence = "";
  
  for (uint16_t i = 0; i < dataLength && sequenceCount < 50; i++) {
    char c = EEPROM.read(DATA_START_ADDR + i);
    
    if (c == '\0') {
      if (currentSequence.length() > 0) {
        sequences[sequenceCount] = currentSequence;
        
        int firstComma = currentSequence.indexOf(',');
        int secondComma = (firstComma != -1) ? currentSequence.indexOf(',', firstComma + 1) : -1;
        
        if (firstComma != -1 && secondComma != -1) {
          String tStr = currentSequence.substring(firstComma + 1, secondComma);
          sequenceTValues[sequenceCount] = tStr.toInt();
        } else {
          sequenceTValues[sequenceCount] = 60;
        }
        
        sequenceCount++;
        currentSequence = "";
      }
    } else {
      currentSequence += c;
    }
  }
  
  if (currentSequence.length() > 0 && sequenceCount < 50) {
    sequences[sequenceCount] = currentSequence;
    
    int firstComma = currentSequence.indexOf(',');
    int secondComma = (firstComma != -1) ? currentSequence.indexOf(',', firstComma + 1) : -1;
    
    if (firstComma != -1 && secondComma != -1) {
      String tStr = currentSequence.substring(firstComma + 1, secondComma);
      sequenceTValues[sequenceCount] = tStr.toInt();
    } else {
      sequenceTValues[sequenceCount] = 60;
    }
    
    sequenceCount++;
  }
}

void clearEEPROM() {
  Serial.print("Clearing EEPROM... ");
  
  EEPROM.put(HEADER_ADDR, (uint32_t)0);
  EEPROM.put(LENGTH_ADDR, (uint16_t)0);
  
  // Clear only sequence area, leave strum angles intact
  for (int i = DATA_START_ADDR; i < strumBaseAddr; i += 16) {
    EEPROM.write(i, 0);
  }
  
  #if defined(ESP8266) || defined(ESP32)
  EEPROM.commit();
  #endif
  
  sequenceCount = 0;
  currentSelection = 0;
  updateDisplay();
  
  Serial.println("Done! EEPROM cleared.");
}

// ------------------ Rotary Encoder & Display (unchanged) ------------------

void handleEncoder() {
  int currentCLKState = digitalRead(ENCODER_CLK);
  
  if (currentCLKState != lastCLKState && currentCLKState == HIGH) {
    if (millis() - lastEncoderUpdate > DEBOUNCE_DELAY) {
      int dtState = digitalRead(ENCODER_DT);
      
      if (dtState != currentCLKState) {
        if (currentSelection < sequenceCount - 1) {
          currentSelection++;
        }
      } else {
        if (currentSelection > 0) {
          currentSelection--;
        }
      }
      
      updateDisplay();
      lastEncoderUpdate = millis();
    }
  }
  
  lastCLKState = currentCLKState;
}

void handleButton() {
  int buttonState = digitalRead(ENCODER_BUTTON);
  
  if (buttonState == LOW) {
    if (millis() - lastButtonPress > 300) {
      if (sequenceCount > 0 && currentSelection < sequenceCount) {
        executeSelectedSequence();
      }
      lastButtonPress = millis();
    }
  }
}

void executeSelectedSequence() {
  if (sequenceCount == 0 || currentSelection >= sequenceCount) return;
  
  String fullSequence = sequences[currentSelection];
  int firstComma = fullSequence.indexOf(',');
  int secondComma = (firstComma != -1) ? fullSequence.indexOf(',', firstComma + 1) : -1;
  
  if (firstComma != -1 && secondComma != -1) {
    String sequenceData = fullSequence.substring(secondComma + 1);
    sequenceData.trim();
    
    int tValue = sequenceTValues[currentSelection];
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Executing...");
    lcd.setCursor(0, 1);
    
    String displaySeq = sequenceData;
    if (displaySeq.length() > 16) {
      displaySeq = displaySeq.substring(0, 13) + "...";
    }
    lcd.print(displaySeq);
    
    Serial.print("Executing sequence with T=");
    Serial.print(tValue);
    Serial.print("ms: ");
    Serial.println(sequenceData);
    
    processNewSequence(sequenceData, tValue);
    
    delay(1000);
    updateDisplay();
  }
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (sequenceCount == 0) {
    lcd.print("No sequences");
    lcd.setCursor(0, 1);
    lcd.print("Add via Serial");
  } else {
    String displayText = sequences[currentSelection];
    int firstComma = displayText.indexOf(',');
    
    if (firstComma != -1) {
      displayText = displayText.substring(0, firstComma);
    }
    
    displayText.trim();
    
    for (int i = 0; i < displayText.length(); i++) {
      if (displayText[i] < 32 || displayText[i] > 126) {
        displayText.setCharAt(i, ' ');
      }
    }
    displayText.trim();
    
    if (displayText.length() > 16) {
      displayText = displayText.substring(0, 16);
    }
    
    lcd.print(displayText);
    
    lcd.setCursor(0, 1);
    lcd.print(">");
    lcd.print(currentSelection + 1);
    lcd.print("/");
    lcd.print(sequenceCount);
    
    int usedSpace = String(currentSelection + 1).length() + String(sequenceCount).length() + 2;
    if (usedSpace <= 10) {
      lcd.print(" T=");
      lcd.print(sequenceTValues[currentSelection]);
      lcd.print("ms");
    }
  }
}

void processNewSequence(String data, int tValue) {
  int start = 0;
  int commaIndex;
  
  while (start < data.length()) {
    commaIndex = data.indexOf(',', start);
    String command;
    
    if (commaIndex == -1) {
      command = data.substring(start);
      start = data.length();
    } else {
      command = data.substring(start, commaIndex);
      start = commaIndex + 1;
    }
    
    command.trim();
    if (command.length() > 0) {
      executeCommand(command, tValue);
    }
  }
  
  delay(GROUP_DELAY_SHORT);
  resetFretServos(-1);
}

void executeCommand(String command, int tValue) {
  int starIndex = command.indexOf('*');
  String action;
  int multiplier = 1;
  
  if (starIndex != -1) {
    action = command.substring(0, starIndex);
    String multiplierStr = command.substring(starIndex + 1);
    multiplier = multiplierStr.toInt();
    if (multiplier <= 0) multiplier = 1;
  } else {
    action = "";
    multiplier = command.toInt();
    if (multiplier <= 0) multiplier = 1;
    
    delay(multiplier * tValue);
    return;
  }
  
  if (action.length() > 0) {
    if (isStrumAction(action)) {
      resetFretServos(-1);
      executeStrumAction(action);
    } else {
      int servoNum = action.toInt();
      if (servoNum >= -7 && servoNum <= 7) {
        int fret = abs(servoNum);
        resetFretServos(fret);
        executeFretAction(servoNum);
      }
    }
    
    delay(multiplier * tValue);
  }
}

bool isStrumAction(String action) {
  if (action.length() != 2) return false;
  if (action[0] != action[1]) return false;
  if (action[0] < '0' || action[0] > '3') return false;
  return true;
}

void executeStrumAction(String action) {
  int strumServo = STRUM_SERVO_START + (action[0] - '0');
  toggleServo(strumServo);
}

void executeFretAction(int servoNum) {
  int fret = abs(servoNum);
  
  if (servoNum >= 0) {
    if (servoPositions[fret] != 1) {
      moveServo(fret, true);
    }
  } else {
    if (servoPositions[fret] != 2) {
      moveServo(fret, false);
    }
  }
  
  int strumServo = STRUM_SERVO_START + (fret % STRUM_SERVO_COUNT);
  toggleServo(strumServo);
}

void resetFretServos(int exceptFret) {
  for (int i = 0; i < FRET_SERVO_COUNT; i++) {
    if (i != exceptFret && servoPositions[i] != 0) {
      setServoAngle(i, 94);
      servoPositions[i] = 0;
    }
  }
}

void moveServo(int servo, bool up) {
  bool reversed = (servo == 2 || servo == 3 || servo == 6 || servo == 7);
  int angle = up ? (reversed ? 120 : 60) : (reversed ? 60 : 120);
  
  setServoAngle(servo, angle);
  servoPositions[servo] = up ? 1 : 2;
}

void toggleServo(int n) {
  if (n < SERVO_MIN || n > SERVO_MAX) return;
  int strumIndex = n - STRUM_SERVO_START;
  int angle = servoStates[n] ? strumDefaultAngles[strumIndex] : strumActiveAngles[strumIndex];
  servoStates[n] = !servoStates[n];
  setServoAngle(n, angle);
}

void setServoAngle(uint8_t n, int angle) {
  int pulseLen = map(angle, 0, 180, 100, 480);
  pwm.setPWM(n, 0, pulseLen);
}