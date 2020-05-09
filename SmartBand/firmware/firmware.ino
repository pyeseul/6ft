#include <SPI.h>
#include <stdint.h>
#include <BLEPeripheral.h>

#include <nrf_nvic.h>//interrupt controller stuff
#include <nrf_sdm.h>
#include <nrf_soc.h>
#include <WInterrupts.h>
#include "Adafruit_GFX.h"
#include "SSD1306.h"
#include <TimeLib.h>
#include <nrf.h>
#include "count_steps.h"
#include "count_steps.c"
#include "i2csoft.h"

#define wdt_reset() NRF_WDT->RR[0] = WDT_RR_RR_Reload
#define wdt_enable(timeout) \
  NRF_WDT->CONFIG = NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) | ( WDT_CONFIG_SLEEP_Pause << WDT_CONFIG_SLEEP_Pos); \
  NRF_WDT->CRV = (32768*timeout)/1000; \
  NRF_WDT->RREN |= WDT_RREN_RR0_Msk;  \
  NRF_WDT->TASKS_START = 1

Adafruit_SSD1306 display(128, 32, &SPI, 28, 4, 29);

boolean debug = true;

// TODO: bring it back to smaller value later (3000ms?)
#define sleepDelay ((unsigned long)-1)
#define BUTTON_PIN              30
#define refreshRate 100

int menu;
volatile bool buttonPressed = false;
long startbutton;
unsigned long sleepTime, displayRefreshTime;
volatile bool sleeping = false;
int timezone;
int steps;
int steps1;
String serialNr = "235246472";
String versionNr = "110.200.051";
String btversionNr = "100.016.051";
String msgText;
boolean gotoBootloader = false;
boolean vibrationMode;

String bleSymbol = " ";
int contrast;

BLEPeripheral                   blePeripheral           = BLEPeripheral();
BLEService                      batteryLevelService     = BLEService("190A");
BLECharacteristic   TXchar        = BLECharacteristic("0002", BLENotify, 20);
BLECharacteristic   RXchar        = BLECharacteristic("0001", BLEWriteWithoutResponse, 20);

BLEService                      batteryLevelService1     = BLEService("190B");
BLECharacteristic   TXchar1        = BLECharacteristic("0004", BLENotify, 20);
BLECharacteristic   RXchar1        = BLECharacteristic("0003", BLEWriteWithoutResponse, 20);

#define N_GRAINS     253 // Number of grains of sand
#define WIDTH        127 // Display width in pixels
#define HEIGHT       32 // Display height in pixels
#define MAX_FPS      150 // Maximum redraw rate, frames/second

// The 'sand' grains exist in an integer coordinate space that's 256X
// the scale of the pixel grid, allowing them to move and interact at
// less than whole-pixel increments.
#define MAX_X (WIDTH  * 256 - 1) // Maximum X coordinate in grain space
#define MAX_Y (HEIGHT * 256 - 1) // Maximum Y coordinate
struct Grain {
  int16_t  x,  y; // Position
  int16_t vx, vy; // Velocity
  uint16_t pos;
} grain[N_GRAINS];

uint32_t        prevTime   = 0;      // Used for frames-per-second throttle
uint16_t         backbuffer = 0, img[WIDTH * HEIGHT]; // Internal 'map' of pixels

//HeartRate Variables
bool fingerin;
byte wert[65];
volatile byte posi = 64;
unsigned long rpmTime;
float reads[4], sum;
long int now1, ptr;
float last, rawRate, start;
float first, second1, third, before, print_value;
bool rising;
int rise_count;
int n;
long int last_beat;
int lastHigh;


#ifdef __cplusplus
extern "C" {
#endif

#define LF_FREQUENCY 32768UL
#define SECONDS(x) ((uint32_t)((LF_FREQUENCY * x) + 0.5))
#define wakeUpSeconds 120
void RTC2_IRQHandler(void)
{
  volatile uint32_t dummy;
  if (NRF_RTC2->EVENTS_COMPARE[0] == 1)
  {
    NRF_RTC2->EVENTS_COMPARE[0] = 0;
    NRF_RTC2->CC[0] = NRF_RTC2->COUNTER +  SECONDS(wakeUpSeconds);
    dummy = NRF_RTC2->EVENTS_COMPARE[0];
    dummy;
    //powerUp();
  }
}

void initRTC2() {

  NVIC_SetPriority(RTC2_IRQn, 15);
  NVIC_ClearPendingIRQ(RTC2_IRQn);
  NVIC_EnableIRQ(RTC2_IRQn);

  NRF_RTC2->PRESCALER = 0;
  NRF_RTC2->CC[0] = SECONDS(wakeUpSeconds);
  NRF_RTC2->INTENSET = RTC_EVTENSET_COMPARE0_Enabled << RTC_EVTENSET_COMPARE0_Pos;
  NRF_RTC2->EVTENSET = RTC_INTENSET_COMPARE0_Enabled << RTC_INTENSET_COMPARE0_Pos;
  NRF_RTC2->TASKS_START = 1;
}
#ifdef __cplusplus
}
#endif

void powerUp() {
  if (sleeping) {
    sleeping = false;
    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay();
    display.display();
    if (debug)Serial.begin(115200);
    delay(5);
  }
  sleepTime = millis();
}

