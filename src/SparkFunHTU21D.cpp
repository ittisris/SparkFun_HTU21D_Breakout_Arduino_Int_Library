/*
  HTU21D Humidity Sensor Library
  By: Nathan Seidle
  SparkFun Electronics
  Date: September 22nd, 2013
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

  This library allows an Arduino to read from the HTU21D low-cost high-precision humidity sensor.

  If you have feature suggestions or need support please use the github support page: https://github.com/sparkfun/HTU21D

  Hardware Setup: The HTU21D lives on the I2C bus. Attach the SDA pin to A4, SCL to A5. If you are using the SparkFun
  breakout board you *do not* need 4.7k pull-up resistors on the bus (they are built-in).

  Link to the breakout board product:

  Software:
  Call HTU21D.Begin() in setup.
  HTU21D.ReadHumidity() will return a float containing the humidity. Ex: 54.7
  HTU21D.ReadTemperature() will return a float containing the temperature in Celsius. Ex: 24.1
  HTU21D.SetResolution(byte: 0b.76543210) sets the resolution of the readings.
  HTU21D.check_crc(message, check_value) verifies the 8-bit CRC generated by the sensor
  HTU21D.read_user_register() returns the user register. Used to set resolution.
*/

#include <Wire.h>

#include "SparkFunHTU21D.h"

HTU21D::HTU21D()
{
  //Set initial values for private vars
}

//Begin
/*******************************************************************************************/
//Start I2C communication
void HTU21D::begin(void)
{
  Wire.begin();
}

#define MAX_WAIT 100
#define DELAY_INTERVAL 10
#define MAX_COUNTER (MAX_WAIT/DELAY_INTERVAL)

#define ERROR_I2C_TIMEOUT 	998
#define ERROR_BAD_CRC		999

//Given a command, reads a given 2-byte value with CRC from the HTU21D
uint16_t HTU21D::readValue(byte cmd)
{
  //Request a humidity reading
  Wire.beginTransmission(HTU21D_ADDRESS);
  Wire.write(cmd); //Measure value (prefer no hold!)
  Wire.endTransmission();

  //Hang out while measurement is taken. datasheet says 50ms, practice may call for more
  byte toRead;
  byte counter;
  for (counter = 0, toRead = 0 ; counter < MAX_COUNTER && toRead != 3 ; counter++)
  {
    delay(DELAY_INTERVAL);

    //Comes back in three bytes, data(MSB) / data(LSB) / Checksum
    toRead = Wire.requestFrom(HTU21D_ADDRESS, 3);
  }

  if (counter == MAX_COUNTER) return (ERROR_I2C_TIMEOUT); //Error out

  byte msb, lsb, checksum;
  msb = Wire.read();
  lsb = Wire.read();
  checksum = Wire.read();

  uint16_t rawValue = ((uint16_t) msb << 8) | (uint16_t) lsb;

  if (checkCRC(rawValue, checksum) != 0) return (ERROR_BAD_CRC); //Error out

  return rawValue & 0xFFFC; // Zero out the status bits
}

//Read the humidity
/*******************************************************************************************/
//Calc humidity and return it to the user
//Returns 998 if I2C timed out
//Returns 999 if CRC is wrong
float HTU21D::readHumidity(void)
{
  uint16_t rawHumidity = readValue(TRIGGER_HUMD_MEASURE_NOHOLD);
  
  if(rawHumidity == ERROR_I2C_TIMEOUT || rawHumidity == ERROR_BAD_CRC) return(rawHumidity);

  //Given the raw humidity data, calculate the actual relative humidity
  float tempRH = rawHumidity * (125.0 / 65536.0); //2^16 = 65536
  float rh = tempRH - 6.0; //From page 14

  return (rh);
}

//Read the temperature
/*******************************************************************************************/
//Calc temperature and return it to the user
//Returns 998 if I2C timed out
//Returns 999 if CRC is wrong
float HTU21D::readTemperature(void)
{
  uint16_t rawTemperature = readValue(TRIGGER_TEMP_MEASURE_NOHOLD);

  if(rawTemperature == ERROR_I2C_TIMEOUT || rawTemperature == ERROR_BAD_CRC) return(rawTemperature);

  //Given the raw temperature data, calculate the actual temperature
  float tempTemperature = rawTemperature * (175.72 / 65536.0); //2^16 = 65536
  float realTemperature = tempTemperature - 46.85; //From page 14

  return (realTemperature);
}

//Set sensor resolution
/*******************************************************************************************/
//Sets the sensor resolution to one of four levels
//Page 12:
// 0/0 = 12bit RH, 14bit Temp
// 0/1 = 8bit RH, 12bit Temp
// 1/0 = 10bit RH, 13bit Temp
// 1/1 = 11bit RH, 11bit Temp
//Power on default is 0/0

void HTU21D::setResolution(byte resolution)
{
  byte userRegister = readUserRegister(); //Go get the current register state
  userRegister &= B01111110; //Turn off the resolution bits
  resolution &= B10000001; //Turn off all other bits but resolution bits
  userRegister |= resolution; //Mask in the requested resolution bits

  //Request a write to user register
  writeUserRegister(userRegister);
}

//Read the user register
byte HTU21D::readUserRegister(void)
{
  byte userRegister;

  //Request the user register
  Wire.beginTransmission(HTU21D_ADDRESS);
  Wire.write(READ_USER_REG); //Read the user register
  Wire.endTransmission();

  //Read result
  Wire.requestFrom(HTU21D_ADDRESS, 1);

  userRegister = Wire.read();

  return (userRegister);
}

void HTU21D::writeUserRegister(byte val)
{
  Wire.beginTransmission(HTU21D_ADDRESS);
  Wire.write(WRITE_USER_REG); //Write to the user register
  Wire.write(val); //Write the new resolution bits
  Wire.endTransmission();
}

//Give this function the 2 byte message (measurement) and the check_value byte from the HTU21D
//If it returns 0, then the transmission was good
//If it returns something other than 0, then the communication was corrupted
//From: http://www.nongnu.org/avr-libc/user-manual/group__util__crc.html
//POLYNOMIAL = 0x0131 = x^8 + x^5 + x^4 + 1 : http://en.wikipedia.org/wiki/Computation_of_cyclic_redundancy_checks
#define SHIFTED_DIVISOR 0x988000 //This is the 0x0131 polynomial shifted to farthest left of three bytes

byte HTU21D::checkCRC(uint16_t message_from_sensor, uint8_t check_value_from_sensor)
{
  //Test cases from datasheet:
  //message = 0xDC, checkvalue is 0x79
  //message = 0x683A, checkvalue is 0x7C
  //message = 0x4E85, checkvalue is 0x6B

  uint32_t remainder = (uint32_t)message_from_sensor << 8; //Pad with 8 bits because we have to add in the check value
  remainder |= check_value_from_sensor; //Add on the check value

  uint32_t divsor = (uint32_t)SHIFTED_DIVISOR;

  for (int i = 0 ; i < 16 ; i++) //Operate on only 16 positions of max 24. The remaining 8 are our remainder and should be zero when we're done.
  {
    //Serial.print("remainder: ");
    //Serial.println(remainder, BIN);
    //Serial.print("divsor:    ");
    //Serial.println(divsor, BIN);
    //Serial.println();

    if ( remainder & (uint32_t)1 << (23 - i) ) //Check if there is a one in the left position
      remainder ^= divsor;

    divsor >>= 1; //Rotate the divsor max 16 times so that we have 8 bits left of a remainder
  }

  return (byte)remainder;
}