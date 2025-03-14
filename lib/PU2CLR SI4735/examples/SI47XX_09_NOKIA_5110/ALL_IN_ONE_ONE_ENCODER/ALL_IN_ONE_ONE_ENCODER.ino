/*
  See user_manual.txt before operating the receiver.
  
  This sketch uses the display Nokia 5110 compatible device with a very lightweight library to control it called LCD5110_Graph. 
  To install and to know more about the Nokia 5110 library used here see: http://www.rinkydinkelectronics.com/library.php?id=47
  
  If you are using a SI4735-D60 or SI4732-A10, you can also use this sketch to add the SSB functionalities to the
  original Pavleski's project. If you are using another SI4730-D60, the SSB wil not work. But you will still have
  the SW functionalities.

  It is  a  complete  radio  capable  to  tune  LW,  MW,  SW  on  AM  and  SSB  mode  and  also  receive  the
  regular  comercial  stations.

  Features:   EEPROM save and restore of receiver status; AM; SSB; LW/MW/SW; external mute circuit control; AGC; Attenuation gain control;
              SSB filter; CW; AM filter; 1, 5, 10, 50 and 500kHz step on AM and 10Hhz sep on SSB

  Wire up on Arduino UNO, Pro mini and SI4735-D60

  | Device name               | Device Pin / Description      |  Arduino Pin  |
  | ----------------          | ----------------------------- | ------------  |
  | Display NOKIA 5110        |                               |               |
  |                           | (1) RST (RESET)               |     8         |
  |                           | (2) CE or CS                  |     9         |
  |                           | (3) DC or DO                  |    10         |
  |                           | (4) DIN or DI or MOSI         |    11         |
  |                           | (5) CLK                       |    13         |
  |                           | (6) VCC  (3V-5V)              |    +VCC       |
  |                           | (7) BL/DL/LIGHT               |    +VCC       |
  |                           | (8) GND                       |    GND        |
  |     Si4735                |                               |               |
  |                           | (*3) RESET (pin 15)           |     16/A2     |
  |                           | (*3) SDIO (pin 18)            |     A4        |
  |                           | (*3) SCLK (pin 17)            |     A5        |
  |                           | (*4) SEN (pin 16)             |    GND        |
  |    Buttons                |                               |               |
  |                           | Encoder push button           |     A0/14     |  
  |    Encoder                |                               |               |
  |                           | A                             |       2       |
  |                           | B                             |       3       |

  (*1) You have to press the push button and after, rotate the encoder to select the parameter.
       After you activate a command by pressing a push button, it will keep active for 2,5 seconds.
  (*2) The SEEK direction is based on the last movement of the encoder. If the last movement of
       the encoder was clockwise, the SEEK will be towards the upper limit. If the last movement of
       the encoder was counterclockwise, the SEEK direction will be towards the lower limit.
  (*3) - If you are using the SI4732-A10, check the corresponding pin numbers.
  (*4) - If you are using the SI4735-D60, connect the SEN pin to the ground;
         If you are using the SI4732-A10, connect the SEN pin to the +Vcc.

  By PU2CLR, Ricardo, FEB  2022.
*/

#include <SI4735.h>
#include <EEPROM.h>
#include <LCD5110_Graph.h> // you can download this library on http://www.rinkydinkelectronics.com/library.php?id=47

#include "Rotary.h"
#include <patch_ssb_compressed.h>    // Compressed SSB patch version (saving almost 1KB)

const uint16_t size_content = sizeof ssb_patch_content; // See ssb_patch_content.h
const uint16_t cmd_0x15_size = sizeof cmd_0x15;         // Array of lines where the 0x15 command occurs in the patch content.

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

#define RESET_PIN 16 // Arduino pin A2

// Enconder PINs
#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 3

// NOKIA Display pin setup
#define NOKIA_RST   8    // RESET
#define NOKIA_CE    9    // Some NOKIA devices show CS
#define NOKIA_DC   10    //
#define NOKIA_DIN  11    // MOSI
#define NOKIA_CLK  13    // SCK
#define NOKIA_LED   0    // 0 if wired to +3.3V directly

// Buttons controllers
#define ENCODER_PUSH_BUTTON 14 // Pin A0/14

#define MIN_ELAPSED_TIME 300
#define MIN_ELAPSED_RSSI_TIME 500
#define ELAPSED_COMMAND 2000 // time to turn off the last command controlled by encoder. Time to goes back to the FVO control
#define ELAPSED_CLICK 1500   // time to check the double click commands
#define DEFAULT_VOLUME 45    // change it for your favorite sound volume

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define SSB 1

#define EEPROM_SIZE        512

#define STORE_TIME 10000 // Time of inactivity to make the current receiver status writable (10s / 10000 milliseconds).


/*
 * Signal Icons (Antenna Icon + RSSI level representation) 
 */