void powerDown() {
  if (!sleeping) {
    if (debug)NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;
    sleeping = true;

  digitalWrite(26, LOW);
    digitalWrite(28, LOW);
    digitalWrite(5, LOW);
    digitalWrite(6, LOW);
    digitalWrite(29, LOW);
    digitalWrite(4, LOW);
    NRF_SAADC ->ENABLE = 0; //disable ADC
    NRF_PWM0  ->ENABLE = 0; //disable all pwm instance
    NRF_PWM1  ->ENABLE = 0;
    NRF_PWM2  ->ENABLE = 0;
  }
}

void charge() {
  if (sleeping)menu = 88;
  powerUp();
}

void buttonHandler() {
  if (!sleeping) buttonPressed = true;
  else menu = 0;
  powerUp();
}

void acclHandler() {
  ReadRegister(0x17);
  if (sleeping) {
    menu = 77;
    powerUp();
  }
}

void blePeripheralConnectHandler(BLECentral& central) {
  if (debug)Serial.println("BLEconnected");
  menu = 0;
  powerUp();
  bleSymbol = "B";
}

void blePeripheralDisconnectHandler(BLECentral& central) {
  if (debug)Serial.println("BLEdisconnected");
  menu = 0;
  powerUp();
  bleSymbol = " ";
}

String answer = "";
String tempCmd = "";
int tempLen = 0, tempLen1;
boolean syn;

void characteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
  char remoteCharArray[21];
  tempLen1 = characteristic.valueLength();
  tempLen = tempLen + tempLen1;
  memset(remoteCharArray, 0, sizeof(remoteCharArray));
  memcpy(remoteCharArray, characteristic.value(), tempLen1);
  tempCmd = tempCmd + remoteCharArray;
  if (tempCmd[tempLen - 2] == '\r' && tempCmd[tempLen - 1] == '\n') {
    answer = tempCmd.substring(0, tempLen - 2);
    tempCmd = "";
    tempLen = 0;
    if (debug)Serial.print("RxBle: ");
    if (debug)Serial.println(answer);
    filterCmd(answer);
  }
}

void filterCmd(String Command) {
  if (Command == "AT+BOND") {
    sendBLEcmd("AT+BOND:OK");
  } else if (Command == "AT+ACT") {
    sendBLEcmd("AT+ACT:0");
  } else if (Command.substring(0, 7) == "BT+UPGB") {
    gotoBootloader = true;
  } else if (Command.substring(0, 8) == "BT+RESET") {
    if (gotoBootloader)NRF_POWER->GPREGRET = 0x01;
    sd_nvic_SystemReset();
  } else if (Command.substring(0, 7) == "AT+RUN=") {
    sendBLEcmd("AT+RUN:" + Command.substring(7));
  } else if (Command.substring(0, 8) == "AT+USER=") {
    sendBLEcmd("AT+USER:" + Command.substring(8));
  }  else if (Command.substring(0, 7) == "AT+REC=") {
    sendBLEcmd("AT+REC:" + Command.substring(7));
  } else if (Command.substring(0, 8) == "AT+PUSH=") {
    sendBLEcmd("AT+PUSH:OK");
    menu = 99;
    powerUp();
    handlePush(Command.substring(8));
  } else if (Command.substring(0, 9) == "AT+MOTOR=") {
    sendBLEcmd("AT+MOTOR:" + Command.substring(9));
  } else if (Command.substring(0, 8) == "AT+DEST=") {
    sendBLEcmd("AT+DEST:" + Command.substring(8));
  } else if (Command.substring(0, 9) == "AT+ALARM=") {
    sendBLEcmd("AT+ALARM:" + Command.substring(9));
  } else if (Command.substring(0, 13) == "AT+HRMONITOR=") {
    sendBLEcmd("AT+HRMONITOR:" + Command.substring(13));
  } else if (Command.substring(0, 13) == "AT+FINDPHONE=") {
    sendBLEcmd("AT+FINDPHONE:" + Command.substring(13));
  } else if (Command.substring(0, 13) == "AT+ANTI_LOST=") {
    sendBLEcmd("AT+ANTI_LOST:" + Command.substring(13));
  } else if (Command.substring(0, 9) == "AT+UNITS=") {
    sendBLEcmd("AT+UNITS:" + Command.substring(9));
  } else if (Command.substring(0, 11) == "AT+HANDSUP=") {
    sendBLEcmd("AT+HANDSUP:" + Command.substring(11));
  } else if (Command.substring(0, 7) == "AT+SIT=") {
    sendBLEcmd("AT+SIT:" + Command.substring(7));
  } else if (Command.substring(0, 7) == "AT+LAN=") {
    sendBLEcmd("AT+LAN:ERR");
  } else if (Command.substring(0, 14) == "AT+TIMEFORMAT=") {
    sendBLEcmd("AT+TIMEFORMAT:" + Command.substring(14));
  } else if (Command == "AT+BATT") {
    sendBLEcmd("AT+BATT:" + String(getBatteryLevel()));
  } else if (Command == "BT+VER") {
    sendBLEcmd("BT+VER:" + btversionNr);
  } else if (Command == "AT+VER") {
    sendBLEcmd("AT+VER:" + versionNr);
  } else if (Command == "AT+SN") {
    sendBLEcmd("AT+SN:" + serialNr);
  } else if (Command.substring(0, 10) == "AT+DISMOD=") {
    sendBLEcmd("AT+DISMOD:" + Command.substring(10));
  } else if (Command.substring(0, 7) == "AT+LAN=") {
    sendBLEcmd("AT+LAN:ERR");
  } else if (Command.substring(0, 10) == "AT+MOTOR=1") {
    sendBLEcmd("AT+MOTOR:1" + Command.substring(10));
    digitalWrite(25, HIGH);
    delay(300);
    digitalWrite(25, LOW);
  } else if (Command.substring(0, 12) == "AT+CONTRAST=") {
    contrast = Command.substring(12).toInt();
  } else if (Command.substring(0, 6) == "AT+DT=") {
    SetDateTimeString(Command.substring(6));
    sendBLEcmd("AT+DT:" + GetDateTimeString());
  } else if (Command.substring(0, 5) == "AT+DT") {
    sendBLEcmd("AT+DT:" + GetDateTimeString());
  } else if (Command.substring(0, 12) == "AT+TIMEZONE=") {
    timezone = Command.substring(12).toInt();
    sendBLEcmd("AT+TIMEZONE:" + String(timezone));
  } else if (Command.substring(0, 11) == "AT+TIMEZONE") {
    sendBLEcmd("AT+TIMEZONE:" + String(timezone));
  } else if (Command == "AT+STEPSTORE") {
    sendBLEcmd("AT+STEPSTORE:OK");
  } else if (Command == "AT+TOPACE=1") {
    sendBLEcmd("AT+TOPACE:OK");
    sendBLEcmd("NT+TOPACE:" + String(steps));
  } else if (Command == "AT+TOPACE=0") {
    sendBLEcmd("AT+TOPACE:" + String(steps));
  } else if (Command == "AT+DATA=0") {
    sendBLEcmd("AT+DATA:0,0,0,0");
  } else if (Command.substring(0, 8) == "AT+PACE=") {
    steps1 = Command.substring(8).toInt();
    sendBLEcmd("AT+PACE:" + String(steps1));
  } else if (Command == "AT+PACE") {
    sendBLEcmd("AT+PACE:" + String(steps1));
  } else if (Command == "AT+DATA=1") {
    sendBLEcmd("AT+DATA:0,0,0,0");
  } else if (Command.substring(0, 7) == "AT+SYN=") {
    if (Command.substring(7) == "1") {
      sendBLEcmd("AT+SYN:1");
      syn = true;
    } else {
      sendBLEcmd("AT+SYN:0");
      syn = false;
    }
  }

}

