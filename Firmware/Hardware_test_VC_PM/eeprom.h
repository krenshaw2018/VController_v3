// ************************ External EEPROM data addresses ***********************
#ifndef EEPROM_H
#define EEPROM_H

#define EEPROM_ADDRESS 0x50    // i2c address of 24LC512 eeprom chip
#define EEPROM_DELAY_LENGTH 5  // time between EEPROM writes (usually 5 ms is OK)
unsigned long WriteDelay = 0;

// Low level read/write of the 24LC512

void EEPROM_delay() {
  // Will delay if last message was within EEPROM_DELAY_LENGTH (5 ms)
  while (millis() - WriteDelay <= EEPROM_DELAY_LENGTH) {}
  WriteDelay = millis();
}

void EEPROM_wait_ready() { // Pause until delay has expired
  while (millis() - WriteDelay <= EEPROM_DELAY_LENGTH) {};
}

byte read_ext_EEPROM(unsigned int eeaddress )
// Reads a single byte from 24LC512 chip
{
  EEPROM_wait_ready();
  byte rdata = 0xFF;

  Wire.beginTransmission(EEPROM_ADDRESS);
  Wire.send((int)(eeaddress >> 8));   // MSB
  Wire.send((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();

  Wire.requestFrom(EEPROM_ADDRESS, 1);

  if (Wire.available()) rdata = Wire.receive();

  return rdata;
}

void write_ext_EEPROM(unsigned int eeaddress, byte data )
// Write a single byte to 24LC512 chip
{
  if (data != read_ext_EEPROM(eeaddress)) {

    EEPROM_delay();

    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.send((int)(eeaddress >> 8));   // MSB
    Wire.send((int)(eeaddress & 0xFF)); // LSB
    Wire.send(data);
    Wire.endTransmission();
  }
}

#endif