const uint8_t signalLevel[5][17] PROGMEM= {
                                            {0xC1, 0xC3, 0xC5, 0xF9, 0xC5, 0xC3, 0xC1, 0xC0, 0xE0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0}, // Level 0 => rssi < 12
                                            {0xC1, 0xC3, 0xC5, 0xF9, 0xC5, 0xC3, 0xC1, 0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0}, // Level 1 => rssi < 25
                                            {0xC1, 0xC3, 0xC5, 0xF9, 0xC5, 0xC3, 0xC1, 0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0xC0}, // Level 2 => rssi < 45
                                            {0xC1, 0xC3, 0xC5, 0xF9, 0xC5, 0xC3, 0xC1, 0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0xF8, 0xC0, 0xFC, 0xC0, 0xC0}, // Level 3 => rssi < 60 
                                            {0xC1, 0xC3, 0xC5, 0xF9, 0xC5, 0xC3, 0xC1, 0xC0, 0xE0, 0xC0, 0xF0, 0xC0, 0xF8, 0xC0, 0xFC, 0xC0, 0xFE}  // Level 4 => rssi >= 60
                                          };

// EEPROM - Stroring control variables
const uint8_t app_id = 31; // Useful to check the EEPROM content before processing useful data
const int eeprom_address = 0;
long storeTime = millis();

bool itIsTimeToSave = false;

bool bfoOn = false;
bool ssbLoaded = false;

int8_t agcIdx = 0;
uint8_t disableAgc = 0;
int8_t agcNdx = 0;
int8_t softMuteMaxAttIdx = 4;
int8_t avcIdx;   // min 12 and max 90 
uint8_t countClick = 0;

uint8_t seekDirection = 1;

bool cmdBand = false;
bool cmdVolume = false;
bool cmdAgc = false;
bool cmdBandwidth = false;
bool cmdStep = false;
bool cmdMode = false;
bool cmdMenu = false;
bool cmdSoftMuteMaxAtt = false;
bool cmdAvc = false;

int16_t currentBFO = 0;
long elapsedRSSI = millis();
long elapsedButton = millis();
long elapsedCommand = millis();
long elapsedClick = millis();
volatile int encoderCount = 0;
uint16_t currentFrequency;
uint16_t previousFrequency = 0;

const uint8_t currentBFOStep = 10;

const char * menu[] = {"Volume", "Step", "Mode", "BFO", "BW", "AGC/Att", "SoftMute", "AVC", "Seek Up", "Seek Down"};
int8_t menuIdx = 0;
const int lastMenu = 9;
int8_t currentMenuCmd = -1;

typedef struct
{
  uint8_t idx;      // SI473X device bandwidth index
  const char *desc; // bandwidth description
} Bandwidth;

int8_t bwIdxSSB = 4;
const int8_t maxSsbBw = 5;
Bandwidth bandwidthSSB[] = {
  {4, "0.5"},
  {5, "1.0"},
  {0, "1.2"},
  {1, "2.2"},
  {2, "3.0"},
  {3, "4.0"}
};

int8_t bwIdxAM = 4;
const int8_t maxAmBw = 6;
Bandwidth bandwidthAM[] = {
  {4, "1.0"},
  {5, "1.8"},
  {3, "2.0"},
  {6, "2.5"},
  {2, "3.0"},
  {1, "4.0"},
  {0, "6.0"}
};

int8_t bwIdxFM = 0;
const int8_t maxFmBw = 4;

Bandwidth bandwidthFM[] = {
    {0, "AUT"}, // Automatic - default
    {1, "110"}, // Force wide (110 kHz) channel filter.
    {2, " 84"},
    {3, " 60"},
    {4, " 40"}};

int tabAmStep[] = {1,    // 0
                   5,    // 1
                   9,    // 2
                   10,   // 3
                   50,   // 4
                   500}; // 5

const int8_t lastAmStep = (sizeof tabAmStep / sizeof(int)) - 1;
int8_t idxAmStep = 3;

int tabFmStep[] = {5, 10, 20};
const int8_t lastFmStep = (sizeof tabFmStep / sizeof(int)) - 1;
int idxFmStep = 1;

uint16_t currentStepIdx = 1;


const char *bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
uint8_t currentMode = FM;

/**
 *  Band data structure
 */
typedef struct
{
  const char *bandName;   // Band description
  uint8_t bandType;       // Band type (FM, MW or SW)
  uint16_t minimumFreq;   // Minimum frequency of the band
  uint16_t maximumFreq;   // maximum frequency of the band
  uint16_t currentFreq;   // Default frequency or current frequency
  int8_t currentStepIdx;  // Idex of tabStepAM:  Defeult frequency step (See tabStepAM)
  int8_t bandwidthIdx;    // Index of the table bandwidthFM, bandwidthAM or bandwidthSSB;
  uint8_t disableAgc;     // Stores the disable or enable AGC for the band 
  int8_t agcIdx;          // Stores AGC value for the band
  int8_t agcNdx;          // Stores AGC number for the band 
  int8_t avcIdx;          // Stores AVC for the band
} Band;