void sendBLEcmd(String Command) {
  if (debug)Serial.print("TxBle: ");
  if (debug)Serial.println(Command);
  Command = Command + "\r\n";
  while (Command.length() > 0) {
    const char* TempSendCmd;
    String TempCommand = Command.substring(0, 20);
    TempSendCmd = &TempCommand[0];
    TXchar.setValue(TempSendCmd);
    TXchar1.setValue(TempSendCmd);
    Command = Command.substring(20);
  }
}

String GetDateTimeString() {
  String datetime = String(year());
  if (month() < 10) datetime += "0";
  datetime += String(month());
  if (day() < 10) datetime += "0";
  datetime += String(day());
  if (hour() < 10) datetime += "0";
  datetime += String(hour());
  if (minute() < 10) datetime += "0";
  datetime += String(minute());
  return datetime;
}

void SetDateTimeString(String datetime) {
  int year = datetime.substring(0, 4).toInt();
  int month = datetime.substring(4, 6).toInt();
  int day = datetime.substring(6, 8).toInt();
  int hr = datetime.substring(8, 10).toInt();
  int min = datetime.substring(10, 12).toInt();
  int sec = datetime.substring(12, 14).toInt();
  setTime( hr, min, sec, day, month, year);
}

void handlePush(String pushMSG) {
  int commaIndex = pushMSG.indexOf(',');
  int secondCommaIndex = pushMSG.indexOf(',', commaIndex + 1);
  int lastCommaIndex = pushMSG.indexOf(',', secondCommaIndex + 1);
  String MsgText = pushMSG.substring(commaIndex + 1, secondCommaIndex);
  int timeShown = pushMSG.substring(secondCommaIndex + 1, lastCommaIndex).toInt();
  int SymbolNr = pushMSG.substring(lastCommaIndex + 1).toInt();
  msgText = MsgText;
  if (debug)Serial.println("MSGtext: " + msgText);
  if (debug)Serial.println("symbol: " + String(SymbolNr));
}

int getBatteryLevel() {
  return map(analogRead(3), 500, 715, 0, 100);
}

