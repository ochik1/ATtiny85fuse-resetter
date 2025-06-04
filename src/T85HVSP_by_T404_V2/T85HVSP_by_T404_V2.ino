// AVR High-voltage Serial Fuse Reprogrammer
// Adapted from code and design by Paul Willoughby 03/20/2010
//   http://www.rickety.us/2010/03/arduino-avr-high-voltage-serial-programmer/
// and Wayne Holder 
//   https://sites.google.com/site/wayneholder/attiny-fuse-reset
//
// Fuse Calc:
//   http://www.engbedded.com/fusecalc/

/*
**  Ported to ATtiny404 & add only RSTDISBL initialize fuction by K.Ochi Jun. 04 2025
**  
**  ATtiny404 14pin arduino/megaTinyCore port
**  https://github.com/SpenceKonde/megaTinyCore
**   Pinout 
**    https://github.com/SpenceKonde/megaTinyCore/blob/master/megaavr/extras/ATtiny_x04.gif
**        VDD|        |GND
**     0  PA4|        |PA3 10
**     1  PA5|        |PA2  9
**     2  PA6|        |PA1  8
**     3  PA7|        |PA0 11 (nRESET/UPDI)
**     4  PB3|        |PB0  7
**     5  PB2|        |PB1  6
**    
*/

#define  SW1    5   // PB2 Reset Start Switch
#define  LED    4   // PB3 Status indicator LED
#define  VEN    9   // PA2 Target Power Enable to RoadSwitch.EN
#define  RES    8   // PA1 Output to level shifter for !RESET from transistor
#define  SDI   10   // PA3 Target.SDI:PB0, Data Input
#define  SII    0   // PA4 Target.SII:PB1, Instruction Input
#define  SDO    1   // PA5 Target.SDO:PB2, Data Output
#define  SCI    2   // PA6 Target.SCI:PB3, Clock Input
#define  CLA    3   // PA7 1:Select Clear All or 0:Clear RSTDISBL Only

#define  HFUSE  0x747C
#define  LFUSE  0x646C
#define  EFUSE  0x666E

// ATTiny series signatures
#define  ATTINY13   0x9007  // L: 0x6A, H: 0xFF             8 pin
#define  ATTINY24   0x910B  // L: 0x62, H: 0xDF, E: 0xFF   14 pin
#define  ATTINY25   0x9108  // L: 0x62, H: 0xDF, E: 0xFF    8 pin
#define  ATTINY44   0x9207  // L: 0x62, H: 0xDF, E: 0xFFF  14 pin
#define  ATTINY45   0x9206  // L: 0x62, H: 0xDF, E: 0xFF    8 pin
#define  ATTINY84   0x930C  // L: 0x62, H: 0xDF, E: 0xFFF  14 pin
#define  ATTINY85   0x930B  // L: 0x62, H: 0xDF, E: 0xFF    8 pin

// readFuses() set gloval variables
byte FuseH = 0;
byte FuseL = 0; 
byte FuseX = 0;

// State definition
#define IDLE    0
#define PRG_S1  1
#define PRG_S2  2
#define PRG_S3  3
#define SUCCESS 4
#define ERROR   5
uint8_t State = IDLE;

byte shiftOut (byte val1, byte val2) {
  int inBits = 0;
  //Wait until SDO goes high
  while (!digitalRead(SDO))
    ;
  unsigned int dout = (unsigned int) val1 << 2;
  unsigned int iout = (unsigned int) val2 << 2;
  for (int ii = 10; ii >= 0; ii--)  {
    digitalWrite(SDI, !!(dout & (1 << ii)));
    digitalWrite(SII, !!(iout & (1 << ii)));
    inBits <<= 1;
    inBits |= digitalRead(SDO);
    digitalWrite(SCI, HIGH);
    digitalWrite(SCI, LOW);
  }
  return inBits >> 2;
}

void writeFuse (unsigned int fuse, byte val) {
  shiftOut(0x40, 0x4C);
  shiftOut( val, 0x2C);
  shiftOut(0x00, (byte) (fuse >> 8));
  shiftOut(0x00, (byte) fuse);
}

void readFuses (int8_t sig) {
  shiftOut(0x04, 0x4C);  // LFuse
  shiftOut(0x00, 0x68);
  FuseL = shiftOut(0x00, 0x6C);

  shiftOut(0x04, 0x4C);  // HFuse
  shiftOut(0x00, 0x7A);
  FuseH = shiftOut(0x00, 0x7E);
  if(sig!=13) {
    shiftOut(0x04, 0x4C);  // EFuse
    shiftOut(0x00, 0x6A);
    FuseX = shiftOut(0x00, 0x6E);
  } else {
    FuseX = 0;
  }
}