/*
   Band table
   YOU CAN CONFIGURE YOUR OWN BAND PLAN. Be guided by the comments.
   To add a new band, all you have to do is insert a new line in the table below. No extra code will be needed.
   You can remove a band by deleting a line if you do not want a given band. 
   Also, you can change the parameters of the band.
   ATTENTION: You have to RESET the eeprom after adding or removing a line of this table. 
              Turn your receiver on with the encoder push button pressed at first time to RESET the eeprom content.  
*/
Band band[] = {
    {"VHF", FM_BAND_TYPE, 8700, 10800, 9470, 1, 0, 1, 0, 0, 0},
    {"MW ", MW_BAND_TYPE, 150, 1720, 810, 3, 4, 0, 0, 0, 32},
    {"SW1", SW_BAND_TYPE, 1700, 10000, 7200, 1, 4, 1, 0, 0, 32},
    {"SW2", SW_BAND_TYPE, 10000, 20000, 13600, 1, 4, 1, 0, 0, 32},
    {"SW3", SW_BAND_TYPE, 20000, 30000, 21500, 1, 4, 1, 0, 0, 32},
    {"ALL", SW_BAND_TYPE, 150, 30000, 28400, 0, 4, 1, 0, 0, 48}    // ALL (LW, MW and SW - from 150kHz to 30MHz
};

const int8_t lastBand = (sizeof band / sizeof(Band)) - 1;
int8_t bandIdx = 0;
int tabStep[] = {1, 5, 10, 50, 100, 500, 1000};
const int lastStep = (sizeof tabStep / sizeof(int)) - 1;

uint8_t rssi = 0;
uint8_t volume = DEFAULT_VOLUME;

// Devices class declarations
Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
LCD5110 nokia(NOKIA_CLK,NOKIA_DIN,NOKIA_DC,NOKIA_RST,NOKIA_CE);

extern uint8_t SmallFont[]; // Font Nokia
extern uint8_t BigNumbers[]; 

SI4735 rx;

void setup()
{
  // Encoder pins
  pinMode(ENCODER_PUSH_BUTTON, INPUT_PULLUP);
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  // Start the Nokia display device
  nokia.InitLCD();
  nokia.setContrast(70); // 0 - 120 -> Set the appropriated value for your Nokia 5110 display 
  splash(); // Show Splash - Remove this line if you do not want it. 
  EEPROM.begin();

  // If you want to reset the eeprom, keep the VOLUME_UP button pressed during statup
  if (digitalRead(ENCODER_PUSH_BUTTON) == LOW)
  {
    EEPROM.update(eeprom_address, 0);
    show(0,0,"EEPROM RESET");
    delay(2000);
    nokia.clrScr();
  }
  // controlling encoder via interrupt
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  rx.setI2CFastModeCustom(100000);
  rx.getDeviceI2CAddress(RESET_PIN); // Looks for the I2C bus address and set it.  Returns 0 if error
  rx.setup(RESET_PIN, MW_BAND_TYPE);
  delay(300);
  rx.setAvcAmMaxGain(48); // Sets the maximum gain for automatic volume control on AM/SSB mode (you can use values between 12 and 90dB).
  // Checking the EEPROM content
  if (EEPROM.read(eeprom_address) == app_id)
  {
    readAllReceiverInformation();
  } else 
    rx.setVolume(volume);
  
  useBand();
  showStatus();
}

/**
 * Shows splash message 
 */
void splash()
{
  nokia.setFont(SmallFont);
  nokia.clrScr();
  nokia.print(F("PU2CLR"),30,25);
  nokia.update(); 
  delay(2000);  
  nokia.clrScr();
  nokia.print(F("SI4735"),0,0);
  nokia.print(F("Arduino"), 0,15);
  nokia.print(F("Library"), 0, 30);
  nokia.update();
  delay(2000);
  nokia.clrScr();
}

/*
 *  writes the conrrent receiver information into the eeprom.
 *  The EEPROM.update avoid write the same data in the same memory position. It will save unnecessary recording.
 */
void saveAllReceiverInformation()
{
  int addr_offset;

  EEPROM.begin();
  EEPROM.update(eeprom_address, app_id);                 // stores the app id;
  EEPROM.update(eeprom_address + 1, rx.getVolume()); // stores the current Volume
  EEPROM.update(eeprom_address + 2, bandIdx);            // Stores the current band
  EEPROM.update(eeprom_address + 4, currentMode); // Stores the current Mode (FM / AM / SSB)
  EEPROM.update(eeprom_address + 5, currentBFO >> 8);
  EEPROM.update(eeprom_address + 6, currentBFO & 0XFF);

  addr_offset = 7;
  band[bandIdx].currentFreq = currentFrequency;

  for (int i = 0; i <= lastBand; i++)
  {
    EEPROM.update(addr_offset++, (band[i].currentFreq >> 8));   // stores the current Frequency HIGH byte for the band
    EEPROM.update(addr_offset++, (band[i].currentFreq & 0xFF)); // stores the current Frequency LOW byte for the band
    EEPROM.update(addr_offset++, band[i].currentStepIdx);       // Stores current step of the band
    EEPROM.update(addr_offset++, band[i].bandwidthIdx);         // table index (direct position) of bandwidth
    EEPROM.update(addr_offset++, band[i].disableAgc );
    EEPROM.update(addr_offset++, band[i].agcIdx);
    EEPROM.update(addr_offset++, band[i].agcNdx);
    EEPROM.update(addr_offset++, band[i].avcIdx);

  }
  EEPROM.end();
}