void setup() {
  pinMode(BUTTON_PIN, INPUT);
  pinMode(3, INPUT);
  if (digitalRead(BUTTON_PIN) == LOW) {
    NRF_POWER->GPREGRET = 0x01;
    sd_nvic_SystemReset();
  }
  pinMode(2, INPUT);
  pinMode(26, OUTPUT);
  pinMode(25, OUTPUT);
  digitalWrite(25, HIGH);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  pinMode(15, INPUT);
  if (debug)Serial.begin(115200);
  if (debug)Serial.println("Hello, World!");
  wdt_enable(5000);
  blePeripheral.setLocalName("DS-D6");
  blePeripheral.setAdvertisingInterval(555);
  blePeripheral.setAppearance(0x0000);
  blePeripheral.setConnectable(true);
  blePeripheral.setDeviceName("ATCDSD6");
  blePeripheral.setAdvertisedServiceUuid(batteryLevelService.uuid());
  blePeripheral.addAttribute(batteryLevelService);
  blePeripheral.addAttribute(TXchar);
  blePeripheral.addAttribute(RXchar);
  RXchar.setEventHandler(BLEWritten, characteristicWritten);
  blePeripheral.setAdvertisedServiceUuid(batteryLevelService1.uuid());
  blePeripheral.addAttribute(batteryLevelService1);
  blePeripheral.addAttribute(TXchar1);
  blePeripheral.addAttribute(RXchar1);
  RXchar1.setEventHandler(BLEWritten, characteristicWritten);
  blePeripheral.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  blePeripheral.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
  blePeripheral.begin();
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonHandler, FALLING);
  attachInterrupt(digitalPinToInterrupt(15), acclHandler, RISING);
  NRF_GPIO->PIN_CNF[15] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[15] |= ((uint32_t)GPIO_PIN_CNF_SENSE_High << GPIO_PIN_CNF_SENSE_Pos);
  attachInterrupt(digitalPinToInterrupt(2), charge, RISING);
  NRF_GPIO->PIN_CNF[2] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[2] |= ((uint32_t)GPIO_PIN_CNF_SENSE_High << GPIO_PIN_CNF_SENSE_Pos);
  display.begin(SSD1306_SWITCHCAPVCC);
  delay(100);
  display.clearDisplay();
  // display.setFont(&FreeSerifItalic9pt7b);
  display.display();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 0);
  display.println("D6 Emulator");
  display.display();
  digitalWrite(25, LOW);
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
  initRTC2();
  initi2c();
  initkx023();

  uint8_t i, j, bytes;
  memset(img, 0, sizeof(img)); // Clear the img[] array
  for (i = 0; i < N_GRAINS; i++) { // For each sand grain...
    do {
      grain[i].x = random(WIDTH  * 256); // Assign random position within
      grain[i].y = random(HEIGHT * 256); // the 'grain' coordinate space
      // Check if corresponding pixel position is already occupied...
      for (j = 0; (j < i) && (((grain[i].x / 256) != (grain[j].x / 256)) ||
                              ((grain[i].y / 256) != (grain[j].y / 256))); j++);
    } while (j < i); // Keep retrying until a clear spot is found
    img[(grain[i].y / 256) * WIDTH + (grain[i].x / 256)] = 255; // Mark it
    grain[i].pos = (grain[i].y / 256) * WIDTH + (grain[i].x / 256);
    grain[i].vx = grain[i].vy = 0; // Initial velocity is zero
  }
}

void loop() {
  blePeripheral.poll();
  wdt_reset();
  if (sleeping) {
    sd_app_evt_wait();
    sd_nvic_ClearPendingIRQ(SD_EVT_IRQn);
  } else {

    switch (menu) {
      case 0:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu0();
        }
        break;
      case 1:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu1();
        }
        break;
      case 2:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu2();
        }
        break;
      case 3:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu3();
        }
        break;
      case 4:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu4();
        }
        break;
      case 5:
        displayMenu5();
        break;
      case 6:
  uint8_t buffer[13];
  Pah8001_ReadRawData(buffer);
  if (buffer[0]) {
    if (buffer[8] == 128) {
      rawRate = map(buffer[6], 0, 255, 1023, 0);
      fingerin = true;
    } else fingerin = false;
  }
  if (millis() - rpmTime >= 20) {
    rpmTime = millis();

    sum -= reads[ptr];
    sum += rawRate;
    reads[ptr] = rawRate;
    last = sum / 4;

    if (last > before)
    {
      rise_count++;
      if (!rising && rise_count > 5)
      {
        if (!rising)lastHigh = rawRate;
        rising = true;
        first = millis() - last_beat;
        last_beat = millis();
        print_value = 60000. / (0.4 * first + 0.3 * second1 + 0.3 * third);
        third = second1;
        second1 = first;
      }
    }
    else
    {
      rising = false;
      rise_count = 0;
    }
    before = last;
    ptr++;
    ptr %= 4;
  }
  if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
        displayMenu6();
  }
        break;
      case 77:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu77();
        }
        break;
      case 88:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu88();
        }
        break;
      case 99:
        if (millis() - displayRefreshTime > refreshRate) {
          displayRefreshTime = millis();
          displayMenu99();
        }
        break;
    }
    if (buttonPressed) {
      buttonPressed = false;
      Serial.println(menu);
      switch (menu) {
        case 0:
          menu = 1;
          break;
        case 1:
          menu = 2;
          break;
        case 2:
          menu = 3;
          break;
        case 3:
          menu = 4;
          break;
        case 4:
          startbutton = millis();
          while (!digitalRead(BUTTON_PIN)) {}
          if (millis() - startbutton > 1000) {
            delay(100);
            int err_code = sd_power_gpregret_set(0x01);
            sd_nvic_SystemReset();
            while (1) {};
          } else {
            menu = 5;
          }
          break;
        case 5:
  digitalWrite(26, HIGH);
  delay(50);
  Wire.begin();
  delay(50);
  Pah8001_Configure();
  for (int i = 0; i < 4; i++)reads[i] = 0;
  sum = 0;
  ptr = 0;
          menu = 6;
          break;
        case 6:
    Pah8001_PowerOff();

  digitalWrite(26, LOW);
          menu = 0;
          break;
        case 77:
          menu = 0;
          break;
        case 88:
          menu = 0;
          break;
        case 99:
          digitalWrite(25, LOW);
          menu = 0;
          break;
      }
    }
    switch (menu) {
      case 0:
        if (millis() - sleepTime > sleepDelay ) powerDown();
        break;
      case 1:
        if (millis() - sleepTime > sleepDelay ) powerDown();
        break;
      case 2:
        if (millis() - sleepTime > sleepDelay ) powerDown();
        break;
      case 3:
        if (millis() - sleepTime > sleepDelay ) powerDown();
        break;
      case 4:
        if (millis() - sleepTime > sleepDelay ) powerDown();
        break;
      case 5:
        if (millis() - sleepTime > 20000 ) powerDown();
        break;
      case 6:
        if (millis() - sleepTime > 45000 ) powerDown();
        break;
      case 77:
        if (millis() - sleepTime > 3000 ) powerDown();
        break;
      case 88:
        if (millis() - sleepTime > 3000 ) powerDown();
        break;
      case 99:
        if (millis() - sleepTime > 6000 ) {
          digitalWrite(25, LOW);
          powerDown();
        }
        break;
    }
  }
}

