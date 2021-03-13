/* Add dependencies */
#include <AWS_IOT.h>
#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

/* Add constant */
#define RST_PIN         4
#define SS_PIN          5
#define LED_PIN         2
#define PUSH_BUTTON_1   25
#define PUSH_BUTTON_2   26
#define RESET_BUTTON    0
#define SIZE_BUFFER     18
#define MAX_SIZE_BLOCK  16

/* Add network configuration */
char WIFI_SSID[]="****ssid****";
char WIFI_PASSWORD[]="****password****";

/* Add AWS configuration */
char HOST_ADDRESS[]="*********.iot.us-east-1.amazonaws.com";
char CLIENT_ID[]= "Thingamajig";
char PUB_TOPIC_NAME[]= "Thingamajig/Order";
char SUB_TOPIC_NAME[]= "Thingamajig/Confirmation";

AWS_IOT hornbill;
int connectionStatus = WL_IDLE_STATUS;
int msgReceived = 0;
char sendPayload[512];
char rcvdPayload[512];

/* Add lcd display configuration */
const int lcdColumns = 16;
const int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

/* Add product variables */
int product1Count = 0;
int product2Count = 0;
int orderStatus = 0;
int dot = 0;
String userId = "";

/* Add rfid reader variables */
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode rfidStatus;

void setup() {
  setupSerial();
  setupLcdDisplay();
  connectToWifi();
  connectToAWS();
  setupRfidReader();
  setupDeviceLed();
  setupPushButtons();
  showWelcomeMessage();
}

void loop() {
  int pushButton1State = digitalRead(PUSH_BUTTON_1);
  int pushButton2State = digitalRead(PUSH_BUTTON_2);
  int resetButtonState = digitalRead(RESET_BUTTON);

  if (orderStatus == 1) {
    blinkingLed();
    if(msgReceived == 1) {
      msgReceived = 0;
      checkConfirmationMessage();
    }
  } else if (pushButton1State == HIGH) {
    product1Count++;
    setProductMessage();
    delay(500);
  } else if (pushButton2State == HIGH) {
    product2Count++;
    setProductMessage();
    delay(500);
  } else if (resetButtonState == LOW) {
    resetProducts();
  } else {
    checkRfidAndPlaceOrder();
  }
}

void setupSerial() {
  Serial.begin(115200);
}

void setupLcdDisplay() {
  lcd.init();                    
  lcd.backlight();
}

void connectToWifi() {
  // Disconnect from any Wifi connection
  WiFi.disconnect(true);

  // Connecting to Wifi
  while (connectionStatus != WL_CONNECTED) {
    String wifiText = String("Wifi");
    for (int i = 0; i < dot; i++) { wifiText += "."; }
    setMessage("Connecting to", wifiText, 0);
    Serial.print("Attempting to connect to Wifi network: "); Serial.println(WIFI_SSID);
    connectionStatus = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(5000);
    dot++;
  }

  // Connected to Wifi
  Serial.println("Connected to Wifi!");
  setMessage("Connected to", "Wifi!", 2000);
  dot = 0;
}

void connectToAWS() {
  if(hornbill.connect(HOST_ADDRESS,CLIENT_ID) == 0) {
    setMessage("Connecting to", "AWS", 1500);
    if (hornbill.subscribe(SUB_TOPIC_NAME, confirmationCallbackHandler) == 0) {
      Serial.println("Connected to AWS, bru");
      setMessage("Connected to", "AWS", 2000);
    } else {
      setMessage("AWS connection", "failed", 0);
      Serial.println("AWS connection failed, Check the HOST Address");
      while(1);
    }
  }
  else {
    setMessage("AWS connection", "failed", 0);
    Serial.println("AWS connection failed, Check the HOST Address");
    while(1);
  }
}

void setupRfidReader() {
  setMessage("Setting up", "RFID reader", 1500);
  while (!Serial);
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);
  mfrc522.PCD_DumpVersionToSerial();
  setMessage("RFID reader", "setup done", 2000);
}

void setupDeviceLed() {
  pinMode(LED_PIN, OUTPUT);
}

