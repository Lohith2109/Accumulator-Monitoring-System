// ====================================================================
// ULM020 Charging Trolley - BMS CAN to Nextion Display (UPDATED)
// BMS->Adafruit Feather M4 CAN->Nextion Display
// ====================================================================

#include <CANSAME5x.h>

CANSAME5x CAN;

// ---------------- CAN Message IDs ----------------
//The IDs match with CAN bus Catalogue in the control system handbook ULM020
#define ID_SOC        0x100
#define ID_THERMAL    0x101
#define ID_PACK       0x102
#define ID_CELL_VOLTS 0x103  // New custom expansion frame for max/min cell metrics

// ---------------- Colour Definitions (Nextion 565 format) ----------------
#define COL_GREEN   2016
#define COL_YELLOW  65504
#define COL_RED     63488
#define COL_WHITE   65535
#define COL_BLACK   0

// ---------------- Picture IDs ----------------
#define PIC_GREEN_CIRCLE  0
#define PIC_YELLOW_CIRCLE 1
#define PIC_RED_CIRCLE    2
#define PIC_BLANK         3  // Used to mask warning frames to look hidden/off
#define PIC_HIGHCURR      4
#define PIC_HIGHVOLT      5
#define PIC_LOWBATT       6
#define PIC_TEMPWARN      7

bool lastFault = false;

// ---------------- CAN timeout ----------------
#define CAN_TIMEOUT 1000   // ms - if no message received, assume comms lost
unsigned long lastCanMsg = 0;

// ---------------- BMS values ----------------
int socPercent   = 0;
int highTempC    = 0;
int avgTempC     = 0;
uint16_t hotCellId = 0;
int packV        = 0;
int packI        = 0;

//  CAN ID 0x103 New Monitoring Functionalities
uint16_t maxCellV = 0;   // Millivolts (mV)
uint16_t minCellV = 0;   // Millivolts (mV)
uint16_t cellDelta = 0;  // Calculated variation: max - min (mV)

// Used for Flashing Warning Icons
unsigned long lastFlashToggle = 0;
bool flashActive = false; // Alternates between true and false every 250ms

// ---------------- Page tracking ----------------
int currentPage = 0; // 0 = home page, 1 = details page

// ================= HMI Cache =================
int lastSOC = -1;
int lastPackV = -9999;
int lastPackI = -9999;
int lastAvgTemp = -9999;
int lastHighTemp = -9999;

String lastChargeState = "";
String lastStatusText = "";

uint16_t lastSOCColour = 65535;
int lastStatusPic = -1;

int lastWarnPic[4] = {-1,-1,-1,-1};

// =============================================================
// Nextion helper functions
// =============================================================

//Terminates every command sent to the Nextion display.
void sendEnd() {
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}

//Updates the text displayed by a Nextion text object
void nextionSetText(const char* obj, const String& txt) {
  Serial1.print(obj);
  Serial1.print(".txt=\"");
  Serial1.print(txt);
  Serial1.print("\"");
  sendEnd();
}

//Updates the value of a numeric Nextion component.
void nextionSetVal(const char* obj, int v) {
  Serial1.print(obj);
  Serial1.print(".val=");
  Serial1.print(v);
  sendEnd();
}

//Changes the text colour of a Nextion component.
void nextionSetColor(const char* obj, uint16_t col) {
  Serial1.print(obj);
  Serial1.print(".pco=");
  Serial1.print(col);
  sendEnd();
}

//Changes the background colour of a component.
void nextionSetBgColor(const char* obj, uint16_t col) {
  Serial1.print(obj);
  Serial1.print(".bco=");
  Serial1.print(col);
  sendEnd();
}

//Changes the image assigned to a Picture component.
void nextionSetPic(const char* obj, int picId) {
  Serial1.print(obj);
  Serial1.print(".pic=");
  Serial1.print(picId);
  sendEnd();
}

//Refreshes a Nextion component after its properties have been modified.
void nextionRef(const char* obj) {
  Serial1.print("ref ");
  Serial1.print(obj);
  sendEnd();
}