void displayMenu0() {
  display.setRotation(0);

  uint8_t res[6];
  softbeginTransmission(0x1F);
  softwrite(0x06);
  softendTransmission();
  softrequestFrom(0x1F , 6);
  for (int i = 0; i < 6; i++) {
      res[i] = softread();
  }

  float acc[] = { int16_t((res[1] << 8) | res[0]) / 128,
                  int16_t((res[3] << 8) | res[2]) / 128,
                  int16_t((res[5] << 8) | res[4]) / 128 };

  // pitch, roll - source:
  // http://www.hobbytronics.co.uk/accelerometer-info
  float tilt[] = {(atan(acc[0] / sqrt(acc[1]*acc[1] + acc[2]*acc[2]))*180) / M_PI,
                  (atan(acc[1] / sqrt(acc[0]*acc[0] + acc[2]*acc[2]))*180) / M_PI };

  bool risk = tilt[1] < -30;

  // vibrate if we raise hand (simple WIP test)
  if (risk) {
    digitalWrite(25, HIGH); // vibrate
    display_warning1();
    delay(100);

    digitalWrite(25, LOW);  // stop vibration
    display_warning2();
    delay(100);
  } else {
      display.clearDisplay();
      display.display();
  }
}

void display_warning1() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("      X   X   X      ");
  display.println("                     ");
  display.println("      X   X   X      ");
  display.println("      X   X   X      ");
  display.display();
}

void display_warning2() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("XXXXXX XXX XXX XXXXXX");
  display.println("XXXXXXXXXXXXXXXXXXXXX");
  display.println("XXXXXX XXX XXX XXXXXX");
  display.println("XXXXXX XXX XXX XXXXXX");
  display.display();
}

void displayMenu1() {
  display.setRotation(0);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Menue1 Mac:");
  char tmp[16];
  sprintf(tmp, "%04X", NRF_FICR->DEVICEADDR[1] & 0xffff);
  String MyID = tmp;
  sprintf(tmp, "%08X", NRF_FICR->DEVICEADDR[0]);
  MyID += tmp;
  display.println(MyID);
  display.display();
}

void displayMenu2() {
  display.setRotation(0);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(bleSymbol);
  display.println(" Time and Batt:");
  display.println(GetDateTimeString() + String(second()));
  display.print(getBatteryLevel());
  display.print("%  ");
  display.println(analogRead(3));
  display.println(contrast);
  display.display();
}