void setupPushButtons() {
  pinMode(PUSH_BUTTON_1, INPUT);
  pinMode(PUSH_BUTTON_2, INPUT);
  pinMode(RESET_BUTTON, INPUT);
}

void showWelcomeMessage() {
  setMessage("Device setup", "successful!", 2000);
  setMessage("Let's start", "shopping!", 2000);
  setProductMessage();
}

void confirmationCallbackHandler(char *topicName, int payloadLen, char *payLoad) {
  strncpy(rcvdPayload,payLoad,payloadLen);
  rcvdPayload[payloadLen] = 0;
  msgReceived = 1;
}

void setMessage(String row1Text, String row2Text, int delayMs) {
  lcd.clear();
  if (row1Text.length() > 0) {
    lcd.setCursor(0, 0);
    lcd.print(row1Text);
  }
  if (row2Text.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(row2Text);
  }
  delay(delayMs);
}

void setProductMessage() {
  String product1Message = String("Product 1: ") + String(product1Count);
  String product2Message = String("Product 2: ") + String(product2Count);
  setMessage(product1Message, product2Message, 500);
}

void resetProducts() {
  product1Count = 0;
  product2Count = 0;
  setProductMessage();
}

void blinkingLed() {
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}

void checkRfidAndPlaceOrder() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  setMessage("Reading", "user card", 1000);
  if (!mfrc522.PICC_ReadCardSerial()) {
    setMessage("Can't read", "user card", 1000);
    setProductMessage();
    return;
  }

  userId = getUid();
  setMessage("User ID :", userId, 1000);

  sendData();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

String getUid() {
  // prints the technical details of the card/tag
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));
  
  //prepare the key - all keys are set to FFFFFFFFFFFFh
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  
  //buffer for read data
  byte buffer[SIZE_BUFFER] = {0};
 
  //the block to operate
  byte block = 1;
  byte size = SIZE_BUFFER;
  
  //authenticates the block to operate
  rfidStatus = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (rfidStatus != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(rfidStatus));
    return "";
  }

  //read data from block
  rfidStatus = mfrc522.MIFARE_Read(block, buffer, &size);
  if (rfidStatus != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(rfidStatus));
    return "";
  }

  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
    uid.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  uid.toUpperCase();
  return uid;
}

void sendData() {
  sprintf(sendPayload,"{\"state\":{\"reported\":{\"userId\":\"%s\", \"product1\":\"%d\", \"product2\":\"%d\"}}}", userId, product1Count, product2Count);
  Serial.println(sendPayload);
  setMessage("Placing order", "", 1000);
  if(hornbill.publish(PUB_TOPIC_NAME,sendPayload) == 0) {
    Serial.println("Message published successfully");
    setMessage("Placing order", "successful", 2000);
    orderStatus = 1;
    setMessage("Waiting", "confirmation", 500);
  }
  else {
    Serial.println("Message was not published");
    setMessage("Placing order", "failed", 1000);
    delay(5000);
    userId = "";
    setProductMessage();
  }
}

void checkConfirmationMessage() {
  String json = cleanJson(rcvdPayload);
  StaticJsonDocument<200> doc;
  deserializeJson(doc, json);

  String trxUserId = doc["userId"];
  String orderConfirmation = doc["orderConfirmation"];
  
  if (trxUserId.equals(userId)) {
    orderStatus = 0;
    String orderConfirmation = doc["orderConfirmation"];
    digitalWrite(LED_PIN, HIGH);
    if (orderConfirmation.equalsIgnoreCase(String("accepted"))) {
      setMessage("Order accepted!", "", 1000);
      setMessage("Order accepted!", "Thank you.", 2000);
    } else {
      setMessage("Order rejected!", "", 1000);
      setMessage("Order rejected!", "Sorry.", 2000);
    }
    digitalWrite(LED_PIN, LOW);
    userId = "";
    resetProducts();
  }
}

String cleanJson(char *text) {
  String json = "";
  for (int i = 0; i < strlen(text) - 1; i++) {
    if (i != 0 && text[i] != '\\' && text[i+1] != '\0') {
      json += text[i];
    }
  }
  return json;
}