//Convenience function that changes an image and immediately refreshes it.
void nextionSetPicAndRef(const char* obj, int picId) {
  nextionSetPic(obj, picId);
  nextionRef(obj);
}

//Monitors the serial interface for page change notifications transmitted by the Nextion display.
void checkNextionEvents() {
  while (Serial1.available() >= 5) {

    if (Serial1.peek() == 0x66) {

      Serial1.read();                 // 0x66
      currentPage = Serial1.read();   // Page ID
      Serial1.read();                 // FF
      Serial1.read();                 // FF
      Serial1.read();                 // FF

      Serial.print("Current Page = ");
      Serial.println(currentPage);

    } else {
      Serial1.read();   // discard unexpected byte
    }
  }
}

//Selects the appropriate battery colour according to the State of Charge.
uint16_t socToColor(int soc) {
  if (soc >= 20) return COL_GREEN;
  if (soc >= 10) return COL_YELLOW;
  return COL_RED;
}

//Reduces unnecessary serial communication.
void updateText(const char* obj, const String& newValue, String &oldValue)
{
  if (newValue != oldValue)
  {
    nextionSetText(obj, newValue);
    oldValue = newValue;
  }
}

//Optimises image updates.
void updatePicture(const char* obj, int newPic, int &oldPic)
{
  if (newPic != oldPic)
  {
    nextionSetPicAndRef(obj, newPic);
    oldPic = newPic;
  }
}