void displayMenu3() {
  display.setRotation(0);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("CMD Length: ");
  display.println(answer.length());
  display.println(answer);
  display.display();
}
void displayMenu4() {
  display.setRotation(0);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Hello From Media Lab!");
  display.println("  :)");
  display.println("Hold for Bootloader");
  display.display();
}
void displayMenu6() {
 display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Heartrate");
  display.println((int)rawRate);
  if (fingerin) display.print((int)print_value); else display.print("No Finger");

  if (posi <= 63)posi++; else posi = 0;
  wert[posi] = map(rawRate, lastHigh - 30, lastHigh + 10, 0, 32);
  for (int i = 64; i <= 128; i++) {
    display.drawLine( i, 32, i, 32 - wert[i - 64], WHITE);
  }
  display.drawLine( posi + 65, 0, posi + 65, 32, BLACK);
  display.display();
}
void displayMenu77() {
  display.setRotation(3);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(bleSymbol);
  display.setCursor(0, 11);
  display.print(getBatteryLevel());
  display.println("%");

  display.setTextSize(2);
  display.setCursor(4, 30);
  if (hour() < 10) display.print("0");
  display.println(hour());
  display.setCursor(4, 50);
  if (minute() < 10) display.print("0");
  display.println(minute());
  display.setCursor(4, 70);
  if (second() < 10) display.print("0");
  display.println(second());

  display.setCursor(0, 111);
  display.setTextSize(1);
  if (day() < 10) display.print("0");
  display.print(day());
  display.print(".");
  if (month() < 10) display.print("0");
  display.println(month());
  display.print(".");
  display.println(year());

  display.display();
}
void displayMenu88() {
  display.setRotation(3);
  display.clearDisplay();
  display.setCursor(0, 30);
  display.println("Charge");
  display.display();
}
void displayMenu99() {
  display.setRotation(0);
  digitalWrite(25, vibrationMode);
  if (vibrationMode)vibrationMode = false; else vibrationMode = true;
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(msgText);
  display.display();
}
void displayMenu5() {
  uint32_t t;
  while (((t = micros()) - prevTime) < (1000000L / MAX_FPS));
  prevTime = t;

  uint8_t res[6];
  softbeginTransmission(0x1F);
  softwrite(0x06);
  softendTransmission();
  softrequestFrom(0x1F , 6);
  res[0] = softread();
  res[1] = softread();
  res[2] = softread();
  res[3] = softread();
  res[4] = softread();
  res[5] = softread();
  int x = (int16_t)((res[1] << 8) | res[0]);
  int y = (int16_t)((res[3] << 8) | res[2]);
  int z = (int16_t)((res[5] << 8) | res[4]);

  float accelX = y;
  float accelY = -x;
  float accelZ = z;
  int16_t ax = -accelY / 256,      // Transform accelerometer axes
          ay =  accelX / 256,      // to grain coordinate space
          az = abs(accelZ) / 2048; // Random motion factor
  az = (az >= 3) ? 1 : 4 - az;      // Clip & invert
  ax -= az;                         // Subtract motion factor from X, Y
  ay -= az;
  int16_t az2 = az * 2 + 1;         // Range of random motion to add back in

  int32_t v2; // Velocity squared
  float   v;  // Absolute velocity
  for (int i = 0; i < N_GRAINS; i++) {
    grain[i].vx += ax + random(az2); // A little randomness makes
    grain[i].vy += ay + random(az2); // tall stacks topple better!
    v2 = (int32_t)grain[i].vx * grain[i].vx + (int32_t)grain[i].vy * grain[i].vy;
    if (v2 > 65536) { // If v^2 > 65536, then v > 256
      v = sqrt((float)v2); // Velocity vector magnitude
      grain[i].vx = (int)(256.0 * (float)grain[i].vx / v); // Maintain heading
      grain[i].vy = (int)(256.0 * (float)grain[i].vy / v); // Limit magnitude
    }
  }

  uint16_t        i, bytes, oldidx, newidx, delta;
  int16_t        newx, newy;

  for (i = 0; i < N_GRAINS; i++) {
    newx = grain[i].x + grain[i].vx; // New position in grain space
    newy = grain[i].y + grain[i].vy;
    if (newx > MAX_X) {              // If grain would go out of bounds
      newx         = MAX_X;          // keep it inside, and
      grain[i].vx /= -2;             // give a slight bounce off the wall
    } else if (newx < 0) {
      newx         = 0;
      grain[i].vx /= -2;
    }
    if (newy > MAX_Y) {
      newy         = MAX_Y;
      grain[i].vy /= -2;
    } else if (newy < 0) {
      newy         = 0;
      grain[i].vy /= -2;
    }

    oldidx = (grain[i].y / 256) * WIDTH + (grain[i].x / 256); // Prior pixel #
    newidx = (newy      / 256) * WIDTH + (newx      / 256); // New pixel #
    if ((oldidx != newidx) && // If grain is moving to a new pixel...
        img[newidx]) {       // but if that pixel is already occupied...
      delta = abs(newidx - oldidx); // What direction when blocked?
      if (delta == 1) {           // 1 pixel left or right)
        newx         = grain[i].x;  // Cancel X motion
        grain[i].vx /= -2;          // and bounce X velocity (Y is OK)
        newidx       = oldidx;      // No pixel change
      } else if (delta == WIDTH) { // 1 pixel up or down
        newy         = grain[i].y;  // Cancel Y motion
        grain[i].vy /= -2;          // and bounce Y velocity (X is OK)
        newidx       = oldidx;      // No pixel change
      } else { // Diagonal intersection is more tricky...
        if ((abs(grain[i].vx) - abs(grain[i].vy)) >= 0) { // X axis is faster
          newidx = (grain[i].y / 256) * WIDTH + (newx / 256);
          if (!img[newidx]) { // That pixel's free!  Take it!  But...
            newy         = grain[i].y; // Cancel Y motion
            grain[i].vy /= -2;         // and bounce Y velocity
          } else { // X pixel is taken, so try Y...
            newidx = (newy / 256) * WIDTH + (grain[i].x / 256);
            if (!img[newidx]) { // Pixel is free, take it, but first...
              newx         = grain[i].x; // Cancel X motion
              grain[i].vx /= -2;         // and bounce X velocity
            } else { // Both spots are occupied
              newx         = grain[i].x; // Cancel X & Y motion
              newy         = grain[i].y;
              grain[i].vx /= -2;         // Bounce X & Y velocity
              grain[i].vy /= -2;
              newidx       = oldidx;     // Not moving
            }
          }
        } else { // Y axis is faster, start there
          newidx = (newy / 256) * WIDTH + (grain[i].x / 256);
          if (!img[newidx]) { // Pixel's free!  Take it!  But...
            newx         = grain[i].x; // Cancel X motion
            grain[i].vy /= -2;         // and bounce X velocity
          } else { // Y pixel is taken, so try X...
            newidx = (grain[i].y / 256) * WIDTH + (newx / 256);
            if (!img[newidx]) { // Pixel is free, take it, but first...
              newy         = grain[i].y; // Cancel Y motion
              grain[i].vy /= -2;         // and bounce Y velocity
            } else { // Both spots are occupied
              newx         = grain[i].x; // Cancel X & Y motion
              newy         = grain[i].y;
              grain[i].vx /= -2;         // Bounce X & Y velocity
              grain[i].vy /= -2;
              newidx       = oldidx;     // Not moving
            }
          }
        }
      }
    }
    grain[i].x  = newx; // Update grain position
    grain[i].y  = newy;
    img[oldidx] = 0;    // Clear old spot (might be same as new, that's OK)
    img[newidx] = 255;  // Set new spot
    grain[i].pos = newidx;
  }

  display.clearDisplay();
  for (i = 0; i < N_GRAINS; i++) {
    int yPos = grain[i].pos / WIDTH;
    int xPos = grain[i].pos % WIDTH;
    display.drawPixel(xPos , yPos, WHITE);
  }
  display.display();
}