/**
 * reads the last receiver status from eeprom. 
 */
void readAllReceiverInformation()
{
  uint8_t volume;
  int addr_offset;
  int bwIdx;

  volume = EEPROM.read(eeprom_address + 1); // Gets the stored volume;
  bandIdx = EEPROM.read(eeprom_address + 2);
  currentMode = EEPROM.read(eeprom_address + 4);
  currentBFO = EEPROM.read(eeprom_address + 5) << 8;
  currentBFO |= EEPROM.read(eeprom_address + 6);
  addr_offset = 7;
  for (int i = 0; i <= lastBand; i++)
  {
    band[i].currentFreq = EEPROM.read(addr_offset++) << 8;
    band[i].currentFreq |= EEPROM.read(addr_offset++);
    band[i].currentStepIdx = EEPROM.read(addr_offset++);
    band[i].bandwidthIdx = EEPROM.read(addr_offset++);
    band[i].disableAgc = EEPROM.read(addr_offset++);
    band[i].agcIdx = EEPROM.read(addr_offset++);
    band[i].agcNdx = EEPROM.read(addr_offset++);
    band[i].avcIdx = EEPROM.read(addr_offset++);
  }
  currentFrequency = band[bandIdx].currentFreq;
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentStepIdx = idxFmStep = band[bandIdx].currentStepIdx;
    rx.setFrequencyStep(tabFmStep[currentStepIdx]);
  }
  else
  {
    currentStepIdx = idxAmStep = band[bandIdx].currentStepIdx;
    rx.setFrequencyStep(tabAmStep[currentStepIdx]);
  }
  bwIdx = band[bandIdx].bandwidthIdx;
  if (currentMode == LSB || currentMode == USB)
  {
    loadSSB();
    bwIdxSSB = (bwIdx > 5) ? 5 : bwIdx;
    rx.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);
    // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
    if (bandwidthSSB[bwIdxSSB].idx == 0 || bandwidthSSB[bwIdxSSB].idx == 4 || bandwidthSSB[bwIdxSSB].idx == 5)
      rx.setSSBSidebandCutoffFilter(0);
    else
      rx.setSSBSidebandCutoffFilter(1);
  }
  else if (currentMode == AM)
  {
    bwIdxAM = bwIdx;
    rx.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
  }
  else
  {
    bwIdxFM = bwIdx;
    rx.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
  }
  delay(50);
  rx.setVolume(volume);
}

/*
 * To store any change into the EEPROM, it is needed at least STORE_TIME  milliseconds of inactivity.
 */
void resetEepromDelay()
{
  elapsedCommand = storeTime = millis();
  itIsTimeToSave = true;
}

/**
 *   Set all command flags to false
 *   When all flags are disabled (false), the encoder controls the frequency
 */
void disableCommands()
{
  cmdBand = false;
  bfoOn = false;
  cmdVolume = false;
  cmdAgc = false;
  cmdBandwidth = false;
  cmdStep = false;
  cmdMode = false;
  cmdMenu = false;
  cmdSoftMuteMaxAtt = false;
  cmdAvc =  false; 
  countClick = 0;
  showCommandStatus((char *) "VFO ");
}

/**
 * Reads encoder via interrupt
 * Use Rotary.h and  Rotary.cpp implementation to process encoder via interrupt
 * if you do not add ICACHE_RAM_ATTR declaration, the system will reboot during attachInterrupt call. 
 * With ICACHE_RAM_ATTR macro you put the function on the RAM.
 */
void  rotaryEncoder()
{ // rotary encoder events
  uint8_t encoderStatus = encoder.process();
  if (encoderStatus)
    encoderCount = (encoderStatus == DIR_CW) ? 1 : -1;
}

/**
 * Shows a text on the display 
 */
void show(uint8_t col, uint8_t lin, const char *content) {
  nokia.setFont(SmallFont);
  nokia.print(content, col, lin);
  nokia.update();
}

/**
 * Shows frequency information on Display
 */
void showFrequency()
{
  char freqDisplay[10];
  uint8_t n = 5, d = 3;
  char *p;
 
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    if (currentFrequency < 10000) {
       n = 4;
       d = 2;
    }
    rx.convertToChar(currentFrequency, freqDisplay,n,d, '.');
    p = freqDisplay;
  }
  else
  {
    if (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) {
      rx.convertToChar(currentFrequency, freqDisplay, 5, 0, '.');
      p =  ( currentFrequency < 1000)?  &freqDisplay[2]:&freqDisplay[1];
    }
    else {
       rx.convertToChar(currentFrequency, freqDisplay, 5, 2, '.');
       p =  ( currentFrequency < 10000)?  &freqDisplay[1]:freqDisplay; 
    }
  }
  nokia.setFont(BigNumbers);
  nokia.print(p,0,13);
  nokia.update();

  showMode();
}

/**
 * Shows the current mode
 */
void showMode()
{
  char *bandMode;

  if (currentFrequency < 520)
    bandMode = (char *)"LW  ";
  else
    bandMode = (char *)bandModeDesc[currentMode];
  
  show(1,0,band[bandIdx].bandName);
  show(65,0,bandMode);

}

/**
 * Shows some basic information on display
 */
void showStatus()
{
  nokia.clrScr();
  nokia.drawLine(0,10,84,10);
  nokia.drawLine(0,38,84,38);
  nokia.update();
  showFrequency();
  showRSSI();
}

/**
 *  Shows the current Bandwidth status
 */
void showBandwidth()
{
  char *bw;
  char bandwidth[15];
  
  if (currentMode == LSB || currentMode == USB)
  {
    bw = (char *)bandwidthSSB[bwIdxSSB].desc;
    showBFO();
  }
  else if (currentMode == AM)
  {
    bw = (char *)bandwidthAM[bwIdxAM].desc;
  }
  else
  {
    bw = (char *)bandwidthFM[bwIdxFM].desc;
  }
  nokia.clrScr();
  strcpy(bandwidth, "BW: ");
  strcat(bandwidth, bw);
  show(1,1,bandwidth);
}

/**
 *   Shows the current RSSI and SNR status
 */
void showRSSI()
{
  int rssiAux = 0;
  char sMeter[7];

  if (currentMode == FM)
  {
    show(0,60,(rx.getCurrentPilot()) ? "ST" : "MO");
  } 
  if (rssi < 12)
    rssiAux = 0;
  else if (rssi < 25)
    rssiAux = 1;
  else if (rssi < 45)
    rssiAux = 2;
  else if (rssi < 60)
    rssiAux = 3;
  else 
    rssiAux = 4;

  nokia.drawBitmap(67, 42, (uint8_t *) signalLevel[rssiAux], 17 , 6);

}

/**
 *    Shows the current AGC and Attenuation status
 */
void showAgcAtt()
{
  char sAgc[11];
  nokia.clrScr();
  rx.getAutomaticGainControl();
  if ( !disableAgc /*agcNdx == 0 && agcIdx == 0 */ )
    strcpy(sAgc, "AGC ON");
  else {
   strcpy(sAgc,"ATT: ");
   rx.convertToChar(agcNdx, &sAgc[4], 2, 0, '.');
  }
  show(0,0,sAgc);
}

/**
 *   Shows the current step
 */
void showStep()
{
  char sStep[11];
  nokia.clrScr();
  strcpy(sStep,"STEP:");
  rx.convertToChar((currentMode == FM)? (tabFmStep[currentStepIdx] *10) : tabAmStep[currentStepIdx], &sStep[5], 4, 0, '.');
  show(0,0,sStep);
}

/**
 *  Shows the current BFO value
 */
void showBFO()
{
  char newBFO[10];
  uint16_t auxBfo;
  auxBfo = currentBFO;

  nokia.clrScr();
  strcpy(newBFO,"BFO:");
  if (currentBFO < 0 ) {
    auxBfo = ~currentBFO + 1; // converts to absolute value (ABS) using binary operator
    newBFO[4] = '-';
  }
  else if (currentBFO > 0 )
    newBFO[4] = '+';
  else
    newBFO[4] = ' ';

  rx.convertToChar(auxBfo, &newBFO[5], 4, 0, '.');
  show(0,0,newBFO);
  elapsedCommand = millis();
}

/*
 *  Shows the volume level on LCD
 */
void showVolume()
{
  char volAux[12];
  nokia.clrScr();
  strcpy(volAux,"VOLUME: ");
  rx.convertToChar(rx.getVolume(), &volAux[7], 2, 0, '.');
  show(0,0,volAux);
}

/**
 * Show Soft Mute 
 */
void showSoftMute()
{
  char sMute[14];
  nokia.clrScr();
  strcpy(sMute,"Soft Mute:");
  rx.convertToChar(softMuteMaxAttIdx, &sMute[10], 2, 0, '.');
  show(0,0,sMute);
}

/**
 * Show Soft Mute 
 */
void showAvc()
{
  char sAvc[7];
  nokia.clrScr();
  strcpy(sAvc,"AVC:");
  rx.convertToChar(avcIdx, &sAvc[4], 2, 0, '.');
  show(0,0,sAvc);
}

/**
 * Show menu options
 */
void showMenu() {
  nokia.clrScr();
  nokia.print(menu[menuIdx],0,20);
  nokia.update();
  showCommandStatus( (char *) "Menu");
}

/**
 *   Sets Band up (1) or down (!1)
 */
void setBand(int8_t up_down)
{
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStepIdx = currentStepIdx;
  if (up_down == 1)
    bandIdx = (bandIdx < lastBand) ? (bandIdx + 1) : 0;
  else
    bandIdx = (bandIdx > 0) ? (bandIdx - 1) : lastBand;
  useBand();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
}

