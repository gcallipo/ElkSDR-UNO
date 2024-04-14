
/**
 * 
   ElkSDR_UNO_v1.0.0 - (C) Giuseppe Callipo - IK8YFW

   This source file is under General Public License version 3.
   26.03.2023  - first release.
   24.04.2023  - update tuning and step.
   24.03.2024  - added external tuning knob with fast mode scan.

// Pin used to control the staked SDR_ELEKTOR board
// V3 3-3-17
// Output 4 x 2-30MHz on CLK1, in 10kHz-1MHz steps
// Si5351 I2C bus
// SDA = A4
// SCL = A5

// Pin used to control The tune and button on FDRobot board
// Input analog = A0

// Pin used to control The tuning potentiometer
// (Central pot. )   Input analog = A1
// (Left side pot. ) + 5V
// (Right sid pot. ) GND
// 
 */

/*******************************************************
*                   Base libraries             
********************************************************/
#include <LiquidCrystal.h>

//https://github.com/etherkit/Si5351Arduino
#include "si5351.h"

/*******************************************************
*                   Modules enable             
********************************************************/
//#define DEBUG_SERIAL

// xtal correction for Elektor DDS
#define CORRE 180000

// min freq 0.5MHz, max 30MHz (cHz)
#define FREQMIN 50000000
#define FREQMAX 3000000000

// freq steps 100kHz max, 10Hz step (cHz)
#define FREQ1MHZ    100000000
#define FREQSTEPMAX 10000000
#define FREQSTEP 1000

#define FREQSTEP_10Hz      1000
#define FREQSTEP_100Hz    10000
#define FREQSTEP_1KHz    100000
#define FREQSTEP_10K Hz 1000000

// Potentiometer tuning
#define ANALOG_TUNING A1

// dds object
Si5351 dds;

volatile uint64_t baseTune = 710000000;
// start freq & freqStep (cHz)
volatile uint64_t freq = 710000000; // 7.1MHz
volatile uint64_t freqStep = 1000000; // 10kHz
         uint64_t freqStep_old=0;
// freq change flag
volatile bool freqChange;
int old_knob;

// Output Freq for Elektor SDR, f (cHz) x 4 on CLK1
void freqOut(uint64_t f) {
  dds.set_freq(f * 4ULL, SI5351_CLK1);
}

/*******************************************************
*                   DF1 ROBOT CARD             
********************************************************/

char* string2char(String command){
    if(command.length()!=0){
        char *p = const_cast<char*>(command.c_str());
        return p;
    }
}

// select the pins used on the LCD panel
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// define some values used by the panel and buttons
int lcd_key     = 0;
int adc_key_in  = 0;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

// read the buttons
int read_LCD_buttons()
{
 adc_key_in = analogRead(0);       
 // my buttons when read are centered at these valies: 0, 144, 329, 504, 741
 // we add approx 50 to those values and check to see if we are close
 if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
 
 // For V1.1 us this threshold
 if (adc_key_in < 50)   return btnRIGHT;  
 if (adc_key_in < 250)  return btnUP; 
 if (adc_key_in < 450)  return btnDOWN; 
 if (adc_key_in < 650)  return btnLEFT; 
 if (adc_key_in < 850)  return btnSELECT;  

 return btnNONE;  // when all others fail, return this...
}

void LOGGER_debug(char* msg) {
   #ifdef DEBUG_SERIAL
    Serial.println( msg);
   #endif
}

void LOGGER_debugs(String msg) {
   #ifdef DEBUG_SERIAL
    Serial.println( msg);
   #endif
}

void LOGGER_debugsi(String msg, int ivalue) {
   #ifdef DEBUG_SERIAL
   Serial.println(" ");
    Serial.print(msg );Serial.print(" ");
    Serial.println(ivalue, DEC);
   #endif
}

// read the tuning potentiometer
int read_TUNING_potentiometer()
{
    doTuning();
}

//*************************************