void writeRegister(uint8_t addr, uint8_t data)
{
  Wire.beginTransmission(0x6b);
  Wire.write(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t readRegister(uint8_t addr)
{
  Wire.beginTransmission(0x6b);
  Wire.write(addr);
  Wire.endTransmission();
  Wire.requestFrom(0x6b, 1);
  return Wire.read();
}

static bool ppg_current_change = false;

#define PAH8001_LED_STEP_DELTA 2
#define PAH8001_LED_EXPOSURE_MAX 496
#define PAH8001_LED_EXPOSURE_MIN 32
#define PAH8001_LED_EXPOSURE_BIG 420
#define PAH8001_LED_EXPOSURE_SML 64
#define PAH8001_LED_STEP_MAX 31
#define PAH8001_LED_STEP_MIN 1

static const struct {
  uint8_t reg;
  uint8_t value;
} config[] =
{
  { 0x27u, 0xFFu },
  { 0x28u, 0xFAu },
  { 0x29u, 0x0Au },
  { 0x2Au, 0xC8u },
  { 0x2Bu, 0xA0u },
  { 0x2Cu, 0x8Cu },
  { 0x2Du, 0x64u },
  { 0x42u, 0x20u },
  { 0x48u, 0x00u },
  { 0x4Du, 0x1Au },
  { 0x7Au, 0xB5u },
  { 0x7Fu, 0x01u },
  { 0x07u, 0x48u },
  { 0x23u, 0x40u },
  { 0x26u, 0x0Fu },
  { 0x2Eu, 0x48u },
  { 0x38u, 0xEAu },
  { 0x42u, 0xA4u },
  { 0x43u, 0x41u },
  { 0x44u, 0x41u },
  { 0x45u, 0x24u },
  { 0x46u, 0xC0u },
  { 0x52u, 0x32u },
  { 0x53u, 0x28u },
  { 0x56u, 0x60u },
  { 0x57u, 0x28u },
  { 0x6Du, 0x02u },
  { 0x0Fu, 0xC8u },
  { 0x7Fu, 0x00u },
  { 0x5Du, 0x81u }
};

static bool Pah8001_Configure()
{
  uint8_t value;

  writeRegister(0x06u, 0x82u);
  delay(10);
  writeRegister(0x09u, 0x5Au        );
  writeRegister(0x05u, 0x99u        );
  value = readRegister (0x17u);
  writeRegister(0x17u, value | 0x80 );

  for (size_t i = 0; i < sizeof(config) / sizeof(config[0]); i++)
  {
    writeRegister(config[i].reg, config[i].value);
  }

  value = readRegister(0x00);

  return true;
}

static bool Pah8001_UpdateLed(bool touch)
{
  static bool ppg_sleep = true;
  static uint8_t ppg_states = 0;
  static uint8_t ppg_led_mode = 0;
  static uint8_t ppg_led_step = 10;

  if (!touch)
  {
    writeRegister(0x7Fu, 0x00u);
    writeRegister(0x05u, 0xB8u);
    writeRegister(0x7Fu, 0x01u);
    writeRegister(0x42u, 0xA0u);
    writeRegister(0x38u, 0xE5u);

    ppg_led_step = 10;
    ppg_sleep = true;
    ppg_current_change = false;
  }
  else
  {
    uint8_t value;
    uint16_t exposureTime;
    writeRegister(0x7Fu, 0x00u);
    writeRegister(0x05u, 0x98u);
    writeRegister(0x7Fu, 0x01u);
    writeRegister(0x42u, 0xA4u);
    writeRegister(0x7Fu, 0x00u);

    // Read exposure time
    value = readRegister(0x33);
    exposureTime = (value & 0x3u) << 8;
    value = readRegister(0x32);
    exposureTime |= value;

    writeRegister(0x7Fu, 0x01u);
    if (ppg_sleep)
    {
      writeRegister(0x38u, (0xE0u | 10));
      ppg_sleep = false;
    }

    if (ppg_states++ > 3)
    {
      ppg_states = 0;
      if (ppg_led_mode == 0)
      {
        if (exposureTime >= PAH8001_LED_EXPOSURE_MAX
            || exposureTime <= PAH8001_LED_EXPOSURE_MIN)
        {
          ppg_led_step = readRegister(0x38u);
          ppg_led_step &= 0x1Fu;

          if (exposureTime >= PAH8001_LED_EXPOSURE_MAX &&
              ppg_led_step < PAH8001_LED_STEP_MAX)
          {
            ppg_led_mode = 1;
            if (ppg_led_step += PAH8001_LED_STEP_DELTA > PAH8001_LED_STEP_MAX) {
              ppg_led_step = PAH8001_LED_STEP_MAX;
            }
            writeRegister(0x38u, ppg_led_step | 0xE0u);
            ppg_current_change = true;
          }
          else if (exposureTime <= PAH8001_LED_EXPOSURE_MIN &&
                   ppg_led_step > PAH8001_LED_STEP_MIN)
          {
            ppg_led_mode = 2;
            if (ppg_led_step <= PAH8001_LED_STEP_MIN + PAH8001_LED_STEP_DELTA) {
              ppg_led_step = PAH8001_LED_STEP_MIN;
            }
            else ppg_led_step -= PAH8001_LED_STEP_DELTA;

            writeRegister(0x38u, ppg_led_step | 0xE0u);
            ppg_current_change = true;
          }
          else
          {
            ppg_led_mode = 0;
            ppg_current_change = false;
          }
        }
        else ppg_current_change = false;
      }
      else if (ppg_led_mode == 1)
      {
        if (exposureTime > PAH8001_LED_EXPOSURE_BIG)
        {
          if (ppg_led_step += PAH8001_LED_STEP_DELTA > PAH8001_LED_STEP_MAX)
          {
            ppg_led_mode = 0;
            ppg_led_step = PAH8001_LED_STEP_MAX;
          }
          writeRegister(0x38u, ppg_led_step | 0xE0u);
          ppg_current_change = true;
        }
        else
        {
          ppg_led_mode = 0;
          ppg_current_change = false;
        }
      }
      else
      {
        if (exposureTime < PAH8001_LED_EXPOSURE_SML)
        {
          ppg_led_mode = 2;
          if (ppg_led_step <= PAH8001_LED_STEP_MIN + PAH8001_LED_STEP_DELTA)
          {
            ppg_led_mode = 0;
            ppg_led_step = PAH8001_LED_STEP_MIN;
          }
          else ppg_led_step -= PAH8001_LED_STEP_DELTA;

          writeRegister(0x38u, ppg_led_step | 0xE0u);
          ppg_current_change = true;
        }
        else
        {
          ppg_led_mode = 0;
          ppg_current_change = false;
        }
      }
    }
    else {
      ppg_current_change = false;
    }
  }
  return true;
}

uint8_t Pah8001_ReadRawData(uint8_t buffer[9])
{
  static uint8_t ppg_frame_count = 0, touch_cnt = 0;
  uint8_t value;
  writeRegister(0x7Fu, 0x00u);
  value = readRegister(0x59u);
  if (value == 0x80)
    touch_cnt = 0;
  if (touch_cnt++ < 250)
    if (!Pah8001_UpdateLed(1)) return 0x13;
    else
    {
      if (!Pah8001_UpdateLed(0)) return 0x13;
      touch_cnt = 252;
    }
  writeRegister(0x7Fu, 0x01u);

  value = readRegister(0x68u);
  buffer[0] = value & 0xFu;


  if (buffer[0] != 0) //0 means no data, 1~15 mean have data
  {
    uint8_t tmp[4];
    /* 0x7f is change bank register,
      0x64~0x67 is HR_DATA
      0x1a~0x1C is HR_DATA_Algo
    */

    Wire.beginTransmission(0x6b);
    Wire.write(0x64);
    Wire.endTransmission();
    Wire.requestFrom(0x6b, 4);

    buffer[1] = Wire.read() & 0xFFu;
    buffer[2] = Wire.read() & 0xFFu;
    buffer[3] = Wire.read() & 0xFFu;
    buffer[4] = Wire.read() & 0xFFu;

    Wire.beginTransmission(0x6b);
    Wire.write(0x1a);
    Wire.endTransmission();
    Wire.requestFrom(0x6b, 4);

    buffer[5] = Wire.read() & 0xFFu;
    buffer[6] = Wire.read() & 0xFFu;
    buffer[7] = Wire.read() & 0xFFu;

    writeRegister(0x7Fu, 0x00u);
    value = readRegister(0x59u);
    buffer[8] = value & 0x80u;
  }
  else
  {
    writeRegister(0x7Fu, 0x00u);
    return 0x22;
  }
  return 0;
}


bool Pah8001_HRValid(void)
{
  uint8_t value;
  value = readRegister(0x59u);
  return value & 0x80u == 0x80u;
}

bool Pah8001_PowerOff(void)
{
  writeRegister(0x7Fu, 0x00u);
  writeRegister(0x06u, 0x0Au);
  return true;
}

bool Pah8001_PowerOn(void)
{
  writeRegister(0x7Fu, 0x00u);
  writeRegister(0x06u, 0x02u);
  writeRegister(0x05u, 0x99u);
}
