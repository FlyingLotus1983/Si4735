/* Arduino Si4735 Library.
 * Original by Ryan Owens for SparkFun Electronics on 2011-5-17.
 * Altered by Wagner Sartori Junior <wsartori@gmail.com> on 2011-09-13.
 * Altered by Jon Carrier <jjcarrier@gmail.com> on 2011-10-20.
 * Cleaned up and improved by Michael J. Kennedy.
 *
 * Copyright 2012, 2013 Michael J. Kennedy.
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 * To view a copy of the GNU Lesser General Public License, visit these two web pages:
 *    http://www.gnu.org/licenses/gpl-3.0.html
 *    http://www.gnu.org/licenses/lgpl-3.0.html
 *
 * See README and Si4735.h files for additional documentation.
 * See the example sketches to learn how to use the library in your code.
 *
 * Whenever practical, large arrays are located in program memory (flash ROM),
 * not in SRAM.  On AVR based machines, these arrays are marked with a PROGMEM
 * macro, and require special macros or functions to access.  String functions
 * with _P added to the end of their name can read a PROGMEM string.  Examples:
 *    char buffer[100];
 *    static const char PROGMEM message[] = "Hello";
 *    strcpy_P(buffer, message);
 *
 *    byte c;
 *    static const byte PROGMEM code[] = {2,5,99,3};
 *    c = pgm_read_byte(&code[i]);
 *
 *    word user;
 *    static const word PROGMEM id[] = {1000,1001,1002,2000};
 *    user = pgm_read_word(&id[i]);
 * See <avr/pgmspace.h> include file and GCC documentation (especially
 * "avr-libc-user-manual") for more info:
 * http://www.nongnu.org/avr-libc/user-manual/pgmspace.html
 */

#include "Si4735.h"
#ifdef Si47xx_SPI
 #include "SPI.h"
#else
 #include "Wire.h"
#endif
#include <string.h>

// Arguments for tune_status().
// ARG1 of TUNE_STATUS command.
enum {
   TUNE_STATUS_CANCEL_SEEK = TUNE_STATUS_ARG1_CANCEL_SEEK,
   TUNE_STATUS_CLEAR_STC   = TUNE_STATUS_ARG1_CLEAR_INT
};

// Arguments for seek_start().  Gives seek direction with wrap mode selected.
// ARG1 of SEEK_START command.
enum {
   SEEK_START_UP   = SEEK_START_ARG1_WRAP | SEEK_START_ARG1_SEEK_UP,
   SEEK_START_DOWN = SEEK_START_ARG1_WRAP
};

/******************************************************************************
*   Initialization                                                            *
******************************************************************************/

// The Si4735 class constructor to initialize a new object.
Si4735::Si4735(){
   //Init variables
   _frequency  = 0;            //No frequency tuned
   _mode       = RADIO_OFF;    //Radio is initially off
   _region     = REGION_2_NA;  //Default to ITU Region 2, subregion North America
   _locale     = LOCALE_US;    //Default to USA
   _volume     = MAX_VOLUME;   //Default to max volume
   _mute       = false;        //Default to mute off
   _interrupts = CTS_MASK;     //Radio's default interrupts
   clearStationInfo();
   //Clear revision info
   revision.partNumber    =0xFF;
   revision.firmwareMajor ='\0';
   revision.firmwareMinor ='\0';
   revision.componentMajor='\0';
   revision.componentMinor='\0';
   revision.chip          ='\0';
   //Make sure end of string buffers are null terminated
   rds.programService[sizeof(rds.programService)-1]='\0';
   rds.radioText[sizeof(rds.radioText)-1]='\0';
   rds.programTypeName[sizeof(rds.programTypeName)-1]='\0';
}

// Clear RDS station info.
void Si4735::clearStationInfo(){
   //Clear info
   rds.programId=0;  //Unknown station
   rds.RDSSignal=false;  //RDS signal not yet detected
   rds.RBDS=check_if_RBDS();  //Initial guess of RDS/RBDS status
   rds.programType=0;  //Unknown programming format
   rds.groupA=0;  //No RDS groups received yet
   rds.groupB=0;
   rds.extendedCountryCode=ECC_UNKNOWN;
   rds.language       =LANG_UNKNOWN;
   rds.trafficProgram =unknown;
   rds.trafficAlert   =unknown;
   rds.music          =unknown;
   rds.dynamicPTY     =unknown;
   rds.compressedAudio=unknown;
   rds.binauralAudio  =unknown;
   rds.RDSStereo      =unknown;
   rds.offset         =NO_DATE_TIME;  //No date/time yet received
   _abRadioText       =unknown;
   _abProgramTypeName =unknown;
   _extendedCountryCode_count=0;
   _language_count           =0;
   //Clear strings
   for(byte i=0; i<sizeof(rds.programService)-1; i++) rds.programService[i]=' ';
   rds.radioText[0]='\0';
   rds.radioTextLen=0;  //Radio Text not yet received
   rds.programTypeName[0]='\0';
}

#ifndef __AVR__
// Interrupt flag
static volatile bool interrupt_signal=false;

// Interrupt handler for ARM based Arduinos.
static void interrupt_handler(){
   //Tell currentInterrupts() that interrupt signal received from radio
   interrupt_signal=true;
}
#endif

// Applies power to and resets the radio.  Initializes interrupts.
// If option BEGIN_DO_NOT_INIT_BUS is given, the SPI or I2C bus is NOT initialized.
// See sections 6 "Control Interface" and 7 "Powerup" in Si47xx Programming Guide
// and Table 4 "Reset Timing Characteristics" in Si4734/35-C40 data sheet.
void Si4735::begin(byte options, byte bus_arg){
 #ifdef Si47xx_SPI

   //Configure the SPI hardware
   digitalWrite(RADIO_SPI_SS_PIN, HIGH);
   pinMode(RADIO_SPI_SS_PIN, OUTPUT);
   //Init SPI, if requested
   if( !(options & BEGIN_DO_NOT_INIT_BUS) ){
      SPI.begin();
      //Note: Max speed of Si4735 clock input is 2.5 MHz.
      SPI.setClockDivider(bus_arg ? bus_arg : RADIO_SPI_CLOCK_DIV);
   }

 #else

   //Init I2C, if requested
   //Note: I2C's SCLK must be initialized (that is, high) before reset goes high below.
   if( !(options & BEGIN_DO_NOT_INIT_BUS) ){
      Wire.begin();
   }
   //Save radio's address
   _address = bus_arg ? bus_arg : RADIO_I2C_ADDRESS;

 #endif

   pinMode(RADIO_POWER_PIN, OUTPUT);
   pinMode(RADIO_RESET_PIN, OUTPUT);
   //Hard reset radio
   digitalWrite(RADIO_RESET_PIN, LOW);
   //At this point, power may be on or off, depending on when we are called.
   //Remove power from radio
   digitalWrite(RADIO_POWER_PIN, LOW);
 #if 00  // <---Kill driving RADIO_INT_PIN
   //DANGER: We cannot output a high signal on the RADIO_INT_PIN if a unidirectional
   //level shifter is used on the INT pin.  Breakout board users should use a 10 kΩ
   //pull-up resistor (to 3.3V) to select SPI, just like the shield does.
 #ifdef Si47xx_SPI
   //Tell radio to use SPI mode.  INT pin is read by radio when RESET pin rises.
   //Note: The SparkFun Arduino shield already provides a 10 kΩ pull-up resistor
   //on the GPO2/INT pin, which makes this step unnecessary for the shield.
   //Driving this pin is only useful for breakout board users who are using a
   //bidirectional level shifter or do not need a level shifter.
   pinMode(RADIO_INT_PIN, OUTPUT);
   digitalWrite(RADIO_INT_PIN, HIGH);
 #endif
 #endif
   //Give chip a chance to fully power down
   //Note: We wait here because we have removed power from the chip, it takes
   //time to discharge the capacitors connected to the radio's power pins, and
   //circuits don't like it when their power supply makes rapid changes.
   delay(1);
   //Note: Reset must be low while applying power.
   //Apply power to radio
   digitalWrite(RADIO_POWER_PIN, HIGH);
   //Note: Power must be stable for 250 µs before releasing reset.
   //Note: We wait 50 µs longer because capacitors connected to the radio's power
   //pins take time to charge and also for safety.
   //Note: Setup time for GPO1 & GPO2 before reset goes high to select the radio's
   //bus mode is 100 µs.  However, this is less than the 250 µs we must wait anyways.
   //Note: There may not be any I2C or SPI bus traffic 300 ns before reset goes high.
   //Wait 250 µs between applying power and releasing reset.
   delayMicroseconds(250+50);  //Chip requires 250 µs, extra 50 µs for safety
   //Release reset - radio now does its internal cold power up initialization
   digitalWrite(RADIO_RESET_PIN, HIGH);
   //Give chip time to start-up
   //Note: The hold time for GPO1 & GPO2 after reset goes high is 30 ns.
   //Note: The data sheet and guide do not indicate a need to wait before receiving
   //the first command.  However, it's better to wait just a little.
   delay(1);
   //After hardware reset, radio is in low-power "off" state
   _mode = RADIO_OFF;
   //Radio's default interrupts
   _interrupts = CTS_MASK;

   //Initialize interrupt pin for normal usage with internal pull-up resistor on.
   //By having the pull-up resistor active, we prevent spurious interrupts if the user
   //has chosen not to connect the radio's interrupt output to the microcontroller's
   //interrupt input.
   #if ARDUINO >= 101
   pinMode(RADIO_INT_PIN, INPUT_PULLUP);
   #else
   pinMode(RADIO_INT_PIN, INPUT);
   digitalWrite(RADIO_INT_PIN, HIGH);
   #endif
   //Set external interrupt's mode to trigger on trailing edge of interrupt pulse.
   /* It is possible for two or more interrupts to occur at about the same time,
    * resulting in only one detectable pulse.  To make certain that we get all the
    * interrupt sources that caused the pulse, we trigger at the end (or rising edge)
    * of the active-low pulse.
    */
 #ifdef __AVR__
   /* AVR based Arduinos */
   if(RADIO_EXT_INT<4){
      EICRA |= RISING<<(RADIO_EXT_INT*2);
   }else{
      //Check for Mega or Leonardo
      #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega32U4__)
      EICRB |= RISING<<((RADIO_EXT_INT-4)*2);
      #endif
   }
 #else
   /* ARM based Arduinos - does not use RADIO_EXT_INT */
   //Install interrupt handler
   attachInterrupt(RADIO_INT_PIN, interrupt_handler, RISING);
 #endif
}