unsigned int readSignature () {
  unsigned int sig = 0;
  byte val;
  for (int ii = 1; ii < 3; ii++) {
    shiftOut(0x08, 0x4C);
    shiftOut(  ii, 0x0C);
    shiftOut(0x00, 0x68);
    val = shiftOut(0x00, 0x6C);
    sig = (sig << 8) + val;
  }
  return sig;
}

// PRG_P0
void powerOnTarget(void)
{
  digitalWrite(VEN, HIGH);    // Target Power ON
  delayMicroseconds(50);
  digitalWrite(RES, HIGH);    // 12V RESET ON
  delayMicroseconds(10);
  pinMode(SDO, INPUT);        // Set SDO to input
  delayMicroseconds(300);
}

void powerOffTarget(void)
{
  digitalWrite(SDI, LOW);
  digitalWrite(SII, LOW);
  digitalWrite(SCI, LOW);
  digitalWrite(RES, LOW);     // 12V RESET OFF
  digitalWrite(VEN, LOW);     // Target Power OFF
}


// PRG_P1
int checkSignature(void)
{
  unsigned int sig = readSignature();
  if(sig==ATTINY13) {
    return 13;
  } else if(sig==ATTINY24 || sig==ATTINY44 || sig==ATTINY84 ||
    sig==ATTINY25 || sig==ATTINY45 || sig==ATTINY85) {
    return 85;    // Valid signatures return 85
  } else {
    return -1;
  }
}

// PRG_P2
int8_t resetFuse(int8_t sig)
{
  uint8_t wrFuseL = sig==13 ? 0x6A : 0x62;
  uint8_t wrFuseH = sig==13 ? 0xFF : 0xDF;
  uint8_t wrFuseX = 0xFF;

  // read short pin state & select ALL clear or RSTDISB clear
  int all_clr = digitalRead(CLA);
  if (!all_clr) {    // jumper is shorted
    readFuses(sig);  // Read Fuses to modify RSTDISBL in !CLA mode
    wrFuseL = FuseL;
    wrFuseH = sig==13 ? FuseH | 0x01 : FuseH | 0x80;
    wrFuseX = FuseX;
  }

  // write fuses
  writeFuse(LFUSE, wrFuseL);
  writeFuse(HFUSE, wrFuseH);
  if (sig!=13) {
    writeFuse(EFUSE, wrFuseX);
  }

  // read back & check
  readFuses(sig);    // check to make sure fuses were set properly

  if (FuseL!=wrFuseL || FuseH!=wrFuseH) {
    return -1;      // fast flash if fuses don't match expected
  } else {
    if (sig==13) {
      return 1;
    } else {
      if (FuseX!=wrFuseX) {
        return -1;    // fast flash if fuses don't match expected
      } else {
        return 1;
      }
    }
  }
}


void setup() {
  // Init PIOs
  pinMode(RES, OUTPUT);
  digitalWrite(RES, LOW);   // 12V OFF
  pinMode(VEN, OUTPUT);
  digitalWrite(VEN, LOW);   // Target VDD OFF
  pinMode(SDI, OUTPUT);
  pinMode(SII, OUTPUT);
  pinMode(SCI, OUTPUT);
  pinMode(SDO, OUTPUT);     // Configured as input when in programming mode
  digitalWrite(SDI, LOW);
  digitalWrite(SII, LOW);
  digitalWrite(SCI, LOW);
  digitalWrite(SDO, LOW);
  pinMode(LED, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);
  digitalWrite(LED, LOW);
  pinMode(CLA, INPUT_PULLUP);   // CLA Short PIN

  // ready flash led
  for(uint8_t i = 0; i<5; i++) {
    delay(200);
    digitalWrite(LED, HIGH);
    delay(200);
    digitalWrite(LED, LOW);
  }
}

void loop() {
  static uint8_t sw_dly = 1;
  static uint8_t sw_now = 1;
  int sig = 0;
  static int LED_FLASH = 0;


  // Check switch & GO
  sw_now = (uint8_t)digitalRead(SW1);
  if((State==IDLE) && sw_dly && (!sw_now)) {
    State = PRG_S1;
  }
  sw_dly = sw_now;

  // Reset Fuse Sequrence
  if(State==PRG_S1) {    // 
    powerOnTarget();
    delay(1000);          // wait
    State = PRG_S2;
  }

  if(State==PRG_S2) {
    sig = checkSignature();
    if(sig>0) {
      State = PRG_S3;
    } else {
      State = ERROR;
      powerOffTarget();
    }
  }

  if(State==PRG_S3) {
    int8_t rtn = resetFuse(sig);
    if(rtn>0) {
      State = SUCCESS;
    } else {
      State = ERROR;
    }
    powerOffTarget();
  }

  // LED
  if(State==SUCCESS) {
    digitalWrite(LED, HIGH);    // ON
  }

  if(State== ERROR) {    // FLASH at High Speed
    digitalWrite(LED, LED_FLASH);
    LED_FLASH ^= 1;
  }

  delay(100);
}

