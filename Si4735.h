/* Arduino Si4735 Library.
 * Original by Ryan Owens for SparkFun Electronics on 2011-5-17.
 * Altered by Wagner Sartori Junior <wsartori@gmail.com> on 2011-09-13.
 * Altered by Jon Carrier <jjcarrier@gmail.com> on 2011-11-11.
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
 * See README file for additional documentation.
 * See the example sketches to learn how to use the library in your code.
 */

#ifndef Si4735_h
#define Si4735_h

//Use to activate low level debugging messages on software serial port
//#define DEBUG 1

#if DEBUG
#include "SoftwareSerial.h"  //SoftwareSerial Library
extern SoftwareSerial debugSerial;
#define DEBUG_TX A0  //Tx PIN
#define DEBUG_RX A1  //Rx PIN - not currently used
#define debug(method, ...) debugSerial.method(__VA_ARGS__)
#else
#define debug(method, ...)
#endif

#if ARDUINO >= 100
  #include <Arduino.h>
#else
  #include <WProgram.h>
#endif

#include <pins_arduino.h>  //Defines SPI pins: SCK, MOSI, MISO, SS

#ifdef __AVR__
   #include <avr/pgmspace.h>
#else
   // Create dummy versions of macros and functions found in <avr/pgmspace.h>
   #define PROGMEM
   #define pgm_read_byte(ptr) (*(ptr))
   #define pgm_read_word(ptr) (*(ptr))
   #define strcpy_P(d, s) strcpy((d), (s))
   #define memcpy_P(d, s, l) memcpy((d), (s), (l))
#endif

/******************************************************************************
* Compile time options for Si4735 library.                                    *
* Change these as required when using the SparkFun Si4735 Breakout Board.     *
* Default values are for SparkFun Si4735 Arduino Shield.                      *
******************************************************************************/

// If Si47xx_SPI macro is defined, use SPI bus to talk to radio.  Otherwise, use I2C bus.
// Remove this macro if you want to use I2C when using SparkFun's breakout board.
// Warning: The SparkFun Arduino shield does NOT support I2C without major modification
// of the circuit board!  If you want to use I2C, use the SparkFun breakout board.
// Warning: The I2C code requires Arduino software 1.0 or greater.
//#define Si47xx_SPI

// Radio I/O pins.  These pin assignments are based on the SparkFun shield.
// Change these if you want when using SparkFun's breakout board.
enum {
   RADIO_SPI_SS_PIN=10,  //SS pin on "original" Arduinos
   RADIO_RESET_PIN =9,
   RADIO_POWER_PIN =8,
   //Warning: On the Leonardo, the I2C port's SDA and SCL pins are on pins 2 and 3.
   //Therefore, if you want to use I2C on the Leonardo, RADIO_INT_PIN must be something
   //other than pins 2 and 3.
   //Note: RADIO_INT_PIN must agree with RADIO_EXT_INT below
   #if defined(__AVR_ATmega32U4__) && !defined(Si47xx_SPI)
   RADIO_INT_PIN   =7,  //Leonardo Arduinos with I2C
   #else
   RADIO_INT_PIN   =2,  //All other combinations
   #endif
};

// External interrupt number for AVR based Arduinos.
// Change this if you want when using SparkFun's breakout board.
// For "original" Arduinos this must be from 0 to 1.
// For "Mega" Arduinos this must be from 0 to 5.  (6 and 7 are not connected.)
// For "Leonardo" Arduinos this must be from 0 to 3 or a 6.
// Not used with ARM based Arduinos such as the Due.
enum {
   //Note: RADIO_EXT_INT must agree with RADIO_INT_PIN above
   #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
   RADIO_EXT_INT=4  //Mega Arduinos
   #elif defined(__AVR_ATmega32U4__) && defined(Si47xx_SPI)
   RADIO_EXT_INT=1  //Leonardo Arduinos with SPI
   #elif defined(__AVR_ATmega32U4__) && !defined(Si47xx_SPI)
   RADIO_EXT_INT=6  //Leonardo Arduinos with I2C
   #elif defined(__AVR__)
   RADIO_EXT_INT=0  //Original Arduinos
   #endif
};

/********************************
* Si4735 library default values *
********************************/

// In SPI mode, the second argmunet to begin() gives the clock divider to pass to
// SPI.setClockDivider().  If the second argument is not given to begin(),
// RADIO_SPI_CLOCK_DIV is used instead.  You may change the value of RADIO_SPI_CLOCK_DIV
// if you want.
// Note: Max speed of Si4735 clock input is 2.5 MHz.  All current 5V Arduinos run
// at a 16MHz clock speed.  For 16MHz, use divide by 8 or slower.  The Due runs at
// 84 MHz.  For the Due, use divide by 34 or slower.
// Note: Most users will be running a 5V Arduino with an added level shifter for MISO.
// Some low-to-high volt level shifters can only handle slow speeds.  We choose a slow
// conservative default that is about the same speed as I2C.  If your level shifter
// can handle more, or you don't need a level shifter, you may change the SPI speed.
#ifdef __AVR__
   #if F_CPU <= 8000000L
    #define RADIO_SPI_CLOCK_DIV  SPI_CLOCK_DIV32  //CPU = 8 MHz
   #else
    #define RADIO_SPI_CLOCK_DIV  SPI_CLOCK_DIV64  //CPU = 16 MHz
   #endif
#else
 /* ARM based Due */
 // Note: The Due runs at 3.3V and therefore does not require level shifting.  However,
 // a relatively long wire between the Due and the shield or breakout board will be
 // required and long wires are prone to picking up noise.  Therefore, we choose a slow,
 // conservative bus speed.
 #define RADIO_SPI_CLOCK_DIV  255  //CPU = 84 MHz
#endif

// I2C address of radio chip.  Pass this as the second argument to begin() in I2C mode.
// The radio can be configured to use one of two addresses depending on the SEN/SS input:
//    SEN   Address
//    -------------
//    Low   0x11 (Hexadecimal)
//    High  99   (Decimal)
enum {
   RADIO_I2C_ADDRESS_LOW =0x11,
   RADIO_I2C_ADDRESS_HIGH=99U,
   // If no address is passed to begin(), RADIO_I2C_ADDRESS is used.  Address defaults
   // to SEN == HIGH for Si4707 breakout board compatibility.  Change if you want.
   RADIO_I2C_ADDRESS     =RADIO_I2C_ADDRESS_HIGH
};

/***********************************
* Define Si4735 library class info *
***********************************/

// Bit masks for status byte and interrupts.
// Returned by currentInterrupts(), getInterrupts(), and getStatus().
enum {
   CTS_MASK= 0b10000000,  //Clear To Send
   ERR_MASK= 0b01000000,  //Error occurred
   RSQ_MASK= 0b00001000,  //Received Signal Quality measurement has triggered
   RDS_MASK= 0b00000100,  //RDS data received (FM mode only)
   SAME_MASK=0b00000100,  //SAME (WB) data received (Si4707 only)
   ASQ_MASK= 0b00000010,  //Audio Signal Quality (AUX and WB modes only)
   STC_MASK= 0b00000001   //Seek/Tune Complete
};

// Modes for the library and radio.  Argument for setMode(), returned by getMode().
// Note: When mode==RADIO_OFF, the radio is either in a low power "off" state
// or has all power removed.  Initially, there is no power to the radio.  To
// apply power, call begin() which places the radio in a low power mode.  To
// reenter low power mode later, call setMode(RADIO_OFF).  To remove all power
// from the radio, call end().
enum {
   RADIO_OFF=0,  //Low power or no power
   FM,  //Frequency Modulation band
   AM,  //Amplitude Modulation: Medium wavelength band
   SW,  //Amplitude Modulation: Short wavelength band
   LW,  //Amplitude Modulation: Long wavelength band
   //Alternate names
   MODE_OFF=RADIO_OFF,
   MODE_FM=FM,
   MODE_AM=AM,
   MODE_SW=SW,
   MODE_LW=LW,
};

// Options for setMode()
// Each option flag only has meaning when used with the specified mode.
// Multiple options can be combined with bitwise or (|).
// If the MODE_OPT_NO_XTAL option is passed to setMode(), then POWER_UP_ARG1_XOSCEN
// will not be passed to the POWER_UP command.  Otherwise, POWER_UP_ARG1_XOSCEN
// will be passed.  MODE_OPT_NO_XTAL should NOT be passed to setMode() when using the
// Si4735 shield, the Si4707 breakout board, or when the Si4735 breakout board has
// a 32768 Hz crystal attached to the radio's RCLK and GPO3 pins.  See the POWER_UP
// command in the "Si47xx Programming Guide" and the Si4735 and Si4707 data sheets
// for more information.
enum {
   MODE_OPT_DEFAULT=0,  //Use mode's default options
   //All modes:
   MODE_OPT_NO_XTAL     =0b001,  //No crystal connected to radio's internal oscillator
   //FM, MODE_FM options:
   MODE_FM_OPT_FULL_BAND=0b010,  //Use full FM band (64-108 MHz)
   MODE_FM_OPT_NO_RDS   =0b100,  //Do not use RDS with FM
};

// Audio mode for setMode() and POWER_UP command's ARG2.
// WARNING: See POWER_UP command in "Si47xx Programming Guide" to determine
// which of these arguments are permitted for each chip and function mode.
enum {
   POWER_UP_AUDIO_OUT_NONE    = 0,  //Disable audio output- Si4749 only
   POWER_UP_AUDIO_OUT_ANALOG  = 0b00000101,  //Enable analog audio output only
   POWER_UP_AUDIO_OUT_DIGITAL = 0b10110000,  //Enable digital audio output only
   POWER_UP_AUDIO_OUT_ANALOG_DIGITAL = 0b10110101,  //Enable analog and digital audio output
};

// Options for begin()
// Multiple options can be combined with bitwise or (|).
enum {
   BEGIN_DEFAULT=0,  //Use default options
   BEGIN_DO_NOT_INIT_BUS=0b1,  //Do not initialize SPI or I2C bus
};

// Maximum volume setting
enum {MAX_VOLUME=63};

// The ITU has divided the world into 3 broadcast regions.  We divide region 2
// into separate subregions for North and South America.
enum {
   REGION_1=0,   //Europe, Africa, and north-west Asia (including Russia)
   REGION_2_NA,  //North America
   REGION_2_SA,  //South America
   REGION_3      //Oceania and south-east Asia
};

// Locale options
// These select different analog receiver settings such as frequency spacing and
// the beginning and end of each band.  Also, selects RDS or RBDS.
// All internal message strings are in English.
// Note: The RDS subroutines will examine the station's country codes to determine
// whether to use RDS or RBDS based in part on the locale.
enum {
   //No special locale is given for the current ITU region.
   LOCALE_OTHER=0,//Use when one of the other locales does not apply.  Can be used with any region.
   //Region 1 locales:
   LOCALE_IT,     //Italy
   //Region 2 NA locales:
   LOCALE_US,     //USA
   LOCALE_CA_MX,  //Canada, Mexico, or any other country (excluding the USA)
                  //that can receive a mix of USA and local (non USA) stations.
   //Region 2 SA locales:
   /* None yet */
   //Region 3 locales:
   LOCALE_JP,     //Japan
   LOCALE_KR,     //South Korea
};

// Threshold for RDS data
// TODO
enum {
   RDS_THRESHOLD=3,      //Threshold for larger variables
   RDS_BOOL_THRESHOLD=7  //Threshold for boolean variables
};

// RDS Extended Country Codes
enum {
   ECC_UNKNOWN=0,
   ECC_US=0xA0,  //USA
};

// RDS Language Codes
enum {
   LANG_UNKNOWN=0,
   LANG_EN=0x09,  //English
};

// Si4735::rds.offset==NO_DATE_TIME means no RDS date/time saved in Si4735 class structure.
enum{NO_DATE_TIME=127};

// Ternary variables:  These variables are similar to boolean/binary variables
// which have two states: true and false.  Ternary variables have three states:
// true, false, and unknown.  For theory on ternary logic see:
// http://en.wikipedia.org/wiki/Three-valued_logic
// To have compatabilty with C++'s bool variables, we define:
//    true=1, false=0, unknown=-1
// With this definition, we can use C++'s 'true' and 'false' keywords with
// ternary variables.
typedef signed char ternary;
enum {unknown=-1};  // Neither true nor false

// Convert high byte / low byte into 16 bit word.
#define MAKE_WORD(hb, lb) ((byte(hb) << 8U) | byte(lb))

typedef struct DateTime {
   word year;
   byte month;
   byte day;
   byte wday;  //Day of the week, Sunday = 0
   byte hour;
   byte minute;
};

typedef struct Time {
   byte hour;
   byte minute;
};

// Filled in by getRSQ() and checkRSQ().  Info comes from FM_RSQ_STATUS and AM_RSQ_STATUS commands.
typedef struct RSQMetrics {
   //FM and AM
   byte interrupts;  //Current RSQ interrupt bits
   bool stereo;      //True=stereophonic, False=monophonic
   bool seekable;    //True if seek can find this station at this moment
   bool softMute;    //True if soft muted engaged
   bool AFCRailed;   //AFC has railed.  Probable meaning: The Automatic Frequency
                     //Control has reached the limit of its ability to keep the
                     //current station tuned.
   byte RSSI;        //Received Signal Strength Indication measured in dBµV
   byte SNR;         //Signal to Noise Ratio measured in dB
   //FM only
   byte stereoBlend; //Stereo blend in percent
   signed char freqOffset;  //Signed frequency offset in kHz
   byte multipath;   //Current multipath metric (0 = no multipath; 100 = full multipath)
                     //(Si4735-D50 or later)
};

/*****************************************
* Si47xx radio command and property info *
*****************************************/

// Bit masks for RSQ interrupts in RSQMetrics->interrupts; RESP1 of FM_RSQ_STATUS, AM_RSQ_STATUS, and
// WB_RSQ_STATUS commands; and FM_RSQ_INT_SOURCE, AM_RSQ_INT_SOURCE, and WB_RSQ_INT_SOURCE properties.
// See FM_RSQ_STATUS, AM_RSQ_STATUS, and WB_RSQ_STATUS commands and FM_RSQ_INT_SOURCE, AM_RSQ_INT_SOURCE,
// and WB_RSQ_INT_SOURCE properties in "Si47xx Programming Guide".
enum {
   //FM only
   RSQ_BLEND_MASK=0b10000000,  //Blend detect
   RSQ_MULTH_MASK=0b00100000,  //Multipath detect high (Si4735-D50 or later)
   RSQ_MULTL_MASK=0b00010000,  //Multipath detect low (Si4735-D50 or later)
   //FM, AM, WB
   RSQ_SNRH_MASK =0b00001000,  //SNR detect high
   RSQ_SNRL_MASK =0b00000100,  //SNR detect low
   RSQ_RSSIH_MASK=0b00000010,  //RSSI detect high
   RSQ_RSSIL_MASK=0b00000001   //RSSI detect low
};

// Bit masks for RDS interrupts. Use with FM_RDS_INT_SOURCE property and RESP1 of FM_RDS_STATUS command.
enum {
   RDS_NEW_B_BLOCK_MASK=0b00100000,  //New B block (Si4735-D50 or later)
   RDS_NEW_A_BLOCK_MASK=0b00010000,  //New A block (Si4735-D50 or later)
   RDS_SYNC_FOUND_MASK =0b00000100,  //RDS sync found
   RDS_SYNC_LOST_MASK  =0b00000010,  //RDS sync lost
   RDS_RECEIVED_MASK   =0b00000001   //FM_RDS_INT_FIFO_COUNT or more packets received
};

// Bit masks for ASQ interrupts.
enum {
   //AUX only
   ASQ_AUX_OVERLOAD_MASK=0b1,  //Audio input has overloaded ADC
   //WB only
   ASQ_WB_ALERT_OFF_MASK=0b10,  //Alert tone lost
   ASQ_WB_ALERT_ON_MASK =0b01,  //Alert tone found
};

// Maximum command and response lengths
enum {
   CMD_MAX_LENGTH=8,
   RESP_MAX_LENGTH=16
};

// Si4735 command codes
enum {
   CMD_POWER_UP       =0x01,
   CMD_GET_REV        =0x10,
   CMD_POWER_DOWN     =0x11,
   CMD_SET_PROPERTY   =0x12,
   CMD_GET_PROPERTY   =0x13,
   CMD_GET_INT_STATUS =0x14,
   CMD_PATCH_ARGS     =0x15,
   CMD_PATCH_DATA     =0x16,
   CMD_GPIO_CTL       =0x80,
   CMD_GPIO_SET       =0x81,
   //FM mode
   CMD_FM_TUNE_FREQ   =0x20,
   CMD_FM_SEEK_START  =0x21,
   CMD_FM_TUNE_STATUS =0x22,
   CMD_FM_RSQ_STATUS  =0x23,
   CMD_FM_RDS_STATUS  =0x24,
   CMD_FM_AGC_STATUS  =0x27,
   CMD_FM_AGC_OVERRIDE=0x28,
   //AM mode
   CMD_AM_TUNE_FREQ   =0x40,
   CMD_AM_SEEK_START  =0x41,
   CMD_AM_TUNE_STATUS =0x42,
   CMD_AM_RSQ_STATUS  =0x43,
   CMD_AM_AGC_STATUS  =0x47,
   CMD_AM_AGC_OVERRIDE=0x48,
   //WB mode - not Si4735
   CMD_WB_TUNE_FREQ   =0x50,
   CMD_WB_TUNE_STATUS =0x52,
   CMD_WB_RSQ_STATUS  =0x53,
   CMD_WB_SAME_STATUS =0x54,  //Si4707 only
   CMD_WB_ASQ_STATUS  =0x55,
   CMD_WB_AGC_STATUS  =0x57,
   CMD_WB_AGC_OVERRIDE=0x58,
   //AUX mode - Si4735-D60 or later
   CMD_AUX_ASRC_START =0x61,
   CMD_AUX_ASQ_STATUS =0x65,
};

// Si4735 property codes
// Check "Si47xx Programming Guide" to determine which chips support which properties.
enum {
   PROP_GPO_IEN                             =0x0001,
   PROP_DIGITAL_OUTPUT_FORMAT               =0x0102,
   PROP_DIGITAL_OUTPUT_SAMPLE_RATE          =0x0104,
   PROP_REFCLK_FREQ                         =0x0201,
   PROP_REFCLK_PRESCALE                     =0x0202,
   PROP_RX_VOLUME                           =0x4000,
   PROP_RX_HARD_MUTE                        =0x4001,
   //FM mode
   PROP_FM_DEEMPHASIS                       =0x1100,
   PROP_FM_CHANNEL_FILTER                   =0x1102,
   PROP_FM_BLEND_STEREO_THRESHOLD           =0x1105,
   PROP_FM_BLEND_MONO_THRESHOLD             =0x1106,
   PROP_FM_MAX_TUNE_ERROR                   =0x1108,
   PROP_FM_RSQ_INT_SOURCE                   =0x1200,
   PROP_FM_RSQ_SNR_HI_THRESHOLD             =0x1201,
   PROP_FM_RSQ_SNR_LO_THRESHOLD             =0x1202,
   PROP_FM_RSQ_RSSI_HI_THRESHOLD            =0x1203,
   PROP_FM_RSQ_RSSI_LO_THRESHOLD            =0x1204,
   PROP_FM_RSQ_MULTIPATH_HI_THRESHOLD       =0x1205,
   PROP_FM_RSQ_MULTIPATH_LO_THRESHOLD       =0x1206,
   PROP_FM_RSQ_BLEND_THRESHOLD              =0x1207,
   PROP_FM_SOFT_MUTE_RATE                   =0x1300,
   PROP_FM_SOFT_MUTE_SLOPE                  =0x1301,
   PROP_FM_SOFT_MUTE_MAX_ATTENUATION        =0x1302,
   PROP_FM_SOFT_MUTE_SNR_THRESHOLD          =0x1303,
   PROP_FM_SOFT_MUTE_RELEASE_RATE           =0x1304,
   PROP_FM_SOFT_MUTE_ATTACK_RATE            =0x1305,
   PROP_FM_SEEK_BAND_BOTTOM                 =0x1400,
   PROP_FM_SEEK_BAND_TOP                    =0x1401,
   PROP_FM_SEEK_FREQ_SPACING                =0x1402,
   PROP_FM_SEEK_TUNE_SNR_THRESHOLD          =0x1403,
   PROP_FM_SEEK_TUNE_RSSI_THRESHOLD         =0x1404,
   PROP_FM_RDS_INT_SOURCE                   =0x1500,
   PROP_FM_RDS_INT_FIFO_COUNT               =0x1501,
   PROP_FM_RDS_CONFIG                       =0x1502,
   PROP_FM_RDS_CONFIDENCE                   =0x1503,
   PROP_FM_BLEND_RSSI_STEREO_THRESHOLD      =0x1800,
   PROP_FM_BLEND_RSSI_MONO_THRESHOLD        =0x1801,
   PROP_FM_BLEND_RSSI_ATTACK_RATE           =0x1802,
   PROP_FM_BLEND_RSSI_RELEASE_RATE          =0x1803,
   PROP_FM_BLEND_SNR_STEREO_THRESHOLD       =0x1804,
   PROP_FM_BLEND_SNR_MONO_THRESHOLD         =0x1805,
   PROP_FM_BLEND_SNR_ATTACK_RATE            =0x1806,
   PROP_FM_BLEND_SNR_RELEASE_RATE           =0x1807,
   PROP_FM_BLEND_MULTIPATH_STEREO_THRESHOLD =0x1808,
   PROP_FM_BLEND_MULTIPATH_MONO_THRESHOLD   =0x1809,
   PROP_FM_BLEND_MULTIPATH_ATTACK_RATE      =0x180A,
   PROP_FM_BLEND_MULTIPATH_RELEASE_RATE     =0x180B,
   PROP_FM_HICUT_SNR_HIGH_THRESHOLD         =0x1A00,
   PROP_FM_HICUT_SNR_LOW_THRESHOLD          =0x1A01,
   PROP_FM_HICUT_ATTACK_RATE                =0x1A02,
   PROP_FM_HICUT_RELEASE_RATE               =0x1A03,
   PROP_FM_HICUT_MULTIPATH_TRIGGER_THRESHOLD=0x1A04,
   PROP_FM_HICUT_MULTIPATH_END_THRESHOLD    =0x1A05,
   PROP_FM_HICUT_CUTOFF_FREQUENCY           =0x1A06,
   //AM mode
   PROP_AM_DEEMPHASIS                       =0x3100,
   PROP_AM_CHANNEL_FILTER                   =0x3102,
   PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN=0x3103,
   PROP_AM_MODE_AFC_SW_PULL_IN_RANGE        =0x3104,
   PROP_AM_MODE_AFC_SW_LOCK_IN_RANGE        =0x3105,
   PROP_AM_RSQ_INT_SOURCE                   =0x3200,
   PROP_AM_RSQ_SNR_HIGH_THRESHOLD           =0x3201,
   PROP_AM_RSQ_SNR_LOW_THRESHOLD            =0x3202,
   PROP_AM_RSQ_RSSI_HIGH_THRESHOLD          =0x3203,
   PROP_AM_RSQ_RSSI_LOW_THRESHOLD           =0x3204,
   PROP_AM_SOFT_MUTE_RATE                   =0x3300,
   PROP_AM_SOFT_MUTE_SLOPE                  =0x3301,
   PROP_AM_SOFT_MUTE_MAX_ATTENUATION        =0x3302,
   PROP_AM_SOFT_MUTE_SNR_THRESHOLD          =0x3303,
   PROP_AM_SOFT_MUTE_RELEASE_RATE           =0x3304,
   PROP_AM_SOFT_MUTE_ATTACK_RATE            =0x3305,
   PROP_AM_SEEK_BAND_BOTTOM                 =0x3400,
   PROP_AM_SEEK_BAND_TOP                    =0x3401,
   PROP_AM_SEEK_FREQ_SPACING                =0x3402,
   PROP_AM_SEEK_TUNE_SNR_THRESHOLD          =0x3403,
   PROP_AM_SEEK_TUNE_RSSI_THRESHOLD         =0x3404,
   //WB mode - not Si4735
   PROP_WB_MAX_TUNE_ERROR                   =0x5108,
   PROP_WB_RSQ_INT_SOURCE                   =0x5200,
   PROP_WB_RSQ_SNR_HI_THRESHOLD             =0x5201,
   PROP_WB_RSQ_SNR_LO_THRESHOLD             =0x5202,
   PROP_WB_RSQ_RSSI_HI_THRESHOLD            =0x5203,
   PROP_WB_RSQ_RSSI_LO_THRESHOLD            =0x5204,
   PROP_WB_VALID_SNR_THRESHOLD              =0x5403,
   PROP_WB_VALID_RSSI_THRESHOLD             =0x5404,
   PROP_WB_SAME_INT_SOURCE                  =0x5500,  //Si4707 only
   PROP_WB_ASQ_INT_SOURCE                   =0x5600,
   //AUX mode - Si4735-D60 or later
   PROP_AUX_ASQ_INT_SOURCE                  =0x6600,
};

// Command arguments
enum {
   //POWER_UP
   /* See POWER_UP_AUDIO_OUT constants above for ARG2. */
   POWER_UP_ARG1_CTSIEN   = 0b10000000,  //CTS interrupt enable
   POWER_UP_ARG1_GPO2OEN  = 0b01000000,  //GPO2/INT output enable
   POWER_UP_ARG1_PATCH    = 0b00100000,  //Patch enable
   POWER_UP_ARG1_XOSCEN   = 0b00010000,  //Enable internal oscillator with external 32768 Hz crystal
   POWER_UP_ARG1_FUNC_FM  = 0x0,  //FM receive mode
   POWER_UP_ARG1_FUNC_AM  = 0x1,  //AM receive mode
   POWER_UP_ARG1_FUNC_TX  = 0x2,  //FM transmit mode - not Si4735 or Si4707
   POWER_UP_ARG1_FUNC_WB  = 0x3,  //WB receive mode - not Si4735
   POWER_UP_ARG1_FUNC_AUX = 0x4,  //Auxiliary input mode - Si4735-D60 or later
   POWER_UP_ARG1_FUNC_REV = 0xF,  //Query chip's hardware and firmware revisions
   //FM_TUNE_FREQ, AM_TUNE_FREQ
   FM_TUNE_FREQ_ARG1_FREEZE = 0b10,
   TUNE_FREQ_ARG1_FAST      = 0b01,  //Fast, inaccurate tune
   //FM_SEEK_START, AM_SEEK_START
   SEEK_START_ARG1_SEEK_UP = 0b1000,  //1 = Seek up, 0 = Seek down
   SEEK_START_ARG1_WRAP    = 0b0100,  //Wrap when band limit reached
   //FM_TUNE_STATUS, AM_TUNE_STATUS, WB_TUNE_STATUS
   TUNE_STATUS_ARG1_CANCEL_SEEK = 0b10,  //Cancel seek operation - not WB
   TUNE_STATUS_ARG1_CLEAR_INT   = 0b01,  //Clear STC interrupt
   //FM_RSQ_STATUS, AM_RSQ_STATUS, WB_RSQ_STATUS
   RSQ_STATUS_ARG1_CLEAR_INT = 0b1,  //Clear RSQ and related interrupts
   //FM_RDS_STATUS
   RDS_STATUS_ARG1_STATUS_ONLY = 0b100,
   RDS_STATUS_ARG1_CLEAR_FIFO  = 0b010,  //Clear RDS receive FIFO
   RDS_STATUS_ARG1_CLEAR_INT   = 0b001,  //Clear RDS interrupt
   //WB_SAME_STATUS
   SAME_STATUS_ARG1_CLEAR_BUFFER = 0b10,  //Clear SAME receive buffer
   SAME_STATUS_ARG1_CLEAR_INT    = 0b01,  //Clear SAME interrupt
   //AUX_ASQ_STATUS, WB_ASQ_STATUS
   ASQ_STATUS_ARG1_CLEAR_INT = 0b1,  //Clear ASQ interrupt
   //FM_AGC_OVERRIDE, AM_AGC_OVERRIDE, WB_AGC_OVERRIDE
   AGC_OVERRIDE_ARG1_DISABLE_AGC = 0b1,  //Disable AGC
   //GPIO_CTL, GPIO_SET
   GPIO_ARG1_GPO3 = 0b1000,  //GPO3
   GPIO_ARG1_GPO2 = 0b0100,  //GPO2
   GPIO_ARG1_GPO1 = 0b0010,  //GPO1
};

// Command responses
// Names that begin with FIELD are argument masks.  Others are argument constants.
enum {
   //FM_TUNE_STATUS, AM_TUNE_STATUS, WB_TUNE_STATUS
   FIELD_TUNE_STATUS_RESP1_SEEK_LIMIT=0b10000000,  //Seek hit search limit - not WB
   FIELD_TUNE_STATUS_RESP1_AFC_RAILED=0b10,  //AFC railed
   FIELD_TUNE_STATUS_RESP1_SEEKABLE  =0b01,  //Station could currently be found by seek,
   FIELD_TUNE_STATUS_RESP1_VALID     =0b01,  //that is, the station is valid
   //FM_RSQ_STATUS, AM_RSQ_STATUS, WB_RSQ_STATUS
   /* See RSQ interrupts above for RESP1. */
   FIELD_RSQ_STATUS_RESP2_SOFT_MUTE =0b1000,  //Soft mute active - not WB
   FIELD_RSQ_STATUS_RESP2_AFC_RAILED=0b0010,  //AFC railed
   FIELD_RSQ_STATUS_RESP2_SEEKABLE  =0b0001,  //Station could currently be found by seek,
   FIELD_RSQ_STATUS_RESP2_VALID     =0b0001,  //that is, the station is valid
   FIELD_RSQ_STATUS_RESP3_STEREO=0b10000000,  //Stereo pilot found - FM only
   FIELD_RSQ_STATUS_RESP3_STEREO_BLEND=0b01111111,  //Stereo blend in % (100 = full stereo, 0 = full mono) - FM only
   //FM_RDS_STATUS
   /* See RDS interrupts above for RESP1. */
   FIELD_RDS_STATUS_RESP2_FIFO_OVERFLOW=0b00000100,  //FIFO overflowed
   FIELD_RDS_STATUS_RESP2_SYNC         =0b00000001,  //RDS currently synchronized
   FIELD_RDS_STATUS_RESP12_BLOCK_A=0b11000000,
   FIELD_RDS_STATUS_RESP12_BLOCK_B=0b00110000,
   FIELD_RDS_STATUS_RESP12_BLOCK_C=0b00001100,
   FIELD_RDS_STATUS_RESP12_BLOCK_D=0b00000011,
   RDS_STATUS_RESP12_BLOCK_A_NO_ERRORS    =0U<<6,  //Block had no errors
   RDS_STATUS_RESP12_BLOCK_A_2_BIT_ERRORS =1U<<6,  //Block had 1-2 bit errors
   RDS_STATUS_RESP12_BLOCK_A_5_BIT_ERRORS =2U<<6,  //Block had 3-5 bit errors
   RDS_STATUS_RESP12_BLOCK_A_UNCORRECTABLE=3U<<6,  //Block was uncorrectable
   RDS_STATUS_RESP12_BLOCK_B_NO_ERRORS    =0U<<4,
   RDS_STATUS_RESP12_BLOCK_B_2_BIT_ERRORS =1U<<4,
   RDS_STATUS_RESP12_BLOCK_B_5_BIT_ERRORS =2U<<4,
   RDS_STATUS_RESP12_BLOCK_B_UNCORRECTABLE=3U<<4,
   RDS_STATUS_RESP12_BLOCK_C_NO_ERRORS    =0U<<2,
   RDS_STATUS_RESP12_BLOCK_C_2_BIT_ERRORS =1U<<2,
   RDS_STATUS_RESP12_BLOCK_C_5_BIT_ERRORS =2U<<2,
   RDS_STATUS_RESP12_BLOCK_C_UNCORRECTABLE=3U<<2,
   RDS_STATUS_RESP12_BLOCK_D_NO_ERRORS    =0U<<0,
   RDS_STATUS_RESP12_BLOCK_D_2_BIT_ERRORS =1U<<0,
   RDS_STATUS_RESP12_BLOCK_D_5_BIT_ERRORS =2U<<0,
   RDS_STATUS_RESP12_BLOCK_D_UNCORRECTABLE=3U<<0,
   //WB_SAME_STATUS - TODO

   //AUX_ASQ_STATUS, WB_ASQ_STATUS
   /* See ASQ interrupts above for RESP1. */
   FIELD_AUX_ASQ_STATUS_RESP2_OVERLOAD=0b1,  //Audio input is currently overloading ADC
   FIELD_WB_ASQ_STATUS_RESP2_ALERT    =0b1,  //Alert tone is present
   //FM_AGC_STATUS, AM_AGC_STATUS, WB_AGC_STATUS
   FIELD_AGC_STATUS_RESP1_DISABLE_AGC=0b1,  //True if AGC disabled
};

// Property arguments - TODO incomplete
// Names that begin with FIELD are argument masks.  Others are argument constants.
enum {
   //FM_DEEMPHASIS
   FIELD_FM_DEEMPHASIS_ARG=0b11,
   FM_DEEMPHASIS_ARG_75=0b10,  //75 μs (default)
   FM_DEEMPHASIS_ARG_50=0b01,  //50 μs
   //FM_RDS_INT_SOURCE
   /* See RDS interrupts above. */
   //FM_RDS_CONFIG
   FIELD_FM_RDS_CONFIG_ARG_BLOCK_A=0b11000000<<8,
   FIELD_FM_RDS_CONFIG_ARG_BLOCK_B=0b00110000<<8,
   FIELD_FM_RDS_CONFIG_ARG_BLOCK_C=0b00001100<<8,
   FIELD_FM_RDS_CONFIG_ARG_BLOCK_D=0b00000011<<8,
   FM_RDS_CONFIG_ARG_BLOCK_A_NO_ERRORS    =0U<<14,  //Block must have no errors
   FM_RDS_CONFIG_ARG_BLOCK_A_2_BIT_ERRORS =1U<<14,  //Block may have up to 2 bit errors
   FM_RDS_CONFIG_ARG_BLOCK_A_5_BIT_ERRORS =2U<<14,  //Block may have up to 5 bit errors
   FM_RDS_CONFIG_ARG_BLOCK_A_UNCORRECTABLE=3U<<14,  //Block may be uncorrectable
   FM_RDS_CONFIG_ARG_BLOCK_B_NO_ERRORS    =0U<<12,
   FM_RDS_CONFIG_ARG_BLOCK_B_2_BIT_ERRORS =1U<<12,
   FM_RDS_CONFIG_ARG_BLOCK_B_5_BIT_ERRORS =2U<<12,
   FM_RDS_CONFIG_ARG_BLOCK_B_UNCORRECTABLE=3U<<12,
   FM_RDS_CONFIG_ARG_BLOCK_C_NO_ERRORS    =0U<<10,
   FM_RDS_CONFIG_ARG_BLOCK_C_2_BIT_ERRORS =1U<<10,
   FM_RDS_CONFIG_ARG_BLOCK_C_5_BIT_ERRORS =2U<<10,
   FM_RDS_CONFIG_ARG_BLOCK_C_UNCORRECTABLE=3U<<10,
   FM_RDS_CONFIG_ARG_BLOCK_D_NO_ERRORS    =0U<<8,
   FM_RDS_CONFIG_ARG_BLOCK_D_2_BIT_ERRORS =1U<<8,
   FM_RDS_CONFIG_ARG_BLOCK_D_5_BIT_ERRORS =2U<<8,
   FM_RDS_CONFIG_ARG_BLOCK_D_UNCORRECTABLE=3U<<8,
   FM_RDS_CONFIG_ARG_ENABLE=0b1,  //Enable RDS
   //FM_RSQ_INT_SOURCE, AM_RSQ_INT_SOURCE, WB_RSQ_INT_SOURCE
   /* See RSQ interrupts above. */
   //AUX_ASQ_INT_SOURCE, WB_ASQ_INT_SOURCE
   /* See ASQ interrupts above. */
};

/* The normal sequence for using the Si4735 class library is:
 *
 *    Si4735 radio;  // Create object
 *
 *    void setup(){
 *       // Initialize library and Si4735 chip, returns with radio in low power "off" state.
 *       radio.begin();
 *       // Optionally setup location, defaults to USA.  Can also be called in loop() below.
 *       radio.setRegionAndLocale(region, locale);
 *       // Optionally set initial volume.
 *       radio.setVolume(32);
 *
 *       // do other initialization...
 *
 *    }
 *
 *    void loop(){
 *       radio.setMode(mode);  // Switch radio to given receive mode.
 *
 *       // call other radio methods...
 *
 *       radio.end();  // Finished with radio
 *    }
 */
class Si4735 {
   public:
      /* The Si4735 class constructor to initialize a new object. */
      Si4735();

      /* Applies power to and resets the radio chip.
       * Parameters:
       *  options - Options for initializing.  See definitions above and below.
       *  bus_arg - SPI: Clock divider.  I2C: radio's address.  0 == default.
       * Options include:
       *  BEGIN_DO_NOT_INIT_BUS - Do not initialize SPI or I2C bus.
       * Warning: If BEGIN_DO_NOT_INIT_BUS is given then the SPI or I2C bus must be previously
       * initialized by calling SPI.begin() and SPI.setClockDivider() or Wire.begin().
       */
      void begin(byte options=BEGIN_DEFAULT, byte bus_arg=0);

      /* Removes power from radio.  Call begin() to restart radio after calling end(). */
      void end(void);

      /* Sets up the radio in the desired mode, and limits the frequency band based on locale.
       * The radio must be set to one of these receive modes before other radio commands can be given.
       * The bands are set as follows.  Some values are approximate.
       *   FM: 87.5 - 108.0 MHz (87.5-107.9 for NA/SA, 76-90 for Japan, 64-108 if options==MODE_FM_OPT_FULL_BAND)
       *   AM:  520 -  1710 kHz
       *   SW: 1710 - 23000 kHz
       *   LW:  153 -   279 kHz (Region 1)
       * The user must ensure that the antenna switch on the shield is configured for the desired mode.
       * Parameters:
       *  mode - The desired radio mode [RADIO_OFF,AM,FM,SW,LW].
       *  options - Options for the given mode.  See constants defined above.
       *  audio_mode - ARG2 for POWER_UP command.  Ignored when mode==RADIO_OFF.
       *               Use default value with shield.  See constants defined above.
       */
      void setMode(byte mode, byte options=MODE_OPT_DEFAULT, byte audio_mode=POWER_UP_AUDIO_OUT_ANALOG);

      /* Gets the current mode of the radio [RADIO_OFF,AM,FM,SW,LW]. */
      byte getMode(void);

      /* Tune the radio to the given frequency.  The frequency is in kHz for all AM modes
       * or in 10 kHz in FM mode.  After calling, you should update the user interface and
       * then call waitSTC() or checkFrequency() to wait for the tune operation to complete.
       * WARNING: The Si4735-C40 data sheet in section 5.15 "Reference Clock" on page 24
       * (Rev. 1.0) warns that you should avoid any serial traffic to either the Si4735 or
       * any other chip sharing the same bus while a tune or seek operation is active.
       * (This warning applies when the internal oscillator is used with a crystal attached
       * to the radio's RCLK and GPO3 pins.  The SparkFun shield does use this setup.)
       * As a result, after calling tuneFrequency(), frequencyUp(), frequencyDown(), seekUp(),
       * or seekDown(), you should not send any commands to the Si4735 or other chips on the
       * same bus until the STC (Seek/Tune Complete) interrupt has been received.
       */
      void tuneFrequency(word frequency);

      /* Equivalent to tuneFrequency() followed by waitSTC(). */
      void tuneFrequencyAndWait(word frequency);

      /* Increments the currently tuned frequency by the current spacing returned by getSpacing().
       * If the new frequency would exceed the top of band, the frequency wraps to the bottom.
       * The frequency is in kHz for all AM modes or in 10 kHz in FM mode.  Tuning is done by
       * calling tuneFrequency().  As a result, you must wait for the STC interrupt after calling
       * frequencyUp().  See warning for tuneFrequency().
       * Returns the newly tuned frequency.
       */
      word frequencyUp(void);

      /* Equivalent to frequencyUp() followed by waitSTC(). */
      word frequencyUpAndWait(void);

      /* Decrements the currently tuned frequency by the current spacing returned by getSpacing().
       * If the new frequency would exceed the bottom of band, the frequency wraps to the top.
       * Otherwise identical to frequencyUp().  See warning for tuneFrequency().
       * Returns the newly tuned frequency.
       */
      word frequencyDown(void);

      /* Equivalent to frequencyDown() followed by waitSTC(). */
      word frequencyDownAndWait(void);

      /* Wait for STC (Seek/Tune Complete) interrupt from radio chip.  Returns with STC
       * interrupt cleared.
       */
      void waitSTC(void);

      /* Commands the radio to seek up to the next valid channel. If the top of
       * the band is reached, seek will continue from the bottom of the band.
       * After returning, you should poll checkFrequency() to find when the seek completes.
       * In between polling, you should also check if the user wants to cancel the current
       * seek operation.  If so, you should terminate the seek by calling cancelSeek().
       * See warning for tuneFrequency().
       */
      void seekUp(void);

      /* Commands the radio to seek down to the next valid channel. If the bottom
       * of the band is reached, seek will continue from the top of the band.
       * Otherwise identical to seekUp().  See warning for tuneFrequency().
       */
      void seekDown(void);

      /* Tell radio to cancel seek operation.  Returns the frequency of the currently
       * tuned station and clears the STC interrupt.
       */
      word cancelSeek(void);

      /* First checks if STC interrupt received yet.  If not, returns 0.  Otherwise,
       * returns the frequency of the currently tuned station and clears the STC interrupt.
       * The frequency is in kHz for all AM modes or in 10 kHz in FM mode.
       */
      word checkFrequency(void);

      /* Immediately ask the radio and return the frequency of the currently tuned station.
       * The frequency is in kHz for all AM modes or in 10 kHz in FM mode.  Clears the
       * STC interrupt if clearSTC argument is true.
       */
      word getFrequency(bool clearSTC=false);

      /* Returns the saved frequency of the currently tuned station.  Returns 0 if unknown,
       * usually because a seek is in progress.  The frequency is in kHz for all AM modes
       * or in 10 kHz in FM mode.
       * Warning: currentFrequency() does not indicate if the previous tune or seek operation
       * has completed.  (In other words, it does not tell you if the STC interrupt has been
       * received.) It only returns the frequency from the last tune operation or call to
       * getFrequency() or currentFrequency().
       */
      word currentFrequency();

      /* Collects RDS information from radio chip.  Returns true if new info found.
       * Only works in FM mode.  Collected info is located below in the 'rds' structure
       * inside this class object.
       */
      bool getRDS(void);

      /* Equivalent to getRDS() but first checks RDS interrupt for new RDS data. */
      bool checkRDS(void);

      /* Saves the call sign derived from the current RBDS PI code in the given 5 character
       * buffer.  Returns true if buffer has a valid call sign.  Otherwise, returns false and
       * the buffer is initialized with fill characters.
       * Only provides meaningful info if mode==FM and station is using RBDS.
       */
      bool getCallSign(char callSign[5]);

      /* Translates the current Program Type code into a 16 character English message.
       * Message string is saved in given 17 character buffer.
       */
      void getProgramTypeStr(char text[17]);

      /* Clears RDS station info so that data from previous stations are not overlayed on
       * the current station.  Automatically called when the frequency is changed.
       */
      void clearStationInfo(void);

      /* Retrieves the last date and time broadcasted from the tuned station and
       * writes the local date and time to the given structure.
       * Returns true if station has broadcast date and time at least once,
       * otherwise, it returns false and writes nothing to the structure.
       */
      bool getLocalDateTime(DateTime *date_time);

      /* Retrieves the last time broadcasted from the tuned station and
       * writes the local time to the given structure.
       * Returns true if station has broadcast date and time at least once,
       * otherwise, it returns false and writes nothing to the structure.
       */
      bool getLocalTime(Time *time);

      /* Retrieves the Received Signal Quality parameters/metrics. */
      void getRSQ(RSQMetrics *RSQ);

      /* Retrieves the Received Signal Quality parameters/metrics if the RSQ interrupt has been received.
       * You must use setProperty() to configure what conditions will trigger the RSQ interrupt.
       * See the various RSQ properties in the "Si47xx Programming Guide" for more information.
       * Returns true if RSQMetrics structure has data, or false if no data written.
       */
      bool checkRSQ(RSQMetrics *RSQ);

      /* Sets the volume. If argument is out of the 0 - MAX_VOLUME range, no change will be made.
       * Returns new volume.  This and other volume methods may be called while mode==RADIO_OFF.
       */
      byte setVolume(byte value);

      /* Gets the current volume. */
      byte getVolume(void);

      /* Increases the volume by number given.  Will not increase volume above MAX_VOLUME.
       * Defaults to increase by 1.  Returns new volume.
       */
      byte volumeUp(byte inc=1);

      /* Decreases the volume by number given.  Will not decrease volume below 0.
       * Defaults to decrease by 1.  Returns new volume.
       */
      byte volumeDown(byte dec=1);

      /* Mutes the audio output.  This and other mute methods may be called while mode==RADIO_OFF. */
      void mute(void);

      /* Disables mute. */
      void unmute(void);

      /* Toggles mute and returns new mute status. */
      bool toggleMute(void);

      /* Returns mute status. */
      bool getMute(void);

      /* Sends a command packet to the Si4735.  See the "Si47xx Programming Guide" for a description
       * of the commands and their responses.
       * Parameters:
       *  command - Command to be sent to the radio.
       *  length - Number of bytes in command packet.  Maximum length is CMD_MAX_LENGTH.
       */
      void sendCommand(const byte *command, byte length);

      #ifdef __AVR__
      /* Same as sendCommand() but command is located in flash ROM (PROGMEM). */
      void sendCommand_P(const byte PROGMEM *command_P, byte length);
      #else
      /* Dummy version for ARM based Arduinos. */
      #define sendCommand_P(command, length) sendCommand((command), (length))
      #endif

      /* Used to send an ASCII hexadecimal command string to the radio.
       * Parameters:
       *  myCommand - A null terminated ASCII string consisting of hexadecimal characters.
       *  The string is converted into raw bytes and sent to the radio module.  For debugging
       *  or advanced users.  The command format can be found in the "Si47xx Programming Guide."
       */
      void sendCommand(const char *myCommand);

      /* Gets the long response (16 bytes) from the radio.  The response is written
       * to the given buffer.  Only those bytes that will fit are written.
       * See "Si47xx Programming Guide" for more info on responses.
       * Parameters:
       *  response - Buffer to hold response from radio.
       *  length - Number of bytes in response buffer.  Maximum length is RESP_MAX_LENGTH.
       */
      void getResponse(byte *response, byte length);

      /* Reads the 1 byte status code from radio and returns it.
       * Warning: The status byte is supposed to contain the radio's current interrupt status.
       * However, most commands do not reliably update the status code.  The only interrupt
       * always kept up-to-date is CTS.  To get an accurate copy of the radio's other interrupts,
       * call getInterrupts() or currentInterrupts().
       */
      byte getStatus(void);

      /* Reads the 1 byte interrupt status from radio and returns it:
       *    Bit   Interrupt
       *    ---------------
       *     7    CTS  - Clear To Send
       *     6    ERR  - Error occurred
       *     3    RSQ  - Received Signal Quality measurement has triggered
       *     2    RDS  - RDS data received (FM mode only)
       *     2    SAME - SAME data received (WB mode on Si4707 only)
       *     1    ASQ  - Audio Signal Quality (WB or AUX modes only)
       *     0    STC  - Seek/Tune Complete
       */
      byte getInterrupts(void);

      /* If an interrupt signal has been received, it calls getInterrupts() to get the new interrupt byte.
       * Otherwise, it returns the previously received interrupt byte from the radio.
       */
      byte currentInterrupts(void);

      /* Clears the given interrupt masks.
       * This method is public for applications that send their own custom commands to the radio
       * by calling sendCommand().  Some commands can optionally clear an interrupt.  If such a command
       * is sent to the radio, you MUST manually tell this class that this has happened by calling
       * clearInterrupts() with the interrupt masks of the cleared interrupts.  This is required
       * because this class saves a private copy of the radio's interrupts which is only updated when a
       * new interrupt signal is received from the radio.
       * Note: If you do not send custom commands with sendCommand() or if your commands do not clear
       * any interrupts, then you do not need to call clearInterrupts()!
       */
      void clearInterrupts(byte interrupt_mask);

      /* Set given property. */
      void setProperty(word property, word value);

      /* Get given property. */
      word getProperty(word property);

      /* Set top/bottom of receive band.  Overides setMode()'s default.
       * Frequency is measured in kHz for AM, SW, LW and in 10 kHz increments for FM.
       */
      void setBandTop(word top);
      void setBandBottom(word bottom);

      /* Set frequency spacing.  Overides setMode()'s default.
       * Frequency is measured in kHz for AM, SW, LW and in 10 kHz increments for FM.
       */
      void setSpacing(word spacing);

      /* Get top/bottom of receive band. */
      word getBandTop();
      word getBandBottom();

      /* Get frequency spacing. */
      word getSpacing();

      /* Set the ITU region and locale.  Should be called while radio's mode==RADIO_OFF.
       * The region and locale constants are defined above.
       * Warning: The region and locale MUST agree or unpredictable behavior will result.
       */
      void setRegionAndLocale(byte region, byte locale=LOCALE_OTHER);

      /* Get the region. */
      byte getRegion(void);

      /* Get the locale. */
      byte getLocale(void);

      /* setMode() saves the responce from the GET_REV command here. */
      struct{
         byte partNumber;  //Last two digits of chip's part number in decimal (00-99)
         byte firmwareMajor;   //Firmware major revision in ASCII
         byte firmwareMinor;   //Firmware minor revision in ASCII
         byte componentMajor;  //Component major revision in ASCII
         byte componentMinor;  //Component minor revision in ASCII
         byte chip;            //Chip revision in ASCII
      } revision;

      /* RDS and RBDS data */
      struct{
         word programId;            //Program Identification (PI) code - unique code assigned to program.
                                    //In the US, except for simulcast stations, each station has a unique PI.
                                    //PI = 0 if no RDS info received.
         /* groupA and groupB indicate if the station has broadcast one or more of each RDS group type and version.
          * There is one bit for each group type.  Bit number 0 is for group type 0, and so on.
          * groupA gives version A groups (packets), groupB gives version B groups.
          * If a bit is true then one or more of that group type and version has been received.
          * Example:  If (groupA & 1<<4) is true then at least one Group type 4, version A group (packet)
          * has been received.
          * Note: If the RDS signal is weak, many bad packets will be received.  Sometimes, the packets are so
          * corrupted that the radio thinks the bad data is OK.  This can cause false information to be recorded
          * in the groupA and groupB variables.
          */
         word groupA;               //One bit for each group type, version A
         word groupB;               //One bit for each group type, version B
         bool RDSSignal;            //True if RDS (or RBDS) signal currently detected
         bool RBDS;                 //True if station using RBDS, else using RDS
         byte programType;          //Program Type (PTY) code - identifies program format - call getProgramTypeStr()
         byte extendedCountryCode;  //Extended Country Code (ECC) - constants defined above
         byte language;             //Language Code - constants defined above
         ternary trafficProgram;    //Traffic Program flag - True if station gives Traffic Alerts
         ternary trafficAlert;      //Traffic Alert flag - True if station currently broadcasting Traffic Alert
         ternary music;             //Music/speech flag - True if broadcasting music, false if speech
         ternary dynamicPTY;        //Dynamic PTY flag - True if dynamic (changing) PTY, false if static PTY
         ternary compressedAudio;   //Compressed audio flag - True if compressed audio, false if not compressed
         ternary binauralAudio;     //Binaural audio flag - True if binaural audio, false if not binaural audio
         ternary RDSStereo;         //RDS stereo/mono flag - True if RDS info says station is stereo, false if mono
         char    programService[9]; //Station's name or slogan - usually used like Radio Text
         byte    radioTextLen;      //Length of Radio Text message
         char    radioText[65];     //Descriptive message from station
         char    programTypeName[9];//Program Type Name (PTYN)
         unsigned long MJD;         //UTC Modified Julian Date - origin is November 17, 1858
         byte hour;                 //UTC Hour
         byte minute;               //UTC Minute
         signed char offset;        //Offset measured in half hours to convert UTC to local time.
                                    //If offset==NO_DATE_TIME then MJD, hour, minute are invalid.
      } rds;

   private:
      word _frequency;            //Current tuned frequency - 0 if unknown or no frequency tuned
      word _top, _bottom;         //Band limits
      word _spacing;              //Frequency spacing
      byte _mode;                 //Current radio mode [RADIO_OFF,AM,FM,SW,LW]
      byte _region;               //Current ITU region
      byte _locale;               //Current locale within region
      byte _volume;               //Current volume
      bool _mute;                 //Current mute status
      byte _interrupts;           //Current radio interrupt status
      #ifndef Si47xx_SPI  //I2C
      byte _address;              //Radio's I2C address
      #endif
      /* RDS and RBDS data */
      ternary _abRadioText;       //Indicates new radioText[] string
      ternary _abProgramTypeName; //Indicates new programTypeName[] string
      /* RDS data counters */
      byte _extendedCountryCode_count;
      byte _language_count;
      /* Working buffer that can be used to build a command packet or get a response. */
      byte _buffer[CMD_MAX_LENGTH];  //Length must be CMD_MAX_LENGTH or more
      /* Set radio's volume */
      void set_volume(void);
      /* Do TUNE_STATUS command.  Returns radio's current frequency. */
      word tune_status(byte arg);
      /* Do SEEK_START command. */
      void seek_start(byte arg);
      /* Returns true if station using RBDS, false if using RDS */
      bool check_if_RBDS(void);
};

#endif
