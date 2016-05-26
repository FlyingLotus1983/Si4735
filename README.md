# si4735
Arduino library for the (now obsolete) Si4735 FM/AM/SW/LW radio receiver.  This is a git fork of [Michael Kennedy's](michael.joseph.kennedy.1970 [_a_t_] gmail [dot] com) [fork](https://sourceforge.net/projects/arduino-si4735/files/), which is based off of previous work:

* Ryan Owens, https://www.sparkfun.com/products/10342
* Wagner Sartori Junior, https://github.com/trunet/Si4735
* Jon Carrier, https://github.com/jjcarrier/Si4735

This library can be used with the following SparkFun boards:

* DEV-10342, “SI4735 AM & FM Receiver Shield,” http://www.sparkfun.com/products/10342
* WRL-10906, “Si4735 FM/AM Radio Receiver Breakout,” http://www.sparkfun.com/products/109061

Both contain a Si4735-C40 radio receiver chip. The library was tested with a shield marked on the bottom with a date of "4/14/11."
The Si4735 supports all modes available in the Si47xx family except forWeather Band (WB) receive and FM transmit. As a result, most chips in this family can be used with this library, if you are careful to not access missing modes. In some cases, small modifications to the library may be needed for full compatibility. Note: We do not yet support the auxiliary input mode found on the Si4735-D60 or later. Also note: The Si4700/ 01/02/03/08/09 chips are older and use a completely different command syntax. Therefore, they cannot be
used with this library. This library supports all current Arduinos including the 2009 (Duemilanove), Uno, Mega, Mega 2560,
Leonardo, and Due.