// function to read the position of the tuning knob at high precision (Allard, PE1NWL)
int knob_position() {
  unsigned long knob = 0;
  // the knob value normally ranges from 0 through 1023 (10 bit ADC)
  // in order to increase the precision by a factor 10, we need 10^2 = 100x oversampling
  for (byte i = 0; i < 100; i++) {
    knob = knob + analogRead(ANALOG_TUNING); // take 100 readings from the ADC
  }
  knob = (knob + 5L) / 10L; // take the average of the 100 readings and multiply the result by 10
  //now the knob value ranges from 0 through 10230 (10x more precision)
  return (int)knob;
}

void doTuning() {
  
  int knob = knob_position(); // get the precise tuning knob position


  // the knob is fully on the low end, do fast tune: move down and wait for 300 msec
  // step size is variable: the closer the knob is to the end, the larger the step size
  if (knob < 100 && freq > FREQMIN) {

    LOGGER_debugsi("1. Knob : ", knob);
      baseTune = baseTune - (100 - knob) * 1000UL; // fast tune down in max 1 kHz steps
      freq = constrain(baseTune + knob*10, FREQMIN, FREQMAX);
      freqChange = true;
    delay(200);
  }

  // the knob is full on the high end, do fast tune: move up  and wait for 300 msec
  // step size is variable: the closer the knob is to the end, the larger the step size
  else if (knob > 10130 && freq < FREQMAX) {
    LOGGER_debugsi("2. Knob : ", knob);

      baseTune = baseTune + (knob - 10130) * 1000UL; // fast tune up in max 1 kHz steps
      freq = constrain(baseTune + knob*10, FREQMIN, FREQMAX);
      freqChange = true;
    delay(200);
  }

  // the tuning knob is at neither extremities, tune the signals as usual
  else {
    if (abs(knob - old_knob) > 5) { // improved "flutter fix": only change frequency when the current knob position is more than 4 steps away from the previous position
      
      int direction = (knob-old_knob) >= 0? +1: -1;

      //knob = (knob + old_knob) / 2; // tune to the midpoint between current and previous knob reading
      old_knob = knob;
      
      LOGGER_debugsi("3. Knob : ", knob);

       freq = constrain(baseTune + (knob*direction), FREQMIN, FREQMAX);
      freqChange = true;

      delay(20);
    }
  }
}


//**************************************

/******************************************************
 * 
 *   DISPLAY CONTROL UNIT - UX IF
 * 
 ******************************************************/

  // Buffer for display
 char c[18] , c2[18], b[11], printBuff1[18], printBuff2[18];

 void UXIF_process_command(){

 // try to read from button ... 
 lcd_key = read_LCD_buttons();  // read the buttons

 // if no command, try to decode potentiometer ...
 if (lcd_key == btnNONE){
   read_TUNING_potentiometer();
   return;
 }
 delay(100);
  
 switch (lcd_key)               // depending on which button was pushed, we perform an action
 {
   case btnRIGHT:
     {
      LOGGER_debugs("RIGHT BAND");
     
      if (freqStep == FREQ1MHZ){
          freqStep=freqStep_old;
          freqStep_old=FREQ1MHZ;
        }else{
          freqStep_old=freqStep;
          freqStep = FREQ1MHZ;
        }
         dispfreqStep(10, 1, freqStep); // display freq step
         delay(500);
     break;
     
     }
   case btnLEFT:
     {
           // change slow step
        LOGGER_debugs("LEFT STEP");
      
        // change freqStep, 10kHz to 100kHz
        if (freqStep >= FREQSTEPMAX) freqStep = FREQSTEP; // back to 10Hz
        else freqStep = freqStep * 10; // or increase by x10
        
        dispfreqStep(10, 1, freqStep); // display freq step
        delay(500);


     break;
     }
   case btnUP:
     {
      LOGGER_debug("UP INCREASE FREQ");

      if (freq < FREQMAX) {
        freq += freqStep;
        freqChange = true;
      }
     
     break;
     }
   case btnDOWN:
     {
      LOGGER_debug("DOWN DECREASE FREQ");

      if (freq > FREQMIN) {
        freq -= freqStep;
        freqChange = true;
      }
     
     break;
     }
     case btnNONE:
     {
      // LOGGER_debug("RST");
     break;
     }
  }
 }

 /**
   Display Routines
   These two display routines print a line of characters to the upper and lower lines of the 16x2 display
*/