// =============================================================
// Update Home Page (page0)
// =============================================================
/*void updateHomePage()
{
  //=========================================================
  // 1. Check Fault Conditions
  //=========================================================
  bool systemTimeout = (millis() - lastCanMsg) > CAN_TIMEOUT;
  
  bool highTempFault = (highTempC >= 60);
  bool lowSocFault   = (socPercent <= 5);
  bool overCurrent   = (abs(packI) >= 500);

  bool masterFault = highTempFault || lowSocFault || overCurrent || systemTimeout;

  bool highTempWarning = (highTempC >= 50);
  bool highVoltageWarning = (packV >= 590);
  bool highCurrentWarning = (abs(packI) >= 450);
  bool lowSocWarning = (socPercent <= 15);
  bool cellImbalanceWarning = (cellDelta >= 200);

  bool anyWarning = highTempWarning || highVoltageWarning || highCurrentWarning || lowSocWarning || cellImbalanceWarning;

  //=========================================================
  // 2. Critical Fault Screen
  //=========================================================
  Serial.print("masterFault = ");//test
  Serial.println(masterFault);//test
  if(masterFault)
  {

    if(lastFault==false)
    {
      nextionSetPicAndRef("pstatus", PIC_RED_CIRCLE);

      nextionSetText("tStatus","SHUTDOWN - STOP CHARGING");
      nextionSetColor("tStatus",COL_RED);

      nextionSetText("tCharge","CRITICAL FAULT");
      nextionSetColor("tCharge",COL_RED);

      lastFault = true;
    }

    int warnPic;
    //================ Low Battery Warning =================
    if (socPercent <= 15)
    {
      if (flashActive)
        warnPic = PIC_LOWBATT;
      else
        warnPic = PIC_BLANK;
    }
    else
    {
      warnPic = PIC_BLANK;
    }

    if (warnPic != lastWarnPic[0])
    {
      nextionSetPicAndRef("pWarn1", warnPic);
      lastWarnPic[0] = warnPic;
    }

    //================ High Voltage Warning =================
    if (packV >= 590)
    {
      if (flashActive)
        warnPic = PIC_HIGHVOLT;
      else
        warnPic = PIC_BLANK;
    }
    else
    {
      warnPic = PIC_BLANK;
    }

    if (warnPic != lastWarnPic[1])
    {
      nextionSetPicAndRef("pWarn2", warnPic);
      lastWarnPic[1] = warnPic;
    }

    //================ High Current Warning =================
    if (abs(packI) >= 450)
    {
      if (flashActive)
        warnPic = PIC_HIGHCURR;
      else
        warnPic = PIC_BLANK;
    }
    else
    {
      warnPic = PIC_BLANK;
    }

    if (warnPic != lastWarnPic[2])
    {
      nextionSetPicAndRef("pWarn3", warnPic);
      lastWarnPic[2] = warnPic;
    }

    //================ High Temperature Warning =================
    if (highTempC >= 50)
    {
      if (flashActive)
        warnPic = PIC_TEMPWARN;
      else
        warnPic = PIC_BLANK;
    }
    else
    {
      warnPic = PIC_BLANK;
    }

    if (warnPic != lastWarnPic[3])
    {
      nextionSetPicAndRef("pWarn4", warnPic);
      lastWarnPic[3] = warnPic;
    }

    return;
  }
  lastFault = false;

  //=========================================================
  // Updation of SOC(Status bar colour, Battery value) if there is a change
  //=========================================================

  if(lastSOC != socPercent)
  {
    nextionSetText("tSOC",String(socPercent)+"%");
    nextionSetVal("jSOC",socPercent);

    uint16_t c=socToColor(socPercent);

    if(c!=lastSOCColour)
    {
      nextionSetColor("jSOC",c);
      lastSOCColour=c;
    }

    lastSOC=socPercent;
  }

  if(lastPackV!=packV)
  {
    nextionSetText("tVolt",String(packV)+"V");
    lastPackV=packV;
  }

  if(lastPackI!=packI)
  {
    nextionSetText("tCurr",String(packI)+"A");
    lastPackI=packI;
  }

  if(lastAvgTemp!=avgTempC)
  {
    nextionSetText("tAvg",String(avgTempC)+"C");
    lastAvgTemp=avgTempC;
  }

  if(lastHighTemp!=highTempC)
  {
    nextionSetText("tMax",String(highTempC)+"C");
    lastHighTemp=highTempC;
  }

  //=========================================================
  // 4. Battery Operating State (Charging, Discharging & Idle)
  //=========================================================

  String chargeState;
  uint16_t chargeColour;

  if(packI>0)
  {
    chargeState="DISCHARGING";
    chargeColour=COL_YELLOW;
  }
  else if(packI<0)
  {
    chargeState="CHARGING";
    chargeColour=COL_GREEN;
  }
  else
  {
    chargeState="IDLE";
    chargeColour=COL_WHITE;
  }

  if(chargeState!=lastChargeState)
  {
    nextionSetText("tCharge",chargeState);
    nextionSetColor("tCharge",chargeColour);

    lastChargeState=chargeState;
  }

  //=========================================================
  // 5. Warning Icons-Depending on variable flash active, the pictures gets updated
  //=========================================================

  updatePicture("pWarn1",
      (socPercent<=15)?(flashActive?PIC_LOWBATT:PIC_BLANK):PIC_BLANK,
      lastWarnPic[0]);

  updatePicture("pWarn2",
      (packV>=590)?(flashActive?PIC_HIGHVOLT:PIC_BLANK):PIC_BLANK,
      lastWarnPic[1]);

  updatePicture("pWarn3",
      (abs(packI)>=450)?(flashActive?PIC_HIGHCURR:PIC_BLANK):PIC_BLANK,
      lastWarnPic[2]);

  updatePicture("pWarn4",
      (highTempC>=50)?(flashActive?PIC_TEMPWARN:PIC_BLANK):PIC_BLANK,
      lastWarnPic[3]);

  //=========================================================
  // 6. Status- Updating Operational Status (System OK, Warning)
  //=========================================================

  String statusText;
  int statusPic;
  uint16_t statusColour;

  if(anyWarning)
  {
    statusText="WARNING";
    statusPic=PIC_YELLOW_CIRCLE;
    statusColour=COL_YELLOW;
  }
  else
  {
    statusText="SYSTEM OK";
    statusPic=PIC_GREEN_CIRCLE;
    statusColour=COL_GREEN;
  }

  if(statusText!=lastStatusText)
  {
    nextionSetText("tStatus",statusText);
    nextionSetColor("tStatus",statusColour);

    lastStatusText=statusText;
  }

  if(statusPic!=lastStatusPic)
  {
    nextionSetPicAndRef("pstatus",statusPic);
    lastStatusPic=statusPic;
  }
}*/