// Removes power from radio.  Must call begin() to restart radio.
void Si4735::end(){
   //Note: Removing power below may not actually kill the radio!  This is because
   //the output will not go all the way to 0V, causing a small leakage current to
   //flow from the output.  Also, the radio needs very little power to function.
   //Therefore, we first send a POWER_DOWN command via setMode().
   setMode(RADIO_OFF);
   //Remove power from radio
   digitalWrite(RADIO_POWER_PIN, LOW);
 #ifndef __AVR__
   /* ARM based Arduinos */
   //Remove interrupt handler
   detachInterrupt(RADIO_INT_PIN);
 #endif
}

// Return radio's current mode
byte Si4735::getMode(){
   return _mode;
}

// Change radio's mode.  Gives new function and audio modes and options for those modes.
// Shield users must ensure that the antenna switch on the shield is configured for the desired mode.
void Si4735::setMode(byte new_mode, byte options, byte audio_mode){
   //If mode is not changing, do nothing and return
   byte old_mode=_mode;
   if(new_mode==old_mode) return;

   //Set radio's new mode
   _mode = new_mode;
   //rds == true if caller wants to use RDS
   bool rds = !(options & MODE_FM_OPT_NO_RDS);

   //Note: Because AM/SW/LW all use the same mode in the radio (they only differ
   //in frequency and antenna switch setting), we don't power down when switching
   //between these modes.
   switch(new_mode){
   case AM:
   case SW:
   case LW:
      //New mode is an AM band
      switch(old_mode){
      case AM:
      case SW:
      case LW:
         //Old mode is also an AM band - Do not power down/up!
         goto initialize_mode;
         break;
      }
      break;
   }

   //Power down only if currently powered up
   if(old_mode != RADIO_OFF){
      //Send POWER_DOWN command
      static const byte PROGMEM POWER_DOWN[]={CMD_POWER_DOWN};
      sendCommand_P(POWER_DOWN, sizeof(POWER_DOWN));
   }

   if(new_mode != RADIO_OFF){
      /* Power up and init radio */
      //Default ARG1 settings
      byte arg1;
      if(options & MODE_OPT_NO_XTAL){
         //Enable: Interrupt pin
         arg1 = POWER_UP_ARG1_GPO2OEN;
      }else{
         //Enable: Interrupt pin, crystal oscillator
         arg1 = POWER_UP_ARG1_GPO2OEN | POWER_UP_ARG1_XOSCEN;
      }
      //Build POWER_UP command
      _buffer[0] = CMD_POWER_UP;
      if(new_mode==FM){
         arg1 |= POWER_UP_ARG1_FUNC_FM;
      }else{  //AM, SW, LW
         arg1 |= POWER_UP_ARG1_FUNC_AM;
      }
      _buffer[1] = arg1;
      _buffer[2] = audio_mode;
      //Send POWER_UP command
      sendCommand(_buffer, 3);

      //Restore volume to the current value.
      setVolume(_volume);
      //After POWER_UP command, the radio has mute off.
      //Check if mute should be on.
      if(_mute){
         mute();
      }

      //Enable interrupts for RDS (FM only), STC, and RSQ
      word int_mask;  //Interrupts to enable
      if(new_mode==FM && rds){
         int_mask = STC_MASK | RSQ_MASK | RDS_MASK;
      }else{  //AM, SW, LW, and FM without RDS
         int_mask = STC_MASK | RSQ_MASK;
      }
      setProperty(PROP_GPO_IEN, int_mask);

      //Get radio's revision info
      static const byte PROGMEM GET_REV[]={CMD_GET_REV};
      sendCommand_P(GET_REV, sizeof(GET_REV));
      byte rev_buffer[9];
      getResponse(rev_buffer, sizeof(rev_buffer));
      //Save radio's revision info
      revision.partNumber    =rev_buffer[1];
      revision.firmwareMajor =rev_buffer[2];
      revision.firmwareMinor =rev_buffer[3];
      revision.componentMajor=rev_buffer[6];
      revision.componentMinor=rev_buffer[7];
      revision.chip          =rev_buffer[8];

      initialize_mode:

      //Do mode specific initialization
      word bottom, top, spacing;  //Band limits and spacing
      if(new_mode==FM){
         //All current Si47xx chips with a "D60" suffix have a firmware bug in FM mode
         //which causes noise in the audio output.  Set hidden property to correct bug.
         //See "Si47xx Programming Guide," rev 0.8, Appendix B "Si4704/05/3x-B20/-C40/-D60
         //Compatibility Checklist," page 317.
         if(revision.chip=='D' && revision.firmwareMajor=='6' && revision.firmwareMinor=='0'){
            setProperty(0xFF00, 0);
         }

         if(rds){
            //Enable RDS
            /* The A block always contains the same data (PI) and is not required to
             * decode the rest of the group.  Therefore, we permit it to be damaged.
             * Other blocks must be received perfectly or be correctable.
             */
            setProperty(PROP_FM_RDS_CONFIG, (FM_RDS_CONFIG_ARG_ENABLE |
             FM_RDS_CONFIG_ARG_BLOCK_A_UNCORRECTABLE |
             FM_RDS_CONFIG_ARG_BLOCK_B_5_BIT_ERRORS |
             FM_RDS_CONFIG_ARG_BLOCK_C_5_BIT_ERRORS |
             FM_RDS_CONFIG_ARG_BLOCK_D_5_BIT_ERRORS) );
            //Enable RDS interrupt sources
            //Generate interrupt when new data arrives and when RDS sync is gained or lost.
            setProperty(PROP_FM_RDS_INT_SOURCE, (RDS_RECEIVED_MASK |
             RDS_SYNC_FOUND_MASK | RDS_SYNC_LOST_MASK) );
         }

         /* Manual gives maximum FM range of radio as 64-108 MHz.
          * Radio chip defaults to 87.5-107.9 MHz, 100 kHz spacing.
          * Wikipedia:
          * • North America:  88.1-107.9 MHz, 200 kHz spacing
          *   Note: Most analog receivers in US and NA go down to 87.5 MHz.
          * • Most countries: 87.5-108 MHz, 100 or 200 kHz spacing
          * • Region 2 (North & South America) uses 200 kHz spacing with odd numbered frequencies.
          *   Examples: 88.1, 100.3
          * • Italy uses 50 kHz spacing.
          * • Some former Eastern Bloc countries also use 65-74 MHz but this band is disappearing.
          * • Japan uses 76-90 MHz only.
          * • North American and South Korea use an FM de-emphasis of 75 μs.
          *   Everywhere else uses 50 μs.
          */
         //Default band should work in most countries
         bottom =  8750;
         top    = 10800;
         //Configure regions
         if(_region==REGION_2_NA || _region==REGION_2_SA){
            //Set region 2 (North & South America) frequency spacing to 200 kHz.
            spacing = 20;
            //With 200 kHz spacing, 107.9 MHz is maximum possible frequency.
            top    = 10790;
         }else{  // Regions 1 & 3
            //Regions 1 & 3 use spacing of 100 kHz for increased compatibility.
            spacing = 10;  //100 kHz
         }
         //Configure locales
         switch(_locale){
         case LOCALE_JP:
            //Setup Japan's FM band
            bottom = 7600;
            top    = 9000;
            break;
         case LOCALE_IT:
            //Set Italy's FM spacing to 50 kHz.
            spacing = 5;
            break;
         }
         //Check if caller wants to override locale and force full FM band (64-108 MHz),
         //100 kHz spacing.
         if(options & MODE_FM_OPT_FULL_BAND){
            bottom  =  6400;
            top     = 10800;
            spacing = 10;  //100 kHz
         }
         //Setup FM band and spacing
         setProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
         setProperty(PROP_FM_SEEK_BAND_TOP, top);
         setProperty(PROP_FM_SEEK_FREQ_SPACING, spacing);
         //North America and South Korea use default FM de-emphasis of 75 μs.
         //All others use 50 μs.
         if(_region!=REGION_2_NA && _locale!=LOCALE_KR){
            setProperty(PROP_FM_DEEMPHASIS, FM_DEEMPHASIS_ARG_50);
         }
      }else{  //AM, SW, LW
         //Manual gives maximum AM range of radio as 149-23000 kHz.
         switch(new_mode){
         case AM:
            /* Manual recommends setting band to 520-1710 kHz, 10 kHz spacing for Region 2,
             * 9 kHz spacing for Regions 1 & 3.
             * Note: Chips in the Si47xx family that do not support SW or LW are limited
             * to 520-1710 kHz.
             * Wikipedia, "AM broadcasting" and "AM expanded band":
             * • Region 2 (North & South America):
             *     Old band: 530-1610 kHz
             *     New band: 530-1700 kHz
             *     Spacing:  10 kHz
             * • Regions 1 & 3 (All others):
             *     Current band: 531-1611 kHz
             *     Future band:  531-1701 kHz
             *     Spacing:      9 kHz
             * Note: Wikipedia says the Region 2 AM band begins at 540 kHz.  However,
             * the USA does use 530 kHz for Travelers' Information Stations.  It is
             * possible that 530 kHz is only used in the USA.
             */
            //Note: It is customary for receivers to support an extra channel at the
            //beginning and end of the AM band, even though they are rarely used.
            if(_region==REGION_2_NA || _region==REGION_2_SA){
               bottom  = 520;  //520 is lowest supported frequency on radios without SW/LW
               top     = 1710;  //1710 is highest supported frequency on radios without SW/LW
               spacing = 10;
            }else{  //Regions 1 & 3
               //In this area, stations above 1611 kHz are currently unlicensed "hobby" stations.
               bottom  = 531-9;
               top     = 1701+9;
               spacing = 9;
            }
            break;
         case SW:  //SW uses FM antenna
            /* Manual recommends setting band to 2300-23000 kHz, 5 kHz spacing.
             * Wikipedia: 1800-30000 kHz, 5 kHz spacing.
             */
            //Note: The top AM band frequency (1700 or 1701 kHz) occupies space at 1700±5 kHz
            //or 1701±4.5 kHz.  SW band frequencies use ±2.5 kHz of space.
            bottom  = 1710;   //Start at top of AM band
            top     = 23000;  //Radio's highest supported frequency
            spacing = 5;
            break;
         case LW:
            /* Manual recommends setting band to 153-279 kHz, 9 kHz spacing.
             * Wikipedia:
             * • All major transmitters are in Region 1, 153-279 kHz, 9 kHz spacing.
             * • USA: 160-190 kHz, Part 15 LowFER amateur and experimental stations.
             *        190–535 kHz, non-directional beacon (NDB).
             */
            if(_region==REGION_1){
               //Setup band for Europe's major stations: 153-279 kHz, 9 kHz spacing
               bottom  = 153;
               top     = 279;
               spacing = 9;
            }else{  //Regions 2 & 3
               //Regions 2 & 3 do not have major stations.  Just experimental and beacons.
               //Because users in these regions will just be experimenting, we give full
               //access to the band: 149-535 kHz at 1 kHz spacing.
               bottom  = 149;  //Radio's lowest supported frequency
               top     = 535;  //Stop at top of NDB band, approximate bottom of AM band
               spacing = 1;
            }
            break;
         }
         //Setup AM band and spacing
         setProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
         setProperty(PROP_AM_SEEK_BAND_TOP, top);
         setProperty(PROP_AM_SEEK_FREQ_SPACING, spacing);
      }
      //Save band and spacing
      _bottom=bottom;
      _top=top;
      _spacing=spacing;
   }
   //Frequency unknown
   _frequency=0;
}