void printLine1(char *c) {
  if (strcmp(c, printBuff1)) { // only refresh the display when there was a change
    lcd.setCursor(0, 0); // place the cursor at the beginning of the top line
    lcd.print(c); // write text
    strcpy(printBuff1, c);

    for (byte i = strlen(c); i < 16; i++) { // add white spaces until the end of the 16 characters line is reached
      lcd.print(' ');
    }
  }
}

void printLine2(char *c) {
  if (strcmp(c, printBuff2)) { // only refresh the display when there was a change
    lcd.setCursor(0, 1); // place the cursor at the beginning of the bottom line
    lcd.print(c);
    strcpy(printBuff2, c);

    for (byte i = strlen(c); i < 16; i++) { // add white spaces until the end of the 16 characters line is reached
      lcd.print(' ');
    }
  }
}


// display freq at c)ol, r)ow, f (cHz), to d decimal places (kHz)
void dispFreq(uint8_t c, uint8_t r, uint64_t f, uint8_t d) {
  printLine2((char *)"                ");
  lcd.setCursor(c, r);
  //lcd.print((float)f / 100000, d); // convert to float & kHz
  lcd.print((float)f / 1000, d); // convert to float & kHz
  //lcd.print("kHz ");
  lcd.print("  ");
}

// display at c)ol, r)ow freq step s (cHz)
void dispfreqStep(byte c, byte r, uint64_t s)
{
  lcd.setCursor(c, r);
  switch (s) // display freqStep
  {
    case 1000:
      lcd.print("10Hz    ");
      break;
    case 10000:
      lcd.print("100Hz   ");
      break;
    case 100000:
      lcd.print("1kHz    ");
      break;
    case 1000000:
      lcd.print("10kHz   ");
      break;
    case 10000000:
      lcd.print("100kHz  ");
      break;
    case 100000000:
      lcd.print("1MHz    ");
      break;
  }
}

// display msg *m at c)ol, r)ow
void dispMsg(uint8_t c, uint8_t r, char *m)
{
  lcd.setCursor(c, r);
  lcd.print(m);
}

 /**
   Main Setup and Loop
*/

void setup()
{

 // Initialize the crystal
 lcd.begin(16, 2);   
 lcd.setCursor(0,0);
 printBuff1[0] = 0;
 printBuff2[0] = 0;

 // init dds si5351 module, "0" = default 25MHz XTAL, correction
 dds.init(SI5351_CRYSTAL_LOAD_8PF, 0, CORRE);
 
 // set 8mA output drive
 dds.drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);

 // enable VFO output CLK1, disable CLK0 & 2
 dds.output_enable(SI5351_CLK0, 0);
 dds.output_enable(SI5351_CLK1, 1);
 dds.output_enable(SI5351_CLK2, 0);
 
 // Initialize serial
 #ifdef DEBUG_SERIAL
    Serial.begin(9600);
 #endif

 printLine1((char *)"ElkSDR-UNO");
 printLine2((char *)"IK8YFW 2023");

 delay(3000);
 
 freqOut(freq);
 dispFreq(0, 1, freq, 0); // display freq
 dispfreqStep(10, 1, freqStep); // display freqStep xxxxHz|kHz col 10, row 1
 
}

void loop()
{

 UXIF_process_command();
 delay(100);

 if (freqChange) {
    freqOut(freq);
    freqChange = false;
    dispFreq(0, 1, freq, 0); // display freq
    dispfreqStep(10, 1, freqStep); // display freqStep xxxxHz|kHz col 10, row 1

    //unsigned long knob = knob_position(); // get the current tuning knob position
    baseTune = freq;// - (knob);

  }
  
}