/**
 * Switch the radio to current band
 */
void useBand()
{
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentMode = FM;
    rx.setTuneFrequencyAntennaCapacitor(0);
    rx.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabFmStep[band[bandIdx].currentStepIdx]);
    rx.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
    rx.setFifoCount(1);
    
    bfoOn = ssbLoaded = false;
    bwIdxFM = band[bandIdx].bandwidthIdx;
    rx.setFmBandwidth(bandwidthFM[bwIdxFM].idx);    
  }
  else
  {
    disableAgc = band[bandIdx].disableAgc;
    agcIdx = band[bandIdx].agcIdx;
    agcNdx = band[bandIdx].agcNdx;
    avcIdx = band[bandIdx].avcIdx;
    
    // set the tuning capacitor for SW or MW/LW
    rx.setTuneFrequencyAntennaCapacitor((band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) ? 0 : 1);
    if (ssbLoaded)
    {
      rx.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabAmStep[band[bandIdx].currentStepIdx], currentMode);
      rx.setSSBAutomaticVolumeControl(1);
      rx.setSsbSoftMuteMaxAttenuation(softMuteMaxAttIdx); // Disable Soft Mute for SSB
      bwIdxSSB = band[bandIdx].bandwidthIdx;
      rx.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);
      delay(500);
      rx.setSsbAgcOverrite(disableAgc, agcNdx);
    }
    else
    {
      currentMode = AM;
      rx.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, tabAmStep[band[bandIdx].currentStepIdx]);
      bfoOn = false;
      bwIdxAM = band[bandIdx].bandwidthIdx;
      rx.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
      rx.setAmSoftMuteMaxAttenuation(softMuteMaxAttIdx); // Soft Mute for AM or SSB
      rx.setAutomaticGainControl(disableAgc, agcNdx);

    }
    rx.setSeekAmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq); // Consider the range all defined current band
    rx.setSeekAmSpacing(5); // Max 10kHz for spacing
    rx.setAvcAmMaxGain(avcIdx);
  }
  delay(100);
  currentFrequency = band[bandIdx].currentFreq;
  currentStepIdx = band[bandIdx].currentStepIdx;
  rssi = 0;
  showStatus();
  showCommandStatus((char *) "Band");
}

/*
 *  This function loads the contents of the ssb_patch_content array into the CI (Si4735) and starts the radio on
 *  SSB mode.
 *  This version uses the compressed SSB patch version.
*/
void loadSSB()
{
  rx.setI2CFastModeCustom(500000);
  rx.queryLibraryId(); // Is it really necessary here? I will check it.
  rx.patchPowerUp();
  delay(50);
  rx.downloadCompressedPatch(ssb_patch_content, size_content, cmd_0x15, cmd_0x15_size);
  rx.setSSBConfig(bandwidthSSB[bwIdxSSB].idx, 1, 0, 1, 0, 1);
  rx.setI2CStandardMode();
  ssbLoaded = true;
}

/**
 *  Switches the Bandwidth
 */
void doBandwidth(int8_t v)
{
    if (currentMode == LSB || currentMode == USB)
    {
      bwIdxSSB = (v == 1) ? bwIdxSSB + 1 : bwIdxSSB - 1;

      if (bwIdxSSB > maxSsbBw)
        bwIdxSSB = 0;
      else if (bwIdxSSB < 0)
        bwIdxSSB = maxSsbBw;

      rx.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);
      // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
      if (bandwidthSSB[bwIdxSSB].idx == 0 || bandwidthSSB[bwIdxSSB].idx == 4 || bandwidthSSB[bwIdxSSB].idx == 5)
        rx.setSSBSidebandCutoffFilter(0);
      else
        rx.setSSBSidebandCutoffFilter(1);

      band[bandIdx].bandwidthIdx = bwIdxSSB;
    }
    else if (currentMode == AM)
    {
      bwIdxAM = (v == 1) ? bwIdxAM + 1 : bwIdxAM - 1;

      if (bwIdxAM > maxAmBw)
        bwIdxAM = 0;
      else if (bwIdxAM < 0)
        bwIdxAM = maxAmBw;

      rx.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
      band[bandIdx].bandwidthIdx = bwIdxAM;
    } else {
    bwIdxFM = (v == 1) ? bwIdxFM + 1 : bwIdxFM - 1;
    if (bwIdxFM > maxFmBw)
      bwIdxFM = 0;
    else if (bwIdxFM < 0)
      bwIdxFM = maxFmBw;

    rx.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
    band[bandIdx].bandwidthIdx = bwIdxFM;
  }
  showBandwidth();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
}

/**
 * Show cmd on display. It means you are setting up something.  
 */
void showCommandStatus(char * currentCmd)
{
  // nokia.clrScr();
  nokia.drawRect(29,0,60,10);
  nokia.print(currentCmd, 35, 2);
  nokia.update();
}

/**
 *  AGC and attenuattion setup
 */
void doAgc(int8_t v) {
  agcIdx = (v == 1) ? agcIdx + 1 : agcIdx - 1;
  if (agcIdx < 0 )
    agcIdx = 37;
  else if ( agcIdx > 37)
    agcIdx = 0;
  disableAgc = (agcIdx > 0); // if true, disable AGC; esle, AGC is enable
  if (agcIdx > 1)
    agcNdx = agcIdx - 1;
  else
    agcNdx = 0;
  if ( currentMode == AM ) 
     rx.setAutomaticGainControl(disableAgc, agcNdx); // if agcNdx = 0, no attenuation
  else
    rx.setSsbAgcOverrite(disableAgc, agcNdx, 0B1111111);

  band[bandIdx].disableAgc = disableAgc;
  band[bandIdx].agcIdx = agcIdx;
  band[bandIdx].agcNdx = agcNdx;

  showAgcAtt();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}


/**
 * Switches the current step
 */
void doStep(int8_t v)
{
    if ( currentMode == FM ) {
      idxFmStep = (v == 1) ? idxFmStep + 1 : idxFmStep - 1;
      if (idxFmStep > lastFmStep)
        idxFmStep = 0;
      else if (idxFmStep < 0)
        idxFmStep = lastFmStep;
        
      currentStepIdx = idxFmStep;
      rx.setFrequencyStep(tabFmStep[currentStepIdx]);
      
    } else {
      idxAmStep = (v == 1) ? idxAmStep + 1 : idxAmStep - 1;
      if (idxAmStep > lastAmStep)
        idxAmStep = 0;
      else if (idxAmStep < 0)
        idxAmStep = lastAmStep;

      currentStepIdx = idxAmStep;
      rx.setFrequencyStep(tabAmStep[currentStepIdx]);
      rx.setSeekAmSpacing(5); // Max 10kHz for spacing
    }
    band[bandIdx].currentStepIdx = currentStepIdx;
    showStep();
    delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
    elapsedCommand = millis();
}

/**
 * Switches to the AM, LSB or USB modes
 */
void doMode(int8_t v)
{
  if (currentMode != FM)
  {
    if (v == 1)  { // clockwise
      if (currentMode == AM)
      {
        // If you were in AM mode, it is necessary to load SSB patch (avery time)
        loadSSB();
        ssbLoaded = true;
        currentMode = LSB;
      }
      else if (currentMode == LSB)
        currentMode = USB;
      else if (currentMode == USB)
      {
        currentMode = AM;
        bfoOn = ssbLoaded = false;
      }
    } else { // and counterclockwise
      if (currentMode == AM)
      {
        // If you were in AM mode, it is necessary to load SSB patch (avery time)
        loadSSB();
        ssbLoaded = true;
        currentMode = USB;
      }
      else if (currentMode == USB)
        currentMode = LSB;
      else if (currentMode == LSB)
      {
        currentMode = AM;
        bfoOn = ssbLoaded = false;
      }
    }
    // Nothing to do if you are in FM mode
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStepIdx = currentStepIdx;
    useBand();
  }
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}

/**
 * Sets the audio volume
 */
void doVolume( int8_t v ) {
  if ( v == 1)
    rx.volumeUp();
  else
    rx.volumeDown();

  showVolume();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
}

/**
 *  This function is called by the seek function process.
 */
void showFrequencySeek(uint16_t freq)
{
  currentFrequency = freq;
  showFrequency();
}

/**
 *  Find a station. The direction is based on the last encoder move clockwise or counterclockwise
 */
void doSeek()
{
  if ((currentMode == LSB || currentMode == USB)) return; // It does not work for SSB mode
  nokia.clrScr();
  rx.seekStationProgress(showFrequencySeek, seekDirection);
  showStatus();
  currentFrequency = rx.getFrequency();
}

/**
 * Sets the Soft Mute Parameter
 */
void doSoftMute(int8_t v)
{
  softMuteMaxAttIdx = (v == 1) ? softMuteMaxAttIdx + 1 : softMuteMaxAttIdx - 1;
  if (softMuteMaxAttIdx > 32)
    softMuteMaxAttIdx = 0;
  else if (softMuteMaxAttIdx < 0)
    softMuteMaxAttIdx = 32;

  rx.setAmSoftMuteMaxAttenuation(softMuteMaxAttIdx);
  showSoftMute();
  elapsedCommand = millis();
}

/**
 * Sets the Max gain for Automatic Volume Control. 
 */
void doAvc(int8_t v)
{
  avcIdx = (v == 1) ? avcIdx + 2 : avcIdx - 2;
  if (avcIdx > 90)
    avcIdx = 12;
  else if (avcIdx < 12)
    avcIdx = 90;

  rx.setAvcAmMaxGain(avcIdx);
  band[bandIdx].avcIdx = avcIdx;
  showAvc();
  elapsedCommand = millis();
}

/**
 *  Menu options selection
 */