// Set top of receive band.
void Si4735::setBandTop(word top){
   _top=top;
   if(_mode==FM){
      setProperty(PROP_FM_SEEK_BAND_TOP, top);
   }else{  //AM, SW, LW
      setProperty(PROP_AM_SEEK_BAND_TOP, top);
   }
}

// Set bottom of receive band.
void Si4735::setBandBottom(word bottom){
   _bottom=bottom;
   if(_mode==FM){
      setProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
   }else{  //AM, SW, LW
      setProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
   }
}

// Set frequency spacing.
void Si4735::setSpacing(word spacing){
   _spacing=spacing;
   if(_mode==FM){
      setProperty(PROP_FM_SEEK_FREQ_SPACING, spacing);
   }else{  //AM, SW, LW
      setProperty(PROP_AM_SEEK_FREQ_SPACING, spacing);
   }
}

// Return top of receive band.
word Si4735::getBandTop(){
   return _top;
}

// Return bottom of receive band.
word Si4735::getBandBottom(){
   return _bottom;
}

// Return frequency spacing.
word Si4735::getSpacing(){
   return _spacing;
}

// Set region and locale.  Probably should only be called while radio's mode==RADIO_OFF.
void Si4735::setRegionAndLocale(byte region, byte locale){
   _region=region;
   _locale=locale;
}

// Return region.
byte Si4735::getRegion(){
   return _region;
}

// Return locale.
byte Si4735::getLocale(){
   return _locale;
}

/******************************************************************************
*   Change frequency                                                          *
******************************************************************************/

// Set radio to given frequency and clear STC interrupt.  Frequency is measured in
// kHz for AM, SW, LW and in 10 kHz increments for FM.
// Should be followed by a call to waitSTC() or equivalent.
// Note: tuneFrequency() does not automatically wait for the STC interrupt to make
// this library more flexible when used with boards not using the Si4735's internal
// oscillator.  In this case, bus traffic while tuning is OK.
void Si4735::tuneFrequency(word frequency){
   //Force new frequency into current band
   frequency=constrain(frequency, _bottom, _top);
   //Save new frequency
   _frequency=frequency;
   //Split the desired frequency into two bytes for use with the set frequency command.
   byte highByte = frequency >> 8;
   byte lowByte = frequency & 0x00FF;

   //Depending on the current mode, set the new frequency (TUNE_FREQ) (and clear STC interrupt).
   //Build command
   _buffer[0]=CMD_AM_TUNE_FREQ;
   _buffer[1]=0x00;
   _buffer[2]=highByte;
   _buffer[3]=lowByte;
   _buffer[4]=0x00;
   _buffer[5]=0x00;  //Note: ARG5 ignored by FM_TUNE_FREQ
   switch(_mode){
   case FM:
      _buffer[0]=CMD_FM_TUNE_FREQ;
      break;
   case SW:
      _buffer[5]=0x01;
      break;
   }
   //Send TUNE_FREQ command
   sendCommand(_buffer, 6);

   //Clear local STC interrupt and RDS info
   clearInterrupts(STC_MASK);
   clearStationInfo();
}

// Set radio's frequency and then wait for tuning to complete.  Frequency is measured
// in kHz for AM, SW, LW and in 10 kHz increments for FM.
void Si4735::tuneFrequencyAndWait(word frequency){
   tuneFrequency(frequency);
   waitSTC();
}

// Increments the currently tuned frequency.  The new frequency wraps to the bottom
// if it would exceed the top of band.  Returns the newly tuned frequency.
word Si4735::frequencyUp(void){
   word frequency=_frequency;
   //Check if current frequency is 0 (unknown)
   if(!frequency){
      //Default to top
      frequency=_top;
   }else{
      //Increment frequency
      frequency += _spacing;
      //Check if top of band reached
      if(frequency > _top){
         //Wrap to bottom of band
         frequency=_bottom;
      }
   }
   //Set and return new frequency
   tuneFrequency(frequency);
   return frequency;
}

// Decrements the currently tuned frequency.  The new frequency wraps to the top
// if it would exceed the bottom of band.  Returns the newly tuned frequency.
word Si4735::frequencyDown(void){
   word frequency=_frequency;
   //Check if current frequency is 0 (unknown)
   if(!frequency){
      //Default to bottom
      frequency=_bottom;
   }else{
      //Decrement frequency
      frequency -= _spacing;
      //Check if bottom of band reached
      if(frequency < _bottom){
         //Wrap to top of band
         frequency=_top;
      }
   }
   //Set and return new frequency
   tuneFrequency(frequency);
   return frequency;
}

// Increments the currently tuned frequency and then waits for tuning to complete.
// The new frequency wraps to the bottom if it would exceed the top of band.
// Returns the newly tuned frequency.
word Si4735::frequencyUpAndWait(void){
   //Increment frequency
   frequencyUp();
   //Wait until STC received
   waitSTC();
   //Return new frequency
   return _frequency;
}

// Decrements the currently tuned frequency and then waits for tuning to complete.
// The new frequency wraps to the top if it would exceed the bottom of band.
// Returns the newly tuned frequency.
word Si4735::frequencyDownAndWait(void){
   //Decrement frequency
   frequencyDown();
   //Wait until STC received
   waitSTC();
   //Return new frequency
   return _frequency;
}

// Wait for Seek/Tune Complete (STC).
void Si4735::waitSTC(){
   //Wait until STC received
   while( !(currentInterrupts() & STC_MASK) );
}

// Do SEEK_START command.
// ***** PRIVATE *****
void Si4735::seek_start(byte arg){
   //Build command
   _buffer[0]=CMD_AM_SEEK_START;
   _buffer[1]=arg;
   _buffer[2]=0x00;  //Note: ARG 2-5 ignored by FM_SEEK_START
   _buffer[3]=0x00;
   _buffer[4]=0x00;
   _buffer[5]=0x00;
   switch(_mode){
   case FM:
      _buffer[0]=CMD_FM_SEEK_START;
      break;
   case SW:
      _buffer[5]=0x01;
      break;
   }
   //Send SEEK_START command
   sendCommand(_buffer, 6);

   //Clear local STC interrupt and RDS info
   clearInterrupts(STC_MASK);
   clearStationInfo();
   //Frequency unknown
   _frequency=0;
}

// Seek up and clear STC interrupt
void Si4735::seekUp(){
   seek_start(SEEK_START_UP);
}

// Seek down and clear STC interrupt
void Si4735::seekDown(){
   seek_start(SEEK_START_DOWN);
}

// Do TUNE_STATUS command.
// Returns radio's current frequency.
// ***** PRIVATE *****
word Si4735::tune_status(byte arg){
   //Set TUNE_STATUS command
   if(_mode==FM){
      _buffer[0]=CMD_FM_TUNE_STATUS;
   }else{  //AM, SW, LW
      _buffer[0]=CMD_AM_TUNE_STATUS;
   }
   //Set argument
   _buffer[1]=arg;

   //Send TUNE_STATUS command
   sendCommand(_buffer, 2);
   //Clear local STC interrupt, if required
   if(arg | TUNE_STATUS_CLEAR_STC){
      clearInterrupts(STC_MASK);
   }
   //Now read the response
   getResponse(_buffer, 4);

   //Convert frequency high and low bytes into word and save. Then return current frequency.
   _frequency = MAKE_WORD(_buffer[2], _buffer[3]);
   return _frequency;
}

// Tell radio to cancel seek operation.  Returns radio's current frequency.
// Clears STC interrupt.
word Si4735::cancelSeek(){
   //Tell radio to cancel seek and clear STC.  Then return current frequency.
   return tune_status(TUNE_STATUS_CANCEL_SEEK | TUNE_STATUS_CLEAR_STC);
}

// Check if STC interrupt received.  If found, returns the radio's current frequency
// and clears STC interrupt.  Otherwise, returns 0.
// Frequency is measured in kHz for AM, SW, LW and in 10 kHz increments for FM.
word Si4735::checkFrequency(){
   //Check for Seek/Tune Complete (STC) interrupt.
   if( !(currentInterrupts() & STC_MASK) ){
      return 0;  //STC not yet received
   }
   //STC received - Return current frequency and clear STC
   return tune_status(TUNE_STATUS_CLEAR_STC);
}

// Ask radio and return its current frequency.  Clears STC interrupt if clearSTC argument
// is true.  Frequency is measured in kHz for AM, SW, LW and in 10 kHz increments for FM.
word Si4735::getFrequency(bool clearSTC){
   return tune_status(clearSTC ? TUNE_STATUS_CLEAR_STC : 0);
}

// Return radio's current frequency.  Frequency is measured in kHz for AM, SW, LW
// and in 10 kHz increments for FM.
word Si4735::currentFrequency(){
   return _frequency;
}

/******************************************************************************
*   RSQ status                                                                *
******************************************************************************/

// Get Received Signal Quality (RSQ) information and save in given RSQMetrics structure
// if RSQ interrupt received.  Also clears RSQ interrupt if RSQ data was read.
// Returns true if data written to RSQMetrics structure or false if RSQ interrupt not set.
bool Si4735::checkRSQ(RSQMetrics *RSQ){
   //Check if radio has new RSQ data for us
   if(currentInterrupts() & RSQ_MASK){
      //Get RSQ data
      getRSQ(RSQ);
      debug(print,"RSQ true: ");
      debug(println,_interrupts,HEX);
      return true;
   }else{
      debug(print,"RSQ false: ");
      debug(println,_interrupts,HEX);
   }
   //No RSQ data available
   return false;
}

// Get Received Signal Quality (RSQ) information and save in given RSQMetrics structure.
// Also clears RSQ interrupt.
void Si4735::getRSQ(RSQMetrics *RSQ){
   //RSQ status and clear RSQ interrupt
   static const byte PROGMEM FM_RSQ_STATUS[]={CMD_FM_RSQ_STATUS, RSQ_STATUS_ARG1_CLEAR_INT};
   static const byte PROGMEM AM_RSQ_STATUS[]={CMD_AM_RSQ_STATUS, RSQ_STATUS_ARG1_CLEAR_INT};
   const byte PROGMEM *command;  //Command to send

   //Select Received Signal Quality command
   if(_mode==FM){
      command=FM_RSQ_STATUS;
   }else{  //AM, SW, LW
      command=AM_RSQ_STATUS;
   }
   //Send RSQ_STATUS command
   sendCommand_P(command, 2);
   //Clear local RSQ interrupt
   clearInterrupts(RSQ_MASK);
   //Now read the response
   getResponse(_buffer, 8);

   //Copy the response data into their respective fields
   RSQ->RSSI=_buffer[4];
   RSQ->SNR=_buffer[5];
   RSQ->interrupts=_buffer[1];
   RSQ->seekable =_buffer[2] & FIELD_RSQ_STATUS_RESP2_SEEKABLE;
   RSQ->AFCRailed=_buffer[2] & FIELD_RSQ_STATUS_RESP2_AFC_RAILED;
   RSQ->softMute =_buffer[2] & FIELD_RSQ_STATUS_RESP2_SOFT_MUTE;
   if(_mode==FM){
      RSQ->stereo=_buffer[3] & FIELD_RSQ_STATUS_RESP3_STEREO;
      RSQ->stereoBlend=_buffer[3] & FIELD_RSQ_STATUS_RESP3_STEREO_BLEND;
      RSQ->multipath=_buffer[6];
      RSQ->freqOffset=_buffer[7];
   }else{  //AM, SW, LW
      RSQ->stereo=false;
      RSQ->stereoBlend=0;  //Full mono
      RSQ->multipath=0;
      RSQ->freqOffset=0;
   }
}

/******************************************************************************
*   Volume                                                                    *
******************************************************************************/

// Set radio's volume.
// ***** PRIVATE *****
void Si4735::set_volume(){
   //Set volume if radio on
   if(_mode!=RADIO_OFF){
      //Set the volume to the current value.
      setProperty(PROP_RX_VOLUME, _volume);
   }
}

// Volume up by number given.  Return new volume.
byte Si4735::volumeUp(byte inc){
   //Check if top reached
   if(_volume+inc <= MAX_VOLUME){
      _volume += inc;
   }else{
      _volume = MAX_VOLUME;
   }
   //Set volume
   set_volume();
   return _volume;
}

// Volume down by number given.  Return new volume.
byte Si4735::volumeDown(byte dec){
   //Check if bottom reached
   if(_volume < dec){
      _volume = 0;
   }else{
      _volume -= dec;
   }
   //Set volume
   set_volume();
   return _volume;
}

// Set volume.  Return new volume.
byte Si4735::setVolume(byte new_volume){
   //Check if new volume is legal
   if(0 <= new_volume && new_volume <= MAX_VOLUME){
      //Save it
      _volume = new_volume;
      //Set volume
      set_volume();
   }
   return _volume;
}

// Return current volume.  Note that volume is independent of mute's status.
byte Si4735::getVolume(){
   return _volume;
}

// Mute radio.
void Si4735::mute(){
   //Mute if radio on
   if(_mode!=RADIO_OFF){
      setProperty(PROP_RX_HARD_MUTE, 0b11);  //Mute left and right
   }
   _mute=true;  //currently muted
}

// Disable mute.
void Si4735::unmute(){
   //Unmute if radio on
   if(_mode!=RADIO_OFF){
      setProperty(PROP_RX_HARD_MUTE, 0b00);  //Unmute left and right
   }
   _mute=false;  //currently unmuted
}

// Toggle mute.
bool Si4735::toggleMute(){
   if(_mute){
      unmute();
   }else{
      mute();
   }
   return _mute;
}

// Get mute status.
bool Si4735::getMute(){
   return _mute;
}

/******************************************************************************
*   Send command and get responce or interrupts                               *
******************************************************************************/

// Send command packet.  Maximum length is CMD_MAX_LENGTH bytes.
void Si4735::sendCommand(const byte *command, byte length){
   debug(print,"Command: ");
   debug(print,*command,HEX);
   debug(print,": ");
   /* Note: We do not need to wait for CTS from the previous command because this
    * method waits below until CTS has occured.
    */
   //Check if length too long
   if(length > CMD_MAX_LENGTH) length=CMD_MAX_LENGTH;

 #ifdef Si47xx_SPI

   //Select radio on SPI bus.  SS has 15 ns setup time before clock starts.
   digitalWrite(RADIO_SPI_SS_PIN, LOW);

   //Control byte to write a command
   SPI.transfer(0x48);
   //We now send 8 bytes
   byte i;  //Loop variable
   for(i=0; i<length; i++) SPI.transfer(command[i]);
   //Radio requires we write exactly 8 bytes in SPI mode.
   //Pad the end of packet with 0.
   for(; i<CMD_MAX_LENGTH; i++) SPI.transfer(0x00);

   //Deselect radio on SPI bus.  SS has 5 ns hold time after clock ends.
   digitalWrite(RADIO_SPI_SS_PIN, HIGH);

 #else  //I2C

   //Start I2C packet
   Wire.beginTransmission(_address);
   //Send command
   Wire.write(command, length);
   //Finish I2C packet
   Wire.endTransmission();

 #endif

   //Wait for CTS
   /* All commands take 300 µs for CTS except POWER_UP which takes 110 ms. */
   if(command[0]!=CMD_POWER_UP){
      delayMicroseconds(300);
   }else{
      delay(110);  //POWER_UP
   }
   debug(println,"Command done");
}

#ifdef __AVR__
// Send command packet.  Maximum length is 8 bytes.
// Command given must be located in flash ROM, not SRAM.  Otherwise, equivalent to sendCommand().
void Si4735::sendCommand_P(const byte PROGMEM *command_P, byte length){
   //Check if length too long for buffer
   if(length > sizeof(_buffer)) length=sizeof(_buffer);
   //Copy flash ROM based command to SRAM
   memcpy_P(_buffer, command_P, length);
   //Send command
   sendCommand(_buffer, length);
}
#endif