void updateHomePage()
{
  //=========================================================
  // 1. Evaluate System Conditions
  //=========================================================

  bool systemTimeout = (millis() - lastCanMsg) > CAN_TIMEOUT;

  bool highTempFault = (highTempC >= 60);
  bool lowSocFault   = (socPercent <= 5);
  bool overCurrent   = (abs(packI) >= 500);

  bool masterFault = highTempFault ||
                     lowSocFault   ||
                     overCurrent   ||
                     systemTimeout;

  bool highTempWarning      = (highTempC >= 50);
  bool highVoltageWarning   = (packV >= 590);
  bool highCurrentWarning   = (abs(packI) >= 450);
  bool lowSocWarning        = (socPercent <= 15);
  bool cellImbalanceWarning = (cellDelta >= 200);

  bool anyWarning = highTempWarning      ||
                    highVoltageWarning   ||
                    highCurrentWarning   ||
                    lowSocWarning        ||
                    cellImbalanceWarning;

  //=========================================================
  // 2. Update Telemetry (Always)
  //=========================================================

  if(lastSOC != socPercent)
  {
    nextionSetText("tSOC", String(socPercent) + "%");
    nextionSetVal("jSOC", socPercent);

    uint16_t c = socToColor(socPercent);

    if(c != lastSOCColour)
    {
      nextionSetColor("jSOC", c);
      lastSOCColour = c;
    }

    lastSOC = socPercent;
  }

  if(lastPackV != packV)
  {
    nextionSetText("tVolt", String(packV) + "V");
    lastPackV = packV;
  }

  if(lastPackI != packI)
  {
    nextionSetText("tCurr", String(packI) + "A");
    lastPackI = packI;
  }

  if(lastAvgTemp != avgTempC)
  {
    nextionSetText("tAvg", String(avgTempC) + "C");
    lastAvgTemp = avgTempC;
  }

  if(lastHighTemp != highTempC)
  {
    nextionSetText("tMax", String(highTempC) + "C");
    lastHighTemp = highTempC;
  }

  //=========================================================
  // 3. Update Charge State
  //=========================================================

  String chargeState;
  uint16_t chargeColour;

  if(masterFault)
  {
    chargeState = "CRITICAL FAULT";
    chargeColour = COL_RED;
  }
  else if(packI > 0)
  {
    chargeState = "DISCHARGING";
    chargeColour = COL_YELLOW;
  }
  else if(packI < 0)
  {
    chargeState = "CHARGING";
    chargeColour = COL_GREEN;
  }
  else
  {
    chargeState = "IDLE";
    chargeColour = COL_WHITE;
  }

  if(chargeState != lastChargeState)
  {
    nextionSetText("tCharge", chargeState);
    nextionSetColor("tCharge", chargeColour);

    lastChargeState = chargeState;
  }

  //=========================================================
  // 4. Update Warning Icons
  //=========================================================

  updatePicture("pWarn1",
      lowSocWarning ? (flashActive ? PIC_LOWBATT : PIC_BLANK) : PIC_BLANK,
      lastWarnPic[0]);

  updatePicture("pWarn2",
      highVoltageWarning ? (flashActive ? PIC_HIGHVOLT : PIC_BLANK) : PIC_BLANK,
      lastWarnPic[1]);

  updatePicture("pWarn3",
      highCurrentWarning ? (flashActive ? PIC_HIGHCURR : PIC_BLANK) : PIC_BLANK,
      lastWarnPic[2]);

  updatePicture("pWarn4",
      highTempWarning ? (flashActive ? PIC_TEMPWARN : PIC_BLANK) : PIC_BLANK,
      lastWarnPic[3]);

  //=========================================================
  // 5. Update System Status
  //=========================================================

  String statusText;
  uint16_t statusColour;
  int statusPic;

  if(masterFault)
  {
    statusText = "SHUTDOWN - STOP CHARGING";
    statusColour = COL_RED;
    statusPic = PIC_RED_CIRCLE;
  }
  else if(anyWarning)
  {
    statusText = "WARNING";
    statusColour = COL_YELLOW;
    statusPic = PIC_YELLOW_CIRCLE;
  }
  else
  {
    statusText = "SYSTEM OK";
    statusColour = COL_GREEN;
    statusPic = PIC_GREEN_CIRCLE;
  }

  if(statusText != lastStatusText)
  {
    nextionSetText("tStatus", statusText);
    nextionSetColor("tStatus", statusColour);

    lastStatusText = statusText;
  }

  if(statusPic != lastStatusPic)
  {
    nextionSetPicAndRef("pstatus", statusPic);
    lastStatusPic = statusPic;
  }
}

