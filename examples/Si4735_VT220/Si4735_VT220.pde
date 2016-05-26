/* AM (including SW, MW, and LW) and FM radio receiver based on the Si4735.
 * Requires Si4735 library.  Program written by Michael J. Kennedy.
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
 * User interface is through a VT100, VT220, VT320, or "ANSI" style terminal
 * program using UTF-8 characters.  This permits using the radio's advanced
 * features such as RDS without requiring additional hardware such as an LCD.
 * Please note that the "Serial Monitor" built into the Arduino's development
 * software cannot be used to communicate with this example program.  You must
 * use terminal software capable of talking to a serial port.  For Windows XP
 * you can try the built-in HyperTerminal.  For Linux and Windows you can
 * use PuTTY (http://www.chiark.greenend.org.uk/~sgtatham/putty/) with
 * "Connection type" set to "Serial".  On Linux, I have used GtkTerm and PuTTY.
 * The best option for MacOS X is probably ZTerm (http://www.dalverson.com/zterm/).
 * http://en.wikipedia.org/wiki/List_of_terminal_emulators gives a list of
 * terminal software, however, many of these programs cannot access a serial port.
 * Terminal settings:
 *   Emulation: VT100, VT220, VT320, ANSI, or similar
 *   Character set: UTF-8 (if available)
 *   Baud (BPS): 9600
 *   Data bits: 8
 *   Parity: No
 *   Stop bits: 1
 * ATTENTION: You must either quit the terminal software or get it to release
 * the Arduino's serial port whenever you want to reprogram the Arduino.
 *
 * Hardware:
 * • Arduino
 * • Si4735 Arduino shield from SparkFun Electronics.  Provides Si4735-C40 chip.
 *   Requires some modification.  Or use SparkFun's Si4735 breakout board.
 * • AM/LW ferrite loop antenna between 180 and 450 μH.
 * • FM/SW ~56 cm whip antenna.  (Or just a 56 cm vertical length of wire.)
 *
 * The Si4735-C40 chip includes many features:
 * • 3 AM bands: Short wave, medium wave (common AM service), and long wave.
 * • FM stereo.
 * • FM RDS/RBDS (IEC-62106).  This gives information such as the current time,
 *   station ID, and the audio's content.
 * • Stereo analog audio output.
 * • Stereo digital audio output.  (Feature not usable with SparkFun's Arduino
 *   shield.  You must use SparkFun's breakout board for digital audio output.)
 * • SPI or I2C interface.  (SparkFun's Arduino shield is setup for SPI only.)
 *
 * For more information on the Silicon Labs Si4735-C40 see the following
 * documents:
 * • Data sheet: Si4734/35-C40 - Gives hardware description.
 * • Application Note: AN332: "Si47xx Programming Guide" - Explains the Si4735
 *   (and similar chips) from a software perspective.
 * • Application Note: AN383: "Si47xx Antenna, Schematic, Layout, and Design
 *   Guidelines" - Gives information on designing antennas for the radio.
 *   Please note that the shield is based on the design given in section 10
 *   "Whip Antenna for SW Receive on AMI" (page 46 in Rev. 0.6.)
 *
 * The SparkFun shield requires modification to work correctly.  See the
 * included "README" file.
 * Note: Despite what is printed on the shield, the switch has these two modes:
 * AM/FM/LW and SW.  As a result, you should leave the switch set to AM for both
 * AM and FM reception.
 */

#include "Si4735.h"  //Si4735 Library

// Note: The Arduino developement software has a design flaw.  If these libraries
// are not included here, in your application, the Si4735 Library will not be able
// to find them later.  Also, you should comment out any libraries not needed.
// Otherwise, they will waste memory.
//#include "SPI.h"  //SPI class needed by Si4735 Library when using the SPI bus
#include "Wire.h"  //Wire class needed by Si4735 Library when using the I2C bus
//#include "SoftwareSerial.h"  //SoftwareSerial only needed for debugging messages

//control chars and sequences
#define ESC_CHAR '\x1B'
#define CSI_CHAR '\x9B'  //single char version of CSI
#define CSI "\x1B["    //two char version of CSI
#define HOME CSI"H"
#define POSX(x) CSI #x "G"
#define POS(x,y) CSI #y ";" #x "H"
#define EL CSI "K"  //erase to end of line
#define ERASE_TO_END_OF_SCREEN CSI "J"
#define BOLD CSI "1m"
#define REVERSE CSI "7m"
#define NORMAL CSI "m"
#define HIDE_CURSOR CSI "?25l"

//Create an instance of the Si4735 class named radio.
Si4735 radio;

//Current station for each band.  Values are in kHz except FM which is in 10 kHz increments.
//Initialize these variables to your power-on default preference.
word fm_station=10030;  // * 10 kHz
word am_station=890;
word sw_station=2300;
word lw_station=153;
word *current_station;  //points to current station variable for current band
//Presets.  0 means no entry.
word FM_preset_stations[10]={0,9090,9310,9390,9470,9710,9790,10030,0,10430};
word AM_preset_stations[10]={890};

//Delay in milliseconds until next display update.
#define UPDATE_SHORT_DELAY 1
#define UPDATE_DELAY 400



/* The Timer class times an event with a given length.  An alternative to Arduino's delay(). */
/* To use, first call setTimer() with the length of time to wait.  Next you
 * should poll checkTimer() which returns true once the length of time given to
 * setTimer() has elapsed.  You may reset the timer event at any time by calling
 * setTimer() again.
 */
class Timer {
public:
   void setTimer(unsigned long length);
   bool checkTimer();
private:
   unsigned long start;  //moment timing began
   unsigned long end;    //when event has finished
   bool wrap;            //true if millis() timer will wrap around
};

/* Begin timing.  The length to wait is given in milliseconds (ms). */
/* Because this class works by having the caller poll checkTimer(), it is
 * important that the length of the timer event is not too long, relative to the
 * frequency of polling.  Otherwise, the end of the event might be missed.
 * Here's an example.  If we set the length to (ULONG_MAX-10) then we will only
 * have a 10 ms window to catch this event by calling checkTimer().
 */
void Timer::setTimer(unsigned long length){
   //save current time in milliseconds
   start=millis();
   //compute end of timing event
   end=start+length;
   //check for wrap around
   if(start<=end){
      wrap=false;
   }else{
      wrap=true;
   }
}

/* Returns true if timer event set with setTimer() has elapsed. */
bool Timer::checkTimer(){
   //get current time
   unsigned long time=millis();
   //check if event has finished
   if(wrap){
      if(start<=time || time<end) return false;
   }else{
      if(start<=time && time<end) return false;
   }
   return true;  //done
}

//Create a timer object to measure time between display updates.
Timer displayTimer;



//Convert number to decimal ASCII text.  Output text is placed in buffer
//'number_str'.  Buffer is padded on left with the given pad char.  The default
//pad char is space.
//Returns address of first (leftmost) digit saved in buffer.
char number_str[6];  // 5 digits + null char
char *number_to_text(word num, char pad=' '){
   byte buf;  //current location in output buffer
   byte digit;  //next digit from number
   char *first;  //first digit in buffer

   //begin at end of buffer (points at null char)
   buf = sizeof(number_str)-1;
   if(num){
      //dismantle number
      while(num){
         //get next digit
         digit=num%10;
         num/=10;
         //convert digit to ASCII and write to buffer
         number_str[--buf] = digit+'0';
      }
   }
   else{
      //make sure num==0 will generate output by writing a zero to the buffer
      number_str[--buf]='0';
   }
   //remember location of first digit
   first = number_str+buf;
   //pad buffer with pad char
   while(buf>0){
      number_str[--buf] = pad;
   }
   return first;
}

//Print number to serial port.  The given number of digits will be printed.
//Buffer is padded on left with the given pad char.  The default pad char is space.
//If number of digits is 0, no pad characters will be printed.
void print_number(word num, byte digits=0, char pad=' '){
   char *start;  //first digit of number
   start=number_to_text(num, pad);
   if(digits){
      Serial.write(number_str+5-digits);
   }else{
      Serial.write(start);
   }
}



#define printp(name, str) static const char PROGMEM name[]=str; printpstr(name)

//print flash ROM (program memory) string to serial port.
void printpstr(const char *pstr){
   char ch;  //char from str
   //read next char
   while(ch = pgm_read_byte(pstr++)){
      //write non-null char
      Serial.write(ch);
   }
}

void print_presets(word presets[], byte mode){
   printp(preset_location, POS(1,22)EL);
   for(byte i=0; i<10; i++){
      printp(separate, "      ");
      Serial.write(i+'0');
      printp(colon, ": ");
      //Check if current preset is used
      if(presets[i]){
         //Print frequency
         number_to_text(presets[i]);
         //insert '.' in FM frequency
         if(mode==FM){
            number_str[4]=number_str[3];
            number_str[3]='.';
         }
         Serial.write(number_str);
      }else{
         printp(skip, "     ");
      }
      //check if middle reached
      if(i==4){
         printp(next_line, POS(1,23)EL);
      }
   }
}

void print_mode(){
   //print mode and station frequency units
   printp(pos, POS(10,2));
   switch(radio.getMode()){
   case RADIO_OFF:
      printp(off, "OFF"POS(16,3)"   "POS(45,16)"P  Preferences");
      break;
   case FM:
      printp(fm, "FM "POS(16,3)"MHz"POS(45,16)EL);
      //FM presets
      print_presets(FM_preset_stations, FM);
      return;
      break;
   case AM:
      printp(am, "AM ");
      printp(khz, POS(16,3)"kHz"POS(45,16)EL);
      //AM presets
      print_presets(AM_preset_stations, AM);
      return;
      break;
   case SW:
      printp(sw, "SW ");
      printpstr(khz);
      break;
   case LW:
      printp(lw, "LW ");
      printpstr(khz);
      break;
   }
   //clear preset line
   printp(clear_preset, POS(1,22)EL POS(1,23)EL);
}

void print_station_freq(){
   byte mode;  //current mode

   printp(begin, POS(10,3)BOLD);
   mode=radio.getMode();
   if(mode!=RADIO_OFF){
      number_to_text(*current_station);
      //insert '.' in FM frequency
      if(mode==FM){
         number_str[4]=number_str[3];
         number_str[3]='.';
      }
      Serial.write(number_str);
   }
   else{
      //radio off so clear station field
      printp(clear, "     ");
   }
   printp(norm, NORMAL);
}

void print_volume(){
   printp(begin, POS(10,4)BOLD);
   print_number(radio.getVolume(), 2);
   printp(norm, NORMAL);
}

void print_mute(){
   if(radio.getMute()){
      printp(mute, BOLD POS(17,4)"Muted" NORMAL);
   }else{
      printp(unmute, POS(17,4)"     ");
   }
}

void print_clear_station_info(bool seek=false){
   //station field
   if(seek){
      printp(seek, POS(10,3)"Seek ");  //seek mode
   }else{
      printp(clear_seek, POS(10,3)"     ");  //just clear it
   }
   //clear other fields
   printp(clear,
    POS(2,5)"      "     //stereo/mono flag
    POSX(10)"    "       //stereo blend
    POS(10,6)"    "      //call sign
    REVERSE
    POS(10,7)"        "  //program service
    NORMAL
    POS(10,8)"                "  //program type
    POSX(28)"        "   //program type name
    POS(10,9)"   "       //SNR
    POSX(18)"    "       //"Weak" flag
    POS(10,10)"   "      //RSSI
    POS(10,11)EL         //message
    POS(10,12)EL);       //date/time
}

//home cursor
void print_home(){
   printp(home, HOME);
}

//clear screen and print data field labels
void init_screen(){
   //hide cursor, home cursor, clear screen, then print labels and menu
   printp(labels_menu, HIDE_CURSOR  HOME  ERASE_TO_END_OF_SCREEN
    POS(4,2)"Mode:"
    POS(1,3)"Station:"
    POS(2,4)"Volume:   /63"
    POS(8,5)":"
    POS(4,6)"Call:"
    POS(6,7)"ID: " REVERSE "        " NORMAL
    POS(4,8)"Type:"
    POS(5,9)"SNR:     dB"
    POS(4,10)"RSSI:     dBµV"
    POS(1,11)"Message:"
    POS(4,12)"Time:"
    POS(45,2)"O  Off"
    POS(45,3)"F  FM band"
    POS(45,4)"A  AM band"
    POS(45,5)"L  LW band"
    POS(45,6)"S  SW band (Flip antenna switch)"
    POS(44,7)"<>  Seek (Cursor Left/Right)"
    POS(41,8)"Space  Cancel seek"
    POS(44,9)"[]  Step frequency"
    POS(44,13)"+-  Volume (Cursor Up/Down)"
    POS(45,14)"M  Mute"
    POS(45,15)"R  Refresh screen");
   print_mode();
   print_station_freq();
   print_volume();
   print_mute();
   print_home();
}

void preferences(){
   print_menu:
   //print preferences menu - just locale for now
   static const char PROGMEM menu[] =
      HOME  ERASE_TO_END_OF_SCREEN
      POS(10,4)"Select your location:"
      POS(14,6) BOLD "Region 1 - Europe, Africa, and north-west Asia:" NORMAL
      POS(10,7)"1 - Italy"
      POS(10,8)"2 - Other"
      POS(14,9) BOLD "Region 2, subregion North America:" NORMAL
      POS(10,10)"3 - USA"
      POS(10,11)"4 - Canada, Mexico"
      POS(10,12)"5 - Other"
      POS(14,13) BOLD "Region 2, subregion South America:" NORMAL
      POS(10,14)"6 - All"
      POS(14,15) BOLD "Region 3 - Oceania and south-east Asia:" NORMAL
      POS(10,16)"7 - Japan"
      POS(10,17)"8 - South Korea"
      POS(10,18)"9 - Other"
      POS(10,20)"Enter - Done"
   ;
   printpstr(menu);
   //print marker on current selection
   static const byte PROGMEM locale_pos[] = {7,10,11,16,17};
   static const byte PROGMEM other_pos[] = {8,12,14,18};
   byte pos;
   if((pos=radio.getLocale()) == LOCALE_OTHER){
      pos=pgm_read_byte( &other_pos[radio.getRegion()] );
   }else{
      pos=pgm_read_byte( &locale_pos[pos-1] );  //-1 to skip LOCALE_OTHER
   }
   printp(marker1, CSI);
   print_number(pos, 0, '0');  //row=pos
   printp(marker2, ";8H"BOLD">"NORMAL);  //column=8
   //get and execute command
   int ch;  //char from serial port
   while((ch=Serial.read()) != '\r'){  //quit if Enter key found
      if('1'<=ch && ch <='9'){
         //translate menu item selected to new region & locale and save it
         byte selection = ch-'1';
         static const byte PROGMEM menu_to_region[] = {
            REGION_1, REGION_1,
            REGION_2_NA, REGION_2_NA, REGION_2_NA,
            REGION_2_SA,
            REGION_3, REGION_3, REGION_3
         };
         static const byte PROGMEM menu_to_locale[] = {
            LOCALE_IT, LOCALE_OTHER,
            LOCALE_US, LOCALE_CA_MX, LOCALE_OTHER,
            LOCALE_OTHER,
            LOCALE_JP, LOCALE_KR, LOCALE_OTHER
         };
         radio.setRegionAndLocale(
          pgm_read_byte( &menu_to_region[selection]),
          pgm_read_byte( &menu_to_locale[selection]) );
         //show user new selection
         goto print_menu;
      }
   }
   //reprint main screen
   init_screen();
}

#if DEBUG
SoftwareSerial debugSerial =  SoftwareSerial(DEBUG_RX, DEBUG_TX);
#endif

void setup(){
   //null terminate number to text buffer
   number_str[sizeof(number_str)-1]=0;

   //initialize radio
   radio.begin();

   //create a serial connection to personal computer at 9600 BPS, 8,N,1
   Serial.begin(9600);

   #if DEBUG
   //setup DEBUG serial port
   pinMode(DEBUG_TX, OUTPUT);
   debugSerial.begin(9600);
   debug(println,"\r\n***Debug port***\r\n");
   #endif

   //Serial.write("Type 'go' to begin");
   //wait for 'go' command
   //while(Serial.read()!='g');
   //while(Serial.read()!='o');

   //init screen
   init_screen();
}

void loop(){
   int ch;  //char from serial port

   //get all chars waiting from serial port
   while((ch=Serial.read()) >= 0){
      //execute command
      switch(ch){
      case 'F':  //FM
      case 'f':
         radio.setMode(FM);
         current_station=&fm_station;
         init_new_mode:
         print_mode();
         print_clear_station_info();
         //check if band (mode) has a previous current station
         if(!*current_station){
            //No previous station.  Default to bottom of band.
            *current_station = radio.getBandBottom();
         }
         //go to band's current station
         goto go_to_station;
         break;
      case 'A':  //AM
      case 'a':
         radio.setMode(AM);
         current_station=&am_station;
         goto init_new_mode;
         break;
      case 'S':  //SW
      case 's':
         radio.setMode(SW);
         current_station=&sw_station;
         goto init_new_mode;
         break;
      case 'L':  //LW
      case 'l':
         radio.setMode(LW);
         current_station=&lw_station;
         goto init_new_mode;
         break;
      //radio off
      case 'O':
      case 'o':
         radio.setMode(RADIO_OFF);
         print_mode();
         print_clear_station_info();
         break;
      //reset screen
      case 'R':
      case 'r':
      case ' ':
         init_screen();
         break;
      //preferences
      case 'P':
      case 'p':
         if(radio.getMode()==RADIO_OFF){
            preferences();
         }
         break;
      //volume up
      case '+':
      case '=':
      volume_up:
         radio.volumeUp();
         print_volume();
         break;
      //volume down
      case '-':
      volume_down:
         radio.volumeDown();
         print_volume();
         break;
      //toggle mute
      case 'M':
      case 'm':
         radio.toggleMute();
         print_mute();
         break;
      //2 char cursor keys
      case CSI_CHAR:
         //go handle final char
         goto final_char;
         break;
      //3 char cursor keys
      case ESC_CHAR:
         //wait for next char to arrive
         while((ch=Serial.read()) < 0);
         //check for next char in sequence
         if(ch!='[') break;  //wrong char - abort
         final_char:
         //wait for next char to arrive
         while((ch=Serial.read()) < 0);
         //handle final char in sequence
         switch(ch){
         case 'A':  //cursor up
            goto volume_up;
            break;
         case 'B':  //cursor down
            goto volume_down;
            break;
         default:
            //only execute other commands if radio is on
            if(radio.getMode()!=RADIO_OFF){
               switch(ch){
               case 'C':  //cursor right
                  goto seek_up;
                  break;
               case 'D':  //cursor left
                  goto seek_down;
                  break;
               }
            }
            break;
         }
         break;
      default:
         //only execute other commands if radio is on
         if(radio.getMode()!=RADIO_OFF){
            //execute basic commands
            switch(ch){
            //decrement station
            case '[':
               *current_station=radio.frequencyDown();
               goto new_station;
               break;
            //increment station
            case ']':
               *current_station=radio.frequencyUp();
               goto new_station;
               break;
            //seek down to the next station (wrap to the top when the bottom is reached)
            case '<':
            case ',':
            seek_down:
               radio.seekDown();
               goto seek;
               break;
            //seek up to the next station (wrap to the bottom when the top is reached)
            case '>':
            case '.':
            seek_up:
               radio.seekUp();
               seek:
               print_clear_station_info(true);
               //check if seek done
               while( !(*current_station=radio.checkFrequency()) ){
                  //check if user wants to cancel seek (space bar)
                  if(Serial.read()==' '){
                     *current_station=radio.cancelSeek();
                     break;
                  }
               }
               print_station_freq();
               //set delay timer
               displayTimer.setTimer(UPDATE_SHORT_DELAY);
               break;
            //preset stations
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
               //lookup station
               word station;
               if(radio.getMode()==FM){
                  station = FM_preset_stations[ch-'0'];
               }else{  //AM,SW,LW
                  station = AM_preset_stations[ch-'0'];
               }
               //check for empty preset
               if(station){
                  *current_station=station;
                  go_to_station:
                  radio.tuneFrequency(*current_station);
                  new_station:
                  print_clear_station_info();
                  print_station_freq();
                  //set delay timer
                  displayTimer.setTimer(UPDATE_SHORT_DELAY);
                  //wait for tuning to complete
                  radio.waitSTC();
               }
               break;
            }
         }
         break;
      }
      print_home();
   }
   //poll radio if "on"
   switch(radio.getMode()){
   case RADIO_OFF:
      break;  //do nothing
   case FM:
      //check for RDS data from radio chip
      if(radio.checkRDS()){
         //print ID, type, message
         char callSign[5];  //Call sign - RBDS stations only
         radio.getCallSign(callSign);
         printp(rds1, POS(10,6));
         Serial.write(callSign);
         printp(rds2, POS(10,7) REVERSE);
         Serial.write(radio.rds.programService);
         printp(rds3, POS(10,8));
         char programType[17];  //Program Type text
         radio.getProgramTypeStr(programType);
         Serial.write(programType);
         printp(rds4, POSX(28));
         Serial.write(radio.rds.programTypeName);
         printp(rds5, NORMAL POS(10,11));
         Serial.write(radio.rds.radioText);

         //print date/time
         DateTime time;
         static const char PROGMEM day[7][4]={
            "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
         };
         if(radio.getLocalDateTime(&time)){
            printp(rds_date1, POS(10,12));
            //date
            print_number(time.year, 4);
            Serial.write('-');
            print_number(time.month, 2, '0');
            Serial.write('-');
            print_number(time.day, 2, '0');
            printp(rds_date2, "  ");
            printpstr(day[time.wday]);
            printp(rds_date3, "  ");
            //time
            byte hour=time.hour;
            if(hour>12){  //1PM - 11PM
               hour-=12;
            }
            else if(!hour){  //12AM
               hour=12;
            }  //do nothing if 1AM - 12PM
            print_number(hour, 2);
            Serial.write(':');
            print_number(time.minute, 2, '0');
            if(time.hour>=12){
               printp(rds_date4, " PM");
            }else{
               printp(rds_date5, " AM");
            }
            printp(rds_date6, "  ");
            //local offset
            int offset=radio.rds.offset;
            if(offset<0){
               printp(rds_date7, "-");
               offset = -offset;  //make positive for printing
            }else{
               printp(rds_date8, "+");
            }
            print_number(offset/2, 0, '0');
            printp(rds_date8, ".");
            Serial.write(byte(offset%2)*byte(5) + '0');
         }
         print_home();
      }
      /* fall through */
   default:
      //check if time to poll RSQ info
      if(displayTimer.checkTimer()){
         //reset delay timer
         displayTimer.setTimer(UPDATE_DELAY);

         //get station/signal info
         RSQMetrics rsq;
         radio.getRSQ(&rsq);
         //stereo/mono
         if(rsq.stereo){
            //print stereo msg
            printp(rsq_stereo, POS(2,5) BOLD "Stereo" NORMAL ": ");
            //print stereo blend
            print_number(rsq.stereoBlend, 3);
            Serial.write('%');
         }else{
            //print mono msg & clear stereo blend
            printp(rsq_mono, POS(2,5)"  Mono:     ");
         }
         //print signal to noise ratio
         printp(rsq_snr, POS(10,9));
         print_number(rsq.SNR, 3);
         //print weak
         if(rsq.seekable){
            printp(rsq_not_weak, POSX(18)"    ");
         }else{
            printp(rsq_weak, POSX(18)"Weak");
         }
         //print received signal strength indication
         printp(rsq_rssi, POS(10,10));
         print_number(rsq.RSSI, 3);
         print_home();
      }
      break;
   }
}