void doMenu( int8_t v) {
  menuIdx = (v == 1) ? menuIdx + 1 : menuIdx - 1;
  if (menuIdx > lastMenu)
    menuIdx = 0;
  else if (menuIdx < 0)
    menuIdx = lastMenu;

  showMenu();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}

/**
 * Return true if the current status is Menu command
 */
bool isMenuMode() {
  return (cmdMenu | cmdStep | cmdBandwidth | cmdAgc | cmdVolume | cmdSoftMuteMaxAtt | cmdMode | cmdAvc);
}

/**
 * Starts the MENU action process
 */
void doCurrentMenuCmd() {
  disableCommands();
  switch (currentMenuCmd) {
     case 0:                 // VOLUME
      cmdVolume = true;
      showVolume();
      break;
    case 1:                 // STEP
      cmdStep = true;
      showStep();
      break;
    case 2:                 // MODE
      cmdMode = true;
      nokia.clrScr();
      showMode();
      break;
    case 3:
        bfoOn = true;
        if ((currentMode == LSB || currentMode == USB)) {
          showBFO();
        }
      break;      
    case 4:                 // BW
      cmdBandwidth = true;
      showBandwidth();
      break;
    case 5:                 // AGC/ATT
      cmdAgc = true;
      showAgcAtt();
      break;
    case 6: 
      cmdSoftMuteMaxAtt = true;
      showSoftMute();  
      break;
    case 7: 
      cmdAvc =  true; 
      showAvc();
      break;  
    case 8:
      seekDirection = 1;
      doSeek();
      break;  
    case 9:
      seekDirection = 0;
      doSeek();
      break;    
    default:
      showStatus();
      break;
  }
  currentMenuCmd = -1;
  elapsedCommand = millis();
}

/**
 * Main loop
 */
void loop()
{
  // Check if the encoder has moved.
  if (encoderCount != 0)
  {
    if (bfoOn & (currentMode == LSB || currentMode == USB))
    {
      currentBFO = (encoderCount == 1) ? (currentBFO + currentBFOStep) : (currentBFO - currentBFOStep);
      rx.setSSBBfo(currentBFO);
      showBFO();
    }
    else if (cmdMenu)
      doMenu(encoderCount);
    else if (cmdMode)
      doMode(encoderCount);
    else if (cmdStep)
      doStep(encoderCount);
    else if (cmdAgc)
      doAgc(encoderCount);
    else if (cmdBandwidth)
      doBandwidth(encoderCount);
    else if (cmdVolume)
      doVolume(encoderCount);
    else if (cmdSoftMuteMaxAtt)
      doSoftMute(encoderCount);
    else if (cmdAvc)
      doAvc(encoderCount);      
    else if (cmdBand)
      setBand(encoderCount);
    else
    {
      if (encoderCount == 1)
      {
        rx.frequencyUp();
      }
      else
      {
        rx.frequencyDown();
      }
      // Show the current frequency only if it has changed
      currentFrequency = rx.getFrequency();
      showFrequency();
    }
    encoderCount = 0;
    resetEepromDelay();
  }
  else
  {
    if (digitalRead(ENCODER_PUSH_BUTTON) == LOW)
    {
      countClick++;
      if (cmdMenu)
      {
        currentMenuCmd = menuIdx;
        doCurrentMenuCmd();
      }
      else if (countClick == 1)
      { // If just one click, you can select the band by rotating the encoder
        if (isMenuMode())
        {
          disableCommands();
          showStatus();
          showCommandStatus((char *)"VFO ");
        }
        else if ( bfoOn ) {
          bfoOn = false;
          showStatus();
        }
        else
        {
          cmdBand = !cmdBand;
          showCommandStatus((char *)"Band");
        }
      }
      else
      { // GO to MENU if more than one click in less than 1/2 seconds.
        cmdMenu = !cmdMenu;
        if (cmdMenu)
          showMenu();
      }
      delay(MIN_ELAPSED_TIME);
      elapsedCommand = millis();
    }
  }
  // Show RSSI status only if this condition has changed
  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 6)
  {
    rx.getCurrentReceivedSignalQuality();
    int aux = rx.getCurrentRSSI();
    if (rssi != aux && !isMenuMode())
    {
      rssi = aux;
      showRSSI();
    }
    elapsedRSSI = millis();
  }
  // Disable commands control
  if ((millis() - elapsedCommand) > ELAPSED_COMMAND)
  {
    if ((currentMode == LSB || currentMode == USB) )
    {
      bfoOn = false;
      // showBFO();
      // showStatus();
    } else if (isMenuMode()) 
      showStatus();
    disableCommands();
    elapsedCommand = millis();
  }

  if ((millis() - elapsedClick) > ELAPSED_CLICK)
  {
    countClick = 0;
    elapsedClick = millis();
  }

  // Show the current frequency only if it has changed
  if (itIsTimeToSave)
  {
    if ((millis() - storeTime) > STORE_TIME)
    {
      saveAllReceiverInformation();
      storeTime = millis();
      itIsTimeToSave = false;
    }
  }
  delay(2);
}