// =============================================================
// Update Details Page (page1)
// =============================================================
void updateDetailsPage() {
  //Hottest Cell
  nextionSetText("tHotCell", "Hottest Cell: #" + String(hotCellId));

  //Max, Min cell voltage & Cell voltage Imbalance
  nextionSetText("tMaxV", "V_max: " + String((float)maxCellV / 1000.0, 2) + "V");
  nextionSetText("tMinV", "V_min: " + String((float)minCellV / 1000.0, 2) + "V");
  nextionSetText("tDeltaV", "Delta: " + String(cellDelta) + "mV");
  nextionSetColor("tDeltaV", (cellDelta >= 200) ? COL_RED : COL_GREEN);

  bool canOk = (millis() - lastCanMsg) <= CAN_TIMEOUT;

  //Communication Status
  if (canOk) {
    nextionSetText("tCanStatus", "BMS Comms: OK");
    nextionSetColor("tCanStatus", COL_GREEN);
    nextionSetText("tShutdown", "Shutdown Loop: CLOSED");
    nextionSetColor("tShutdown", COL_GREEN);
  } else {
    nextionSetText("tCanStatus", "BMS Comms: LOST");
    nextionSetColor("tCanStatus", COL_RED);
    nextionSetText("tShutdown", "Shutdown Loop: OPEN");
    nextionSetColor("tShutdown", COL_RED);
  }

  //Battery Status
  if (socPercent <= 15) {
    nextionSetText("tLowBatt", "Low Battery: SOC <= 15% - LOW");
    nextionSetColor("tLowBatt", COL_RED);
  } else {
    nextionSetText("tLowBatt", "Low Battery: SOC Normal");
    nextionSetColor("tLowBatt", COL_GREEN);
  }

  //Voltage Status
  if (packV >= 590) {
    nextionSetText("tHighVolt", "High Voltage: " + String(packV) + "V - HIGH");
    nextionSetColor("tHighVolt", COL_RED);
  } else {
    nextionSetText("tHighVolt", "High Voltage: Normal");
    nextionSetColor("tHighVolt", COL_GREEN);
  }

    //Current Status
  if (abs(packI) >= 450) {
    nextionSetText("tHighCurr", "High Current: " + String(packI) + "A - HIGH");
    nextionSetColor("tHighCurr", COL_RED);
  } else {
    nextionSetText("tHighCurr", "High Current: Normal");
    nextionSetColor("tHighCurr", COL_GREEN);
  }

  //Temperature Status
  if (highTempC >= 50) {
    nextionSetText("tOvertemp", "Overtemp: " + String(highTempC) + "C - HOT");
    nextionSetColor("tOvertemp", COL_RED);
  } else {
    nextionSetText("tOvertemp", "Overtemp: Normal");
    nextionSetColor("tOvertemp", COL_GREEN);
  }
}

// =============================================================
// Initial Setup during system startup
// =============================================================
void setup() {
  Serial.begin(115200);    // Arduino IDE Serial Monitor Baud Rate
  Serial1.begin(9600);   // Nextion display baud rate 9600

  pinMode(PIN_CAN_STANDBY, OUTPUT);
  digitalWrite(PIN_CAN_STANDBY, LOW);
  pinMode(PIN_CAN_BOOSTEN, OUTPUT);
  digitalWrite(PIN_CAN_BOOSTEN, HIGH);


  if (!CAN.begin(500000)) {
    Serial.println("CAN init failed!");
    while (1);
  }
  Serial.println("CAN init OK");

  // Initial display setup state configuration
  nextionSetText("tSOC", "--%");
  nextionSetText("tVolt", "--V");
  nextionSetText("tCurr", "--A");
  nextionSetText("tAvg", "--C");
  nextionSetText("tMax", "--C");
  nextionSetText("tCharge", "---");
  nextionSetVal("jSOC", 0);

  nextionSetPicAndRef("pWarn1", PIC_BLANK);
  nextionSetPicAndRef("pWarn2", PIC_BLANK);
  nextionSetPicAndRef("pWarn3", PIC_BLANK);
  nextionSetPicAndRef("pWarn4", PIC_BLANK);

  nextionSetPicAndRef("pstatus", PIC_YELLOW_CIRCLE);
  nextionSetText("tStatus", "WAITING FOR BMS...");
  nextionSetColor("tStatus", COL_YELLOW);
}

