/* 
Connection (module pin - esp32 pin)

LCD display:    RFID RC522:             Keypad:
GND - GND       SDA - D5                1 row - D32
VCC - VIN       SCK - D18               2 row - D33
SDA - D21       MOSI - D23              3 row - D25
SCL - D22       MISO - D19              4 row - D26
                IRQ - NOT CONNECTED     1 column - D27
                GND - GND               2 column - D14
                RST - D2                3 column - D12
                3.3V - 3.3V             4 column - D13
*/

#include <SPI.h>
#include <WiFi.h>
#include <Keypad.h>
#include <MFRC522.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>

#define ROWS 4
#define COLS 4
#define RST_PIN 2
#define SS_PIN 5

char keyMap[ROWS][COLS] = {
  {'1','2','3', 'D'}, // D - in setup mode: Delete last symbol in working mode: ???
  {'4','5','6', 'M'}, // M - Mode(work, setup)
  {'7','8','9', 'H'}, // H - history of payments(card - sum)
  {'*','0','#', 'C'}  // C - copyrights
};

uint8_t rowPins[ROWS] = {32, 33, 25, 26};
uint8_t colPins[COLS] = {27, 14, 12, 13};

Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS );
LiquidCrystal_I2C lcd(0x27,16,2);
MFRC522 rfid(SS_PIN, RST_PIN);

const char* ssid = "TP-LINK_BAFC38";
const char* password = "";

const String APIDomain = "http://192.168.0.104:8000/terminal_api/";
String writingOffMoney = APIDomain + "write_off_money/";

bool isWorkingMode = true;
bool isModeChanged = true;

String amountString = "";

String companyToken = "";
String validSymbolsForAmount = "0123456789";
String validSymbolsForToken = validSymbolsForAmount + "*#";

void wifi_setup() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi with IP Address: ");
  Serial.println(WiFi.localIP());
}

void lcd_work_mode_text() {
  lcd.clear();
  lcd.setCursor(3,0);
  lcd.print("Welcome to");
  lcd.setCursor(3,1);
  lcd.print("EcoReceipt!"); 
}

void lcd_price_set_text(String price) {
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Current price:");
  int cursorPosition = 16 - price.length();
  lcd.setCursor(cursorPosition,1);
  lcd.print(price); 
}

void lcd_setup_mode_text(String token) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Company token");
  lcd.setCursor(0,1);
  lcd.print(":");
  lcd.setCursor(1,1);
  lcd.print(token);
}

void lcd_setup() {
  lcd.init();
  lcd.backlight();
  lcd_work_mode_text();
}

void rfid_setup() {
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522 card
  Serial.println("Terminal started:");
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  rfid.PCD_AntennaOff();
  rfid.PCD_AntennaOn();
}

void setup() {
  Serial.begin(9600);

  // WIFI connecting
  wifi_setup();

  // LCD display setup
  lcd_setup();

  // RFID(card) module setup
  rfid_setup();
}

// TODO: AMOUNT SETUP BY KEYPAD, IF AMOUNT 0, then DO NOT SEND REQUEST
void loop() {
  delay(50);
  // Check if mode button was pressed
  char pressedKey = keypad.getKey();
  if (pressedKey == 'M') {
    isWorkingMode = !isWorkingMode;
    isModeChanged = true;
  } else {
    isModeChanged = false;
  }
  

  if (isWorkingMode) {
    if (isModeChanged) {
      lcd_work_mode_text();
    }
    
    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  
    byte block;
    byte len;
    MFRC522::StatusCode status;
  
    if (pressedKey) {
      if (validSymbolsForAmount.indexOf(pressedKey) >= 0) {
        amountString += pressedKey;
        lcd_price_set_text(amountString);
      } else if (pressedKey == 'D') {
        amountString = amountString.substring(0, amountString.length()-1); 
        lcd_price_set_text(amountString);
      }
    }

    if (amountString.toFloat() <= 0.0) return;
    // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
    if (!rfid.PICC_IsNewCardPresent()) return;
    if (!rfid.PICC_ReadCardSerial()) return;
  
    lcd.setCursor(0,0);
    lcd.clear();
    lcd.print("Waiting for");
    lcd.setCursor(0,1);
    lcd.print("confirmation....");
    
    Serial.println("Card Detected:");
    String cardUID;
    for (byte i = 0; i < rfid.uid.size; i++) {
      cardUID += String(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();

    rfid.PICC_DumpDetailsToSerial(&(rfid.uid)); //dump some details about the card
  
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      Serial.println(writingOffMoney.c_str());
      http.begin(writingOffMoney.c_str());
      http.addHeader("Content-Type", "application/json");
      
      String requestJson = "{\"card_uid\": \"" + cardUID + "\",\"amount\": " + amountString.toFloat() + ", \"company_token\": \"" + companyToken + "\"}";
      Serial.println(requestJson);
      int httpResponseCode = http.POST(requestJson);
  
      lcd.clear();
      lcd.setCursor(0,0);
      if (httpResponseCode > 0) {  // TODO:
        lcd.print("Response success");
        delay(500);
        lcd_work_mode_text(); 
      } else {
        String payload = http.getString();
        lcd.print("Response failure");
        delay(500);
        lcd_work_mode_text(); 
      }
    }

    amountString = "";
     
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    
  } else {
    // Serial.println("Setup mode!");
    if (isModeChanged) {
      lcd_setup_mode_text(companyToken);
    }

    if (pressedKey) {
      if (validSymbolsForToken.indexOf(pressedKey) >= 0) {
      companyToken += pressedKey;
      lcd_setup_mode_text(companyToken);
      } else if (pressedKey == 'D') {
        if (companyToken) {
          companyToken = companyToken.substring(0, companyToken.length()-1); 
          lcd_setup_mode_text(companyToken);
        }
      }
    }
  }
}
