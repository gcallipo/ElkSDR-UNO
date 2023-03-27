
/**
 * 
   ElkSDR_UNO_v1.0.0 - (C) Giuseppe Callipo - IK8YFW

   This source file is under General Public License version 3.
   26.03.2023  - first release.


  
// SDR_ELEKTOR
// V3 3-3-17
// Output 4 x 2-30MHz on CLK1, in 10kHz-1MHz steps
// Si5351 I2C bus
// SDA = A4
// SCL = A5
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

// freq steps 100kHz max, 10kHz step (cHz)
#define FREQSTEPMAX 100000000
#define FREQSTEP 1000

// dds object
Si5351 dds;


// start freq & freqStep (cHz)
volatile uint64_t freq = 710000000; // 7.1MHz
volatile uint64_t freqStep = 1000000; // 10kHz

// freq change flag
volatile bool freqChange;

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

/******************************************************
 * 
 *   DISPLAY CONTROL UNIT - UX IF
 * 
 ******************************************************/

  // Buffer for display
 char c[18] , c2[18], b[11], printBuff1[18], printBuff2[18];

 void UXIF_process_command(){
 
 lcd_key = read_LCD_buttons();  // read the buttons

 switch (lcd_key)               // depending on which button was pushed, we perform an action
 {
   case btnRIGHT:
     {
      LOGGER_debugs("RIGHT BAND");
     
      // LOGGER_debugs(idust_status);
     break;
     }
   case btnLEFT:
     {
      LOGGER_debugs("LEFT STEP");
      
        // change freqStep, 10kHz to 1MHz
        if (freqStep == FREQSTEPMAX) freqStep = FREQSTEP; // back to 10kHz
        else freqStep = freqStep * 10; // or increase by x10
        
        dispFreq(0, 1, freq, 0); // display freq
        dispfreqStep(10, 1, freqStep); // display freqStep xxxxHz|kHz col 10, row 1
 

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
 Serial.begin(9600);

 printLine1((char *)"ElkSDR-UNO");
 printLine2((char *)"IK8YFW 2023");

 delay(3000);

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
  }
  
}