// =============================================================
// Main Loop
// =============================================================
void loop() {

  // =========================================================
  // 1. Process Incoming CAN Messages
  // =========================================================
  while (CAN.parsePacket()) {

    uint32_t id = CAN.packetId();
    uint8_t len = CAN.packetDlc();
    uint8_t data[8] = {0};

    for (int i = 0; i < len && i < 8; i++) {
      data[i] = CAN.read();
    }

    // ---------------- DEBUG OUTPUT ----------------
    Serial.println("============= CAN MESSAGE =============");
    Serial.print("CAN ID: 0x");
    Serial.println(id, HEX);

    Serial.print("DLC: ");
    Serial.println(len);

    Serial.print("DATA: ");
    for (int i = 0; i < len; i++) {
      Serial.print("0x");
      if (data[i] < 16) Serial.print("0");
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("*********** DECODED BMS DATA ***********");
    

    lastCanMsg = millis();

    if (id == ID_SOC && len >= 2) {

      uint16_t soc_x10 = (uint16_t)(data[0] | (data[1] << 8));
      socPercent = (int)(soc_x10 / 10);

      if (socPercent > 100)
        socPercent = 100;

      Serial.print("SOC_x10 = ");
      Serial.println(soc_x10);

      Serial.print("SOC = ");
      Serial.print(socPercent);
      Serial.println("%");
    }

    else if (id == ID_THERMAL && len >= 4) {

      highTempC = (int)data[0];
      avgTempC  = (int)data[1];
      hotCellId = (uint16_t)(data[2] | (data[3] << 8));

      Serial.print("High Temp = ");
      Serial.println(highTempC);

      Serial.print("Avg Temp = ");
      Serial.println(avgTempC);
    }

    else if (id == ID_PACK && len >= 4) {

      packV = (int)(uint16_t)(data[0] | (data[1] << 8));
      packI = (int)(uint16_t)(data[2] | (data[3] << 8));

      Serial.print("Pack Voltage (V) = ");
      Serial.println(packV);

      Serial.print("Pack Current(A) = ");
      Serial.println(packI);
    }

    else if (id == ID_CELL_VOLTS && len >= 4) {

      maxCellV = (uint16_t)(data[0] | (data[1] << 8));
      minCellV = (uint16_t)(data[2] | (data[3] << 8));

      if (maxCellV >= minCellV)
        cellDelta = maxCellV - minCellV;

      Serial.print("Max Cell V = ");
      Serial.println(maxCellV);

      Serial.print("Min Cell V = ");
      Serial.println(minCellV);

      Serial.print("Delta = ");
      Serial.println(cellDelta);
    }
  Serial.println("----------- MESSAGE COMPLETE -----------");
  Serial.println();
  }

  // =========================================================
  // 2. Continuous Non-Blocking 2Hz Flash Timer Engine
  // =========================================================
  if (millis() - lastFlashToggle >= 250) {
    flashActive = !flashActive;
    lastFlashToggle = millis();
  }

  // =========================================================
  // 3. Process HMI Navigation Updates
  // =========================================================
  checkNextionEvents();

  // =========================================================
  // 4. Rate-limit Display Transmissions
  // =========================================================
  static unsigned long lastDisplayUpdate = 0;

  if (millis() - lastDisplayUpdate >= 200) {

    lastDisplayUpdate = millis();

    if (currentPage == 0) {
      updateHomePage();
    }
    else if (currentPage == 1) {
      updateDetailsPage();
    }
  }
}