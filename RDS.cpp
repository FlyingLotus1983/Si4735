/* Arduino Si4735 Library, RDS code.
 * Original RDS code by Wagner Sartori Junior <wsartori@gmail.com> released on
 * 2011-09-13 (very primitive) and Jon Carrier <jjcarrier@gmail.com> released on
 * 2011-10-20.  Cleaned up and improved by Michael J. Kennedy.
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
#include <string.h>

/******************************************************************************
*   RDS/RBDS                                                                  *
******************************************************************************/

// Examines given character.  If printable ASCII character, the character
// is returned.  If not printable, a space is returned.
// ***** PRIVATE *****
static char make_printable(char ch){
   //Replace non-ASCII char with space
   if(ch<32 || 126<ch) ch=' ';
   return ch;
}

// Poll RDS info from radio and saves in class object.  Also clears RDS interrupt.
// Returns true if new info found.  If not FM mode, it returns false.
// TODO: Add RT+, eRT, and maybe TMC
bool Si4735::getRDS(){
   byte response[13];  //Returned RDS info
   //Indices for group data in response[]
   enum {
      PI_H=4,  //Also "Block A"
      PI_L,
      Block_B_H,
      Block_B_L,
      Block_C_H,
      Block_C_L,
      Block_D_H,
      Block_D_L
   };
   byte segment;  //Current segment
   bool new_info=false;  //Return value - true if new RDS info has been collected

   //Check for FM mode
   if(_mode!=FM) return false;
   //Clear local RDS interrupt
   clearInterrupts(RDS_MASK);
   //Read in all pending RDS groups (packets)
   while(1){
      //Ask for next RDS group and clear RDS interrupt
      static const byte PROGMEM FM_RDS_STATUS[]={CMD_FM_RDS_STATUS, RDS_STATUS_ARG1_CLEAR_INT};
      sendCommand_P(FM_RDS_STATUS, sizeof(FM_RDS_STATUS));
      getResponse(response, sizeof(response));

      //Check for RDS signal
      rds.RDSSignal = response[2] & FIELD_RDS_STATUS_RESP2_SYNC;
      //Get number of RDS groups (packets) available
      byte num_groups=response[3];
      //Stop if nothing returned
      if(!num_groups) break;

      /* Because PI is resent in every packet's Block A, we told the radio its OK to
       * give us packets with a corrupted Block A.
       */
      //Check if PI received is valid
      if((response[12] & FIELD_RDS_STATUS_RESP12_BLOCK_A) != RDS_STATUS_RESP12_BLOCK_A_UNCORRECTABLE){
         //Get PI code
         rds.programId = MAKE_WORD(response[PI_H], response[PI_L]);
      }
      //Get PTY code
      rds.programType = ((response[Block_B_H] & 0b00000011) << 3U) | (response[Block_B_L] >> 5U);
      //Get Traffic Program bit
      rds.trafficProgram = bool(response[Block_B_H] & 0b00000100);

      //Get group type (0-15)
      byte type = response[Block_B_H]>>4U;
      //Get group version (0=A, 1=B)
      bool version = response[Block_B_H] & 0b00001000;

      //Save which group type and version was received
      if(version){
         rds.groupB |= 1U<<type;
      }else{
         rds.groupA |= 1U<<type;
      }

      //Groups 0A & 0B - Basic tuning and switching information
      //Group 15B - Fast basic tuning and switching information
      /* Note: We support both Groups 0 and 15B in case the station has poor
       * reception and RDS packets are barely getting through.  This increases
       * the chances of receiving this info.
       */
      if(type==0 || (type==15 && version==1)){
         //Various flags
         rds.trafficAlert = bool(response[Block_B_L] & 0b00010000);
         rds.music =        bool(response[Block_B_L] & 0b00001000);
         bool DI =               response[Block_B_L] & 0b00000100;

         //Get segment number
         segment =               response[Block_B_L] & 0b00000011;
         //Handle DI code
         switch(segment){
         case 0:
            rds.dynamicPTY=DI;
            break;
         case 1:
            rds.compressedAudio=DI;
            break;
         case 2:
            rds.binauralAudio=DI;
            break;
         case 3:
            rds.RDSStereo=DI;
            break;
         }

         //Groups 0A & 0B
         if(type==0){
            //Program Service
            char *ps = &rds.programService[segment*2];
            *ps++ = make_printable(response[Block_D_H]);
            *ps   = make_printable(response[Block_D_L]);
         }
         new_info=true;
      }
      //Group 1A - Extended Country Code (ECC) and Language Code
      else if(type==1 && version==0){
         //We are only interested in the Extended Country Code (ECC) and
         //Language Code for this Group.

         //Get Variant code
         switch(response[Block_C_H] & 0b01110000){
         case (0<<4):  //Variant==0
            //Extended Country Code
            //Check if count has reached threshold
            if(_extendedCountryCode_count < RDS_THRESHOLD){
               byte ecc = response[Block_C_L];
               //Check if datum changed
               if(rds.extendedCountryCode != ecc){
                  _extendedCountryCode_count=0;
                  new_info=true;
               }
               //Save new data
               rds.extendedCountryCode = ecc;
               ++_extendedCountryCode_count;
            }
            break;
         case (3<<4):  //Variant==3
            //Language Code
            //Check if count has reached threshold
            if(_language_count < RDS_THRESHOLD){
               byte language = response[Block_C_L];
               //Check if datum changed
               if(rds.language != language){
                  _language_count=0;
                  new_info=true;
               }
               //Save new data
               rds.language = language;
               ++_language_count;
            }
            break;
         }
      }
      //Groups 2A & 2B - Radio Text
      else if(type==2){
         //Check A/B flag to see if Radio Text has changed
         byte new_ab = bool(response[Block_B_L] & 0b00010000);
         if(new_ab != _abRadioText){
            //New message found - clear buffer
            _abRadioText=new_ab;
            for(byte i=0; i<sizeof(rds.radioText)-1; i++) rds.radioText[i]=' ';
            rds.radioTextLen=sizeof(rds.radioText);  //Default to max length
         }
         //Get segment number
         segment = response[Block_B_L] & 0x0F;

         //Get Radio Text
         char *rt;  //Next position in rds.radioText[]
         byte *block;  //Next char from segment
         byte i;  //Loop counter
         //TODO maybe: convert RDS non ASCII chars to UTF-8 for terminal interface
         if(version==0){  // 2A
            rt = &rds.radioText[segment*4];
            block = &response[Block_C_H];
            i=4;
         }
         else{  // 2B
            rt = &rds.radioText[segment*2];
            block = &response[Block_D_H];
            i=2;
         }
         //Copy chars
         do{
            //Get next char from segment
            char ch = *block++;
            //Check for end of message marker
            if(ch=='\r'){
               //Save new message length
               rds.radioTextLen = rt-rds.radioText;
            }
            //Put next char in rds.radioText[]
            *rt++ = make_printable(ch);
         }while(--i);
         new_info=true;
      }
      //Group 4A - Clock-time and date
      else if(type==4 && version==0){
         //Only use if received perfectly.
         /* Note: Error Correcting Codes (ECC) are not perfect.  It is possible
          * for a block to be damaged enough that the ECC thinks the data is OK
          * when it's damaged or that it can recover when it cannot.  Because
          * date and time are useless unless accurate, we require that the date
          * and time be received perfectly to increase the odds of accurate data.
          */
         if( (response[12] & (FIELD_RDS_STATUS_RESP12_BLOCK_B |
          FIELD_RDS_STATUS_RESP12_BLOCK_C | FIELD_RDS_STATUS_RESP12_BLOCK_D)) ==
          (RDS_STATUS_RESP12_BLOCK_B_NO_ERRORS | RDS_STATUS_RESP12_BLOCK_C_NO_ERRORS |
          RDS_STATUS_RESP12_BLOCK_D_NO_ERRORS) ){
            //Get Modified Julian Date (MJD)
            rds.MJD = (response[Block_B_L] & 0b00000011)<<15UL | response[Block_C_H]<<7U | response[Block_C_L]>>1U;

            //Get hour and minute
            rds.hour = (response[Block_C_L] & 0b00000001)<<4U | response[Block_D_H]>>4U;
            rds.minute = (response[Block_D_H] & 0x0F)<<2U | response[Block_D_L]>>6U;

            //Check if date and time sent (not 0)
            if(rds.MJD || rds.hour || rds.minute || response[Block_D_L]){
               //Get offset to convert UTC to local time
               rds.offset = response[Block_D_L]&0x1F;
               //Check if offset should be negative
               if(response[Block_D_L] & 0b00100000){
                  rds.offset = -rds.offset;  //Make it negative
               }
               new_info=true;
            }
         }
      }
      //Group 10A - Program Type Name
      else if(type==10 && version==0){
         //Check A/B flag to see if Program Type Name has changed
         byte new_ab = bool(response[Block_B_L] & 0b00010000);
         if(new_ab != _abProgramTypeName){
            //New name found - clear buffer
            _abProgramTypeName=new_ab;
            for(byte i=0; i<sizeof(rds.programTypeName)-1; i++) rds.programTypeName[i]=' ';
         }
         //Get segment number
         segment = response[Block_B_L] & 0x01;

         //Get Program Type Name
         char *name = &rds.programTypeName[segment*4];
         *name++ = make_printable(response[Block_C_H]);
         *name++ = make_printable(response[Block_C_L]);
         *name++ = make_printable(response[Block_D_H]);
         *name   = make_printable(response[Block_D_L]);
         new_info=true;
      }
   }
   return new_info;
}

// Same as readRDS() but first checks RDS interrupt before trying to get data.
bool Si4735::checkRDS(){
   //Check if radio has new RDS data for us
   if(currentInterrupts() & RDS_MASK){
      //Get RDS data
      debug(print,"RDS true: ");
      debug(println,_interrupts,HEX);
      return getRDS();
   }else{
      debug(print,"RDS false: ");
      debug(println,_interrupts,HEX);
   }
   //No RDS data available
   return false;
}

// Saves the call sign derived from the RBDS PI code in the given 5 char buffer.
// Returns true if buffer has a valid call sign.  Otherwise, returns false and
// the buffer is initialized with fill chars.  If PI is invalid, a '-' is used.
// If TMC is detected, a '*' is used.  Otherwise spaces are used.  If the call
// sign has only 3 letters, the first character will have a space character,
// followed by the call sign.
// Only provides meaningful info if mode==FM.
bool Si4735::getCallSign(char callSign[5]){
   //Get RDS/RBDS status XXX temp hack
   rds.RBDS = check_if_RBDS();

   //Convert PI code to station's call sign when using USA's RBDS.
   //See document "NRSC-4-B" from http://www.nrscstandards.org
   /* Almost all commercial USA FM stations have four call letters.  (A few
    * have three.)  All begin with either a 'W' or 'K'.  Four letter stations
    * are assigned a PI code by formula.  There are two name spaces for these
    * stations, one for 'W' and one for 'K'.  The size of each name space is
    * given by NAME_SPACE.  The offset for the first name space 'K' is given by
    * NAME_SPACE_BEGIN.  The 'W' name space immediately follows 'K'.  This is
    * followed by the legacy three letter stations.
    */
   word pi=rds.programId;
   enum {NAME_SPACE_BEGIN=0x1000};  //RDS forbids PI codes with a high nibble of 0.
   enum {NAME_SPACE=26*26*26};  //26 letters in alphabet, three letters.
   char fill;  //Fill char

   //Check if using RDS
   if(!rds.RBDS) goto no_pi;

   //Check for PI with North American TMC prefix (high nibble of 0x1)
   if((pi&0xF000) == 0x1000){
      /* There are two possibilities:
       * • The station is transmitting Traffic Message Channel (TMC) traffic
       *   incident info and has elected to modify its PI code by placing the
       *   TMC North American prefix (0x1) in the high nibble of the PI.  In
       *   this case, it is not possible to decode the station's call sign.
       * • The station has call letters in the range KAAA-KGBN, which encodes a
       *   PI with a high nibble of 0x1.  In this case, I can decode the
       *   station's call sign.
       */
      //Check for TMC packets (Group 8A) in RDS data stream.
      if(rds.groupA | (1U<<8)){
         //TMC found, we cannot decode PI code.
         /* Note: We assume that Group 8A is always TMC.  This is not always true.
          * RDS/RBDS permits this packet to be reassigned for custom use.
          */
         fill='*';
         goto fill_call_sign;
      }
      //No TMC packets found.  We can decode PI code.
   }
   //Check for compatability PI codes
   /* RDS gives the following bit layout for PI codes.  (See the included "RDS"
    * document and "NRSC-4-B" for more details on PI codes.)
    * • 4 bits: Country Code.  May not be 0.
    * • 4 bits: Coverage area code.  0 means local program.
    * • 8 bits: Assigned ID.  May not be 0.
    * RBDS mostly ignores all of this.  In particular, there are no Coverage
    * area codes.  However, to maintain some level of compatibility with RDS, the
    * encoding scheme used makes adjustments to the PI code so that all three PI
    * fields never have a value of 0.  To avoid a Country Code of 0, PI codes
    * start with 0x1000.  The other fields require tricky handling which is
    * documented in "NRSC-4-B".
    * Note: RDS permits stations to broadcast an alternate list of frequencies
    * of nearby stations that carry the same programming.  These networks of
    * stations all use a common PI code.  This permits an RDS car radio to stay
    * with a network while traveling.  However, if a PI code has a coverage
    * area code of 0 (local programming), this automatic network following is
    * turned off.  In the USA, a small number of rural stations simulcast on
    * multiple frequencies to increase coverage area.  If the RBDS PI code
    * formula happens to generate a 0 in the coverage area code, RDS radios will
    * not be able to automatically follow these simulcast stations.  (An RBDS
    * radio is supposed to know that RBDS does not use coverage area codes.)
    * Because of this problem, RBDS ensures the coverage area code field will not
    * be 0.
    */
   //Check for PI that should have 0 in ID field.
   if((pi&0xFF00) == 0xAF00){
      //Restore PI code
      pi <<= 8;
      //Fall through to next test.  Both fields could be 0.
   }
   //Check for PI that should have 0 in coverage area code field.
   if((pi&0xF000) == 0xA000){
      //Extract high byte's low nibble
      word hb = pi&0x0F00;
      //Reposition nibble
      hb <<= 4;
      //Clear old high byte from PI
      pi &= 0x00FF;
      //Combine high and low bytes to form PI code
      pi |= hb;
   }
   //Check for legacy 3 letter stations (WLS, etc.)
   if(pi >= NAME_SPACE_BEGIN + NAME_SPACE * 2){
      /* All stations with a legacy call sign of 3 letters are assigned
       * arbitrary codes.  All of these stations began as AM band channels.
       * A few added an FM channel.  In this case, the same call sign is used
       * for both AM and FM channels.  For example, Chicago IL has two legacy
       * stations, WLS and WGN.  While most legacy stations (like WGN) have
       * never had an FM channel (and RDS/RBDS requires FM), however, some
       * (like WLS) have and there is no way to know which of these stations
       * will add an FM channel in the future.
       */
      //Pack letters
      #define PACK(L3,L2,L1) ( ((L3)-'A')*26*26 + ((L2)-'A')*26 + ((L1)-'A') )

      //Call signs for legacy stations - a value of 0 indicates PI code is not used.
      static const word PROGMEM legacy[]={
         PACK('K','E','X'), PACK('K','F','H'), PACK('K','F','I'), PACK('K','G','A'),
         PACK('K','G','O'), PACK('K','G','U'), PACK('K','G','W'), PACK('K','G','Y'),
         PACK('K','I','D'), PACK('K','I','T'), PACK('K','J','R'), PACK('K','L','O'),
         PACK('K','L','Z'), PACK('K','M','A'), PACK('K','M','J'), PACK('K','N','X'),
         PACK('K','O','A'), 0,                 0,                 0,
         PACK('K','Q','V'), PACK('K','S','L'), PACK('K','U','J'), PACK('K','V','I'),
         PACK('K','W','G'), 0,                 0,                 PACK('K','Y','W'),
         0,                 PACK('W','B','Z'), PACK('W','D','Z'), PACK('W','E','W'),
         0,                 PACK('W','G','L'), PACK('W','G','N'), PACK('W','G','R'),
         0,                 PACK('W','H','A'), PACK('W','H','B'), PACK('W','H','K'),
         PACK('W','H','O'), 0,                 PACK('W','I','P'), PACK('W','J','R'),
         PACK('W','K','Y'), PACK('W','L','S'), PACK('W','L','W'), 0,
         0,                 PACK('W','O','C'), 0,                 PACK('W','O','L'),
         PACK('W','O','R'), 0,                 0,                 0,
         PACK('W','W','J'), PACK('W','W','L'), 0,                 0,
         0,                 0,                 0,                 0,
         PACK('K','D','B'), PACK('K','G','B'), PACK('K','O','Y'), PACK('K','P','Q'),
         PACK('K','S','D'), PACK('K','U','T'), PACK('K','X','L'), PACK('K','X','O'),
         0,                 PACK('W','B','T'), PACK('W','G','H'), PACK('W','G','Y'),
         PACK('W','H','P'), PACK('W','I','L'), PACK('W','M','C'), PACK('W','M','T'),
         PACK('W','O','I'), PACK('W','O','W'), PACK('W','R','R'), PACK('W','S','B'),
         PACK('W','S','M'), PACK('K','B','W'), PACK('K','C','Y'), PACK('K','D','F'),
         0,                 0,                 PACK('K','H','Q'), PACK('K','O','B'),
         0,                 0,                 0,                 0,
         0,                 0,                 0,                 PACK('W','I','S'),
         PACK('W','J','W'), PACK('W','J','Z'), 0,                 0,
         0,                 PACK('W','R','C')
      };

      //Check if PI is out of bounds
      pi -= NAME_SPACE_BEGIN + NAME_SPACE * 2;
      if(pi >= sizeof(legacy)/sizeof(word)) goto bad;
      //Translate PI code into packed 3 letter code
      pi=pgm_read_word(&legacy[pi]);
      //Check for undefined code (0) from table
      if(!pi) goto bad;
      //Clear first char
      callSign[0]=' ';
   }
   //W___ call sign
   else if(pi >= NAME_SPACE_BEGIN + NAME_SPACE){
      pi -= NAME_SPACE_BEGIN + NAME_SPACE;
      callSign[0]='W';
   }
   //K___ call sign
   else if(pi >= NAME_SPACE_BEGIN){
      pi -= NAME_SPACE_BEGIN;
      callSign[0]='K';
   }
   //Check if we have received a PI code
   else if(!pi){
      //No RDS info yet
      no_pi:
      fill=' ';
      goto fill_call_sign;
   }
   else{
      //Bad PI code
      bad:
      fill='-';
      fill_call_sign:
      callSign[0]=fill;
      callSign[1]=fill;
      callSign[2]=fill;
      callSign[3]=fill;
      callSign[4]='\0';
      return false;
   }
   //Unpack 3 letters
   callSign[3]=pi%26+'A';  //Extract letter
   pi/=26;                 //Shift letters to right
   callSign[2]=pi%26+'A';
   pi/=26;
   callSign[1]=pi+'A';
   callSign[4]='\0';
   return true;
}

// Returns true if it thinks the current station is using RDBS or false is using RDS.
/* All US stations use RBDS.  All non US stations use RDS.  The following rules
 * are used to determine the country:
 *
 * First we check for an Extended Country Code.  If found, this overrides any
 * locale settings.  This way, you many not need to change the locale when
 * traveling to and from the US.
 *
 * Next we check if the locale is set to US, Canada, or Mexico.  If so, we assume
 * the PI code can tell us the country.  (This will only work if the radio is in
 * or near the US, Canada, or Mexico.)  This test is smart enough to deal with a
 * mix of US and non-US stations when the radio is located near the US boarder.
 * This test will probably fail for stations located outside these three countries.
 * If the locale is not one of these three countries, we assume we are far away
 * from the US and default to RDS.
 *
 * If no PI code has been received yet, we default to the locale setting.  This
 * will only happen if the station has very poor reception of RDS data.
 */
// ***** PRIVATE *****
bool Si4735::check_if_RBDS(){
   //XXX Check Extended Country Code (ECC) if ECC saved is trustworthy
   if(_extendedCountryCode_count < RDS_THRESHOLD){
      goto no_ecc;
   }
   switch(rds.extendedCountryCode){
   case ECC_US:  //Station's country is USA
      return true;  //ECC says USA - USA always uses RBDS
   case ECC_UNKNOWN:
   no_ecc:
      //Check if locale is USA, Canada, Mexico
      switch(_locale){
      case LOCALE_US:
      case LOCALE_CA_MX:
         //Check for PI code
         if(rds.programId){
            //Check Country Code (CC) in PI code (high nibble of PI).
            //In actual practice, if CC is 0x1 to 0xA then USA.
            //If CC is 0xB to 0xF then Canada or Mexico.  CC=0x0 is forbidden.
            if(rds.programId<0xB000) return true;  //PI code says USA
         }else{
            //No PI code received - default to locale
            if(_locale==LOCALE_US) return true;  //Locale says USA
         }
         break;
      }
      break;
   }
   //Station's country is not USA
   return false;  //Other countries never use RBDS
}

// Translates the current Program Type code into a 16-character English message.
// Message is saved in given 17 char buffer.
void Si4735::getProgramTypeStr(char buffer[17]){
   //Get RDS/RBDS status XXX temp hack
   rds.RBDS = check_if_RBDS();

   //Descriptive text for each PTY code
   static const char PROGMEM PTY_RBDS_to_str[51][16]={
      ' ',' ',' ',' ',' ',' ','N','o','n','e',' ',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ',' ','N','e','w','s',' ',' ',' ',' ',' ',' ',
      ' ',' ','I','n','f','o','r','m','a','t','i','o','n',' ',' ',' ',
      ' ',' ',' ',' ',' ','S','p','o','r','t','s',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ',' ','T','a','l','k',' ',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ',' ','R','o','c','k',' ',' ',' ',' ',' ',' ',
      ' ',' ','C','l','a','s','s','i','c',' ','R','o','c','k',' ',' ',
      ' ',' ',' ','A','d','u','l','t',' ','H','i','t','s',' ',' ',' ',
      ' ',' ',' ','S','o','f','t',' ','R','o','c','k',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ','T','o','p',' ','4','0',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ','C','o','u','n','t','r','y',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ','O','l','d','i','e','s',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ',' ','S','o','f','t',' ',' ',' ',' ',' ',' ',
      ' ',' ',' ','N','o','s','t','a','l','g','i','a',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ',' ','J','a','z','z',' ',' ',' ',' ',' ',' ',
      ' ',' ',' ','C','l','a','s','s','i','c','a','l',' ',' ',' ',' ',
      'R','h','y','t','h','m',' ','a','n','d',' ','B','l','u','e','s',
      ' ',' ',' ','S','o','f','t',' ','R',' ','&',' ','B',' ',' ',' ',
      'F','o','r','e','i','g','n',' ','L','a','n','g','u','a','g','e',
      'R','e','l','i','g','i','o','u','s',' ','M','u','s','i','c',' ',
      ' ','R','e','l','i','g','i','o','u','s',' ','T','a','l','k',' ',
      ' ',' ','P','e','r','s','o','n','a','l','i','t','y',' ',' ',' ',
      ' ',' ',' ',' ',' ','P','u','b','l','i','c',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ','C','o','l','l','e','g','e',' ',' ',' ',' ',' ',
      ' ',' ','S','p','a','n','i','s','h',' ','T','a','l','k',' ',' ',
      ' ','S','p','a','n','i','s','h',' ','M','u','s','i','c',' ',' ',
      ' ',' ',' ',' ','H','i','p',' ','H','o','p',' ',' ',' ',' ',' ',
      ' ','R','e','s','e','r','v','e','d',' ',' ','-','2','7','-',' ',
      ' ','R','e','s','e','r','v','e','d',' ',' ','-','2','8','-',' ',
      ' ',' ',' ',' ',' ','W','e','a','t','h','e','r',' ',' ',' ',' ',
      ' ','E','m','e','r','g','e','n','c','y',' ','T','e','s','t',' ',
      ' ','A','L','E','R','T','!',' ','A','L','E','R','T','!',' ',' ',
      //Following messages are for locales outside USA (RDS)
      'C','u','r','r','e','n','t',' ','A','f','f','a','i','r','s',' ',
      ' ',' ',' ','E','d','u','c','a','t','i','o','n',' ',' ',' ',' ',
      ' ',' ',' ',' ',' ','D','r','a','m','a',' ',' ',' ',' ',' ',' ',
      ' ',' ',' ',' ','C','u','l','t','u','r','e','s',' ',' ',' ',' ',
      ' ',' ',' ',' ','S','c','i','e','n','c','e',' ',' ',' ',' ',' ',
      ' ','V','a','r','i','e','d',' ','S','p','e','e','c','h',' ',' ',
      ' ','E','a','s','y',' ','L','i','s','t','e','n','i','n','g',' ',
      ' ','L','i','g','h','t',' ','C','l','a','s','s','i','c','s',' ',
      'S','e','r','i','o','u','s',' ','C','l','a','s','s','i','c','s',
      ' ',' ','O','t','h','e','r',' ','M','u','s','i','c',' ',' ',' ',
      ' ',' ',' ',' ','F','i','n','a','n','c','e',' ',' ',' ',' ',' ',
      'C','h','i','l','d','r','e','n','\'','s',' ','P','r','o','g','s',
      ' ','S','o','c','i','a','l',' ','A','f','f','a','i','r','s',' ',
      ' ',' ',' ',' ','P','h','o','n','e',' ','I','n',' ',' ',' ',' ',
      'T','r','a','v','e','l',' ','&',' ','T','o','u','r','i','n','g',
      'L','e','i','s','u','r','e',' ','&',' ','H','o','b','b','y',' ',
      ' ','N','a','t','i','o','n','a','l',' ','M','u','s','i','c',' ',
      ' ',' ',' ','F','o','l','k',' ','M','u','s','i','c',' ',' ',' ',
      ' ',' ','D','o','c','u','m','e','n','t','a','r','y',' ',' ',' '
   };
   const char *str;  //String from PTY_RBDS_to_str[] array.

   //Translate PTY code into English text based on RBDS/RDS flag.
   if(rds.RBDS){
      str = PTY_RBDS_to_str[rds.programType];
   }else{
      //Translate RDS PTY code to RBDS PTY code
      //Note: Codes above 31 do not actually exist but can be used with the PTY_RBDS_to_str[] table.
      static const byte PROGMEM PTY_RDS_to_RBDS[32]={
         0, 1, 32, 2,
         3, 33, 34, 35,
         36, 37, 9, 5,
         38, 39, 40, 41,
         29, 42, 43, 44,
         20, 45, 46, 47,
         14, 10, 48, 11,
         49, 50, 30, 31
      };
      str = PTY_RBDS_to_str[ pgm_read_byte(&PTY_RDS_to_RBDS[rds.programType]) ];
   }
   //Copy text to caller's buffer.
   memcpy_P(buffer, str, 16);
   buffer[16]='\0';  //Null terminate output buffer
}

// Non leap year
#define DAYS_PER_YEAR 365U
// Leap year
#define DAYS_PER_LEAP_YEAR (DAYS_PER_YEAR + 1)
// Leap year every 4 years
#define DAYS_PER_4YEARS (DAYS_PER_YEAR * 4 + 1)
// Leap year every 4 years except century year (divisable by 100)
#define DAYS_PER_100YEARS (DAYS_PER_4YEARS * (100/4) - 1)

// Get last RDS date and time converted to local date and time.
// Returns true if current station has sent date and time.  Otherwise, it returns
// false and writes nothing to structure.
// Only provides info if mode==FM and station is sending RDS data.
bool Si4735::getLocalDateTime(DateTime *time){
   //Look for date/time info
   if(rds.offset==NO_DATE_TIME) return false;  //No date or time info available

   //Origin for Modified Julian Date (MJD) is November 17, 1858, Wednesday.
   //Move origin to Jan. 2, 2000, Sunday.
   //Note: We don't use Jan. 1 to compensate for the fact that 2000 is a leap year.
   unsigned short days=
      rds.MJD-(             // 1858-Nov-17
      14 +                  // 1858-Dec-1
      31 +                  // 1859-Jan-1
      DAYS_PER_YEAR +       // 1860-Jan-1
      10*DAYS_PER_4YEARS +  // 1900-Jan-1
      DAYS_PER_100YEARS +   // 2000-Jan-1
      1);                   // 2000-Jan-2

   //Convert UTC date and time to local date and time.
   //Combine date and time
   unsigned long date_time = ((unsigned long)days)*(24*60) + ((unsigned short)rds.hour)*60 + rds.minute;
   //Adjust offset from units of half hours to minutes
   short offset = short(rds.offset)*30;
   //Compute local date/time
   date_time += offset;
   //Break down date and time
   time->minute = date_time%60;
   date_time /= 60;
   time->hour = date_time%24;
   days= date_time / 24;

   //Compute day of the week - Sunday = 0
   time->wday = days % 7;

   //Compute year
   unsigned char leap_year=0;  /* 1 if leap year, else 0 */
   //Note: This code assumes all century years (2000, 2100...) are not leap years.
   //This will break in 2400 AD.  However, RDS' date field will overflow long before 2400 AD.
   time->year = days/DAYS_PER_100YEARS * 100 + 2000;
   days %= DAYS_PER_100YEARS;
   if(!(days<DAYS_PER_YEAR)){
      days++;  //Adjust for no leap year for century year
      time->year += days/DAYS_PER_4YEARS * 4;
      days %= DAYS_PER_4YEARS;
      if(days < DAYS_PER_LEAP_YEAR){
         leap_year=1;
      }
      else{
         days--;  //Adjust for leap year for first of 4 years
         time->year += days/DAYS_PER_YEAR;
         days %= DAYS_PER_YEAR;
      }
   }

   //Compute month and day of the month
   if(days < 31+28+leap_year){
      if(days < 31){
         /* January */
         time->month = 1;
         time->day = days+1;
      }
      else{
         /* February */
         time->month = 2;
         time->day = days+1-31;
      }
   }
   else{
      /* March - December */
      enum {NUM_MONTHS=10};
      static const unsigned short PROGMEM month[NUM_MONTHS]={
         0,
         31,
         31+30,
         31+30+31,
         31+30+31+30,
         31+30+31+30+31,
         31+30+31+30+31+31,
         31+30+31+30+31+31+30,
         31+30+31+30+31+31+30+31,
         31+30+31+30+31+31+30+31+30
      };
      unsigned short value;  //Value from table
      unsigned char mon;  //Index to month[]

      days -= 31+28+leap_year;
      //Look up month
      for(mon=NUM_MONTHS; days < (value=pgm_read_word(&month[--mon])); );
      time->day = days-value+1;
      time->month = mon+2+1;
   }
   return true;
}

// Get last RDS time converted to local time.
// Returns true if current station has sent date and time.  Otherwise, it returns
// false and writes nothing to structure.
// Only provides info if mode==FM and station is sending RDS data.
bool Si4735::getLocalTime(Time *time){
   //Look for date/time info
   if(rds.offset==NO_DATE_TIME) return false;  //No date or time info available

   //Convert UTC to local time
   /* Note: If the offset is negative, 'hour' and 'minute' could become negative.
    * To compensate, we add 24 to hour and 60 to minute.  We then do a modulus
    * division (%24 and %60) to correct for any overflow caused by either a
    * positive offset or the above mentioned addition.
    */
   time->hour = (rds.hour + rds.offset/2 + 24) % 24;
   time->minute = (rds.minute + rds.offset%2*30 + 60) % 60;
   return true;
}