// Get 16 byte response.  Only those bytes that will fit are written to the buffer.
void Si4735::getResponse(byte *response, byte length){
   debug(print,"Responce: ");
   /* Note: We do not need to wait for CTS because sendCommand() does not return
    * until CTS has occured.
    */
   //Check if length too long
   if(length > RESP_MAX_LENGTH) length=RESP_MAX_LENGTH;

 #ifdef Si47xx_SPI

   //Select radio on SPI bus.  SS has 15 ns setup time before clock starts.
   digitalWrite(RADIO_SPI_SS_PIN, LOW);

   //Control byte to read a long response
   SPI.transfer(0xE0);
   //Store response in caller's buffer.
   byte i;  //Loop variable
   for(i=0; i<length; i++){
      response[i] = SPI.transfer(0x00);
   }
   //Radio requires that we read exactly 16 bytes in SPI mode.
   //Throw out remaining bytes.
   for(; i<RESP_MAX_LENGTH; i++) SPI.transfer(0x00);

   //Deselect radio on SPI bus.  SS has 5 ns hold time after clock ends.
   digitalWrite(RADIO_SPI_SS_PIN, HIGH);

 #else  //I2C

   //Get response
   byte i = Wire.requestFrom(_address, length);
   //Store response in caller's buffer.
   while(i--){
      *response++ = Wire.read();
   }

 #endif
}

// Get single byte status code from radio chip.
byte Si4735::getStatus(){
   byte status;  //Status byte from radio

 #ifdef Si47xx_SPI

   //Select radio on SPI bus.  SS has 15 ns setup time before clock starts.
   digitalWrite(RADIO_SPI_SS_PIN, LOW);

   //Control byte to read single byte status code
   SPI.transfer(0xA0);
   //Get status byte
   status = SPI.transfer(0x00);

   //Deselect radio on SPI bus.  SS has 5 ns hold time after clock ends.
   digitalWrite(RADIO_SPI_SS_PIN, HIGH);

 #else  //I2C

   //Get status byte
   //Note: Wire class has two methodes for requestFrom(), one for byte args and
   //one for int args.  Call the byte method for efficiency.
   Wire.requestFrom(_address, byte(1));  //cast into a 'byte' for efficiency
   status = Wire.read();

 #endif

   return status;
}

// Get radio's interrupts by calling GET_INT_STATUS command.
byte Si4735::getInterrupts(){
   //Send GET_INT_STATUS command
   static const byte PROGMEM GET_INT_STATUS[]={CMD_GET_INT_STATUS};
   sendCommand_P(GET_INT_STATUS, sizeof(GET_INT_STATUS));
   //Get new interrupt status
   _interrupts=getStatus();
   //Return interrupts
   return _interrupts;
}

// Returns current interrupt byte.  If an interrupt signal has been received, the new interrupt
// byte is read and returned.  Otherwise returns previous interrupt byte returned by radio.
byte Si4735::currentInterrupts(){
   //Check for interrupt signal
 #ifdef __AVR__
   if(EIFR & (1<<RADIO_EXT_INT)){
      //Clear AVR's interrupt flag
      EIFR = 1<<RADIO_EXT_INT;
 #else
   if(interrupt_signal){
      //Clear interrupt signal flag
      interrupt_signal=false;
 #endif
      //Get new interrupt status
      getInterrupts();
      debug(print,"Int: ");
      debug(println,_interrupts,HEX);
   }
   //Return current interrupts
   return _interrupts;
}

// Clears the given interrupt masks.
void Si4735::clearInterrupts(byte interrupt_mask){
   //Clear given interrupts
   _interrupts &= ~interrupt_mask;
}

// Set given property.
void Si4735::setProperty(word property, word value){
   _buffer[0] = CMD_SET_PROPERTY;
   _buffer[1] = 0;
   //Property to set
   _buffer[2] = property >> 8;
   _buffer[3] = property;
   //Property's new value
   _buffer[4] = value >> 8;
   _buffer[5] = value;
   sendCommand(_buffer, 6);
}

// Get given property.
word Si4735::getProperty(word property){
   _buffer[0] = CMD_GET_PROPERTY;
   _buffer[1] = 0;
   //Property to get
   _buffer[2] = property >> 8;
   _buffer[3] = property;
   sendCommand(_buffer, 4);

   //Get property's value
   getResponse(_buffer, 4);
   return MAKE_WORD(_buffer[2], _buffer[3]);
}

// Converts hexadecimal ASCII string into command packet and sends it to the radio.
// For debugging or advanced users.
// ASCII chars in input string must be hexadecimal or random data will be sent to radio.
void Si4735::sendCommand(const char *myCommand){
   unsigned char digit;  //next input digit
   byte tempValue;  //build next output char
   byte index=0;  //location in output buffer

   //Convert the ASCII string to a binary string
   //Check for end of input string or end of output buffer
   while(digit=*myCommand++ && index<sizeof(_buffer)){
      //First digit
      if(digit > '9'){
         tempValue = toupper(digit)-'A'+10;
      }else{
         tempValue = digit-'0';
      }
      _buffer[index] = tempValue * 16;
      //Second digit
      if((digit=*myCommand++)=='\0') break;  // stop if second digit missing
      if(digit > '9'){
         tempValue = toupper(digit)-'A'+10;
      }else{
         tempValue = digit-'0';
      }
      _buffer[index++] += tempValue;
   }
   //Send converted command packet to the radio using low-level version of sendCommand()
   sendCommand(_buffer, index);
}

