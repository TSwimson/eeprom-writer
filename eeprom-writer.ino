// EEPROM Programmer - code for an Arduino Mega 2560
//
// Written by K Adcock.
//       Jan 2016 - Initial release
//       Dec 2017 - Slide code tartups, to remove compiler errors for new Arduino IDE (1.8.5).
//   7th Dec 2017 - Updates from Dave Curran of Tynemouth Software, adding commands to enable/disable SDP.
//  10th Dec 2017 - Fixed one-byte EEPROM corruption (always byte 0) when unprotecting an EEPROM
//                  (doesn't matter if you write a ROM immediately after, but does matter if you use -unprotect in isolation)
//                - refactored code a bit (split loop() into different functions)
//                - properly looked at timings on the Atmel datasheet, and worked out that my delays
//                  during reads and writes were about 10,000 times too big!
//                  Reading and writing is now orders-of-magnitude quicker.
//
// Distributed under an acknowledgement licence, because I'm a shallow, attention-seeking tart. :)
//
// http://danceswithferrets.org/geekblog/?page_id=903
//
// This software presents a 9600-8N1 serial port.
//
// R[hex address]                         - reads 16 bytes of data from the EEPROM
// W[hex address]:[data in two-char hex]  - writes up to 16 bytes of data to the EEPROM
// P                                      - set write-protection bit (Atmels only, AFAIK)
// U                                      - clear write-protection bit (ditto)
// V                                      - prints the version string
//
// Any data read from the EEPROM will have a CRC checksum appended to it (separated by a comma).
// If a string of data is sent with an optional checksum, then this will be checked
// before anything is written.
//


/*
  rd,wr,rom_wr,iorq,mrq,rst,bsrq,bsack

  make arduino not affect anything:
  set addr and data as input
  set rd,wr,iorq,mrq as input
  set bsack as input

  prepare for bus take over
  set rom_wr,rst,bsrq as output
  set rom_wr,rst,bsrq high

  reset and request bus
  set rst low
  set bsrq low
  wait
  set reset high
  wait for busack to go low

  prepare to control bus
  set rd,wr,iorq,mrq as output

  activate memory
  set rd,wr high
  set iorq high
  set mrq low

  do the normal write (using rom_wr, and rd)

  prepare to give bus back
  set rd,wr,iorq,mrq,rom_wr,addr,data as input
  set bsrq as input

  reset z80
  set reset low
  wait
  set reset as input
*/

#include <avr/pgmspace.h>

const char hex[] =
{
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

const char version_string[] = {"EEPROM Version=0.02"};

// For the old thing
// static const int pin_Addr14  = 24;
// static const int pin_Addr12  = 26;
// static const int pin_Addr7   = 28;
// static const int pin_Addr6   = 30;
// static const int pin_Addr5   = 32;
// static const int pin_Addr4   = 34;
// static const int pin_Addr3   = 36;
// static const int pin_Addr2   = 38;
// static const int pin_Addr1   = 40;
// static const int pin_Addr0   = 42;
// static const int pin_Data0   = 44;
// static const int pin_Data1   = 46;
// static const int pin_Data2   = 48;
// static const int pin_nWE     = 27;
// static const int pin_Addr13  = 29;
// static const int pin_Addr8   = 31;
// static const int pin_Addr9   = 33;
// static const int pin_Addr11  = 35;
// static const int pin_nOE     = 37;
// static const int pin_Addr10  = 39;
// static const int pin_nCE     = 41;
// static const int pin_Data7   = 43;
// static const int pin_Data6   = 45;
// static const int pin_Data5   = 47;
// static const int pin_Data4   = 49;
// static const int pin_Data3   = 51;
// static const int pin_WaitingForInput  = 13;

static const int pin_WaitingForInput  = 13;

static const int pin_addr[] = {
  22, 23, 24, 25, 26, 27, 28, 29,
  30, 31, 32, 33, 34, 35, 36, 37
};

static const int pin_data[] = {
  38, 39, 40, 41, 42, 43, 44, 45
};

// rd,wr,rom_wr,iorq,mrq,rst,bsrq,bsack
static const int pin_rd = 50;
static const int pin_wr = 49;
static const int pin_rom_wr = 46;
static const int pin_iorq = A0;
static const int pin_mrq = 47;
static const int pin_rst = 48;
static const int pin_bsrq = 53;
static const int pin_bsak = A2;

static const int pin_nWE = pin_rom_wr;
static const int pin_nOE = pin_rd;
static const int pin_nCE = pin_mrq;
// static const int pin_nCE get rid of this

byte g_cmd[80]; // strings received from the controller will go in here
static const int kMaxBufferSize = 16;
byte buffer[kMaxBufferSize];

static const long int k_uTime_WritePulse_uS = 1;
static const long int k_uTime_ReadPulse_uS = 1;


// (to be honest, both of the above are about ten times too big - but the Arduino won't reliably
// delay down at the nanosecond level, so this is the best we can do.)

// the setup function runs once when you press reset or power the board
void setup()
{
  passiveMode();
  Serial.begin(57600);


  pinMode(pin_WaitingForInput, OUTPUT);
}

void loop()
{
  while (true)
  {
    Serial.println("asd");
    digitalWrite(pin_WaitingForInput, HIGH);
    Serial.println("1");
    ReadString();
    Serial.println("2");
    digitalWrite(pin_WaitingForInput, LOW);

    switch (g_cmd[0])
    {
      case 'V': Serial.println(version_string); break;
      case 'P': SetSDPState(true); break;
      case 'U': SetSDPState(false); break;
      case 'R': ReadEEPROM(); break;
      case 'W': WriteEEPROM(); break;
      case 0: break; // empty string. Don't mind ignoring this.
      default: Serial.println("ERR Unrecognised command"); break;
    }
  }
}


void passiveMode() {
  setDataInput();
  setAddrInput();
  pinMode(pin_rd, INPUT);
  pinMode(pin_wr, INPUT);
  pinMode(pin_iorq, INPUT);
  pinMode(pin_mrq, INPUT);
  pinMode(pin_rom_wr, INPUT);
  pinMode(pin_bsrq, INPUT);
  pinMode(pin_bsak, INPUT);
  pinMode(pin_rst, INPUT);
}

void setDataInput() {
  for(int i = 0; i < 8; i++) {
    pinMode(pin_data[i], INPUT);
  }
}

void setDataOutput() {
  for(int i = 0; i < 8; i++) {
    pinMode(pin_data[i], OUTPUT);
  }
}


void setAddrInput() {
  for(int i = 0; i < 16; i++) {
    pinMode(pin_addr[i], INPUT);
  }
}

void setAddrOutput() {
  for(int i = 0; i < 16; i++) {
    pinMode(pin_addr[i], OUTPUT);
  }
}

void takeBus() {
  pinMode(pin_rst, OUTPUT);
  pinMode(pin_bsrq, OUTPUT);
  digitalWrite(pin_rst, LOW);
  digitalWrite(pin_bsrq, LOW);
  delayMicroseconds(10);
//  digitalWrite(pin_rst, HIGH);
  delayMicroseconds(10);
}

void setControlOutput() {
  pinMode(pin_rd, OUTPUT);
  pinMode(pin_wr, OUTPUT);
  pinMode(pin_rom_wr, OUTPUT);
  pinMode(pin_iorq, OUTPUT);
  pinMode(pin_mrq, OUTPUT);
}

void setControlInput() {
  pinMode(pin_rd, INPUT);
  pinMode(pin_wr, INPUT);
  pinMode(pin_rom_wr, INPUT);
  pinMode(pin_iorq, INPUT);
  pinMode(pin_mrq, INPUT);
}

void selectRom() {
  digitalWrite(pin_rd, HIGH);
  digitalWrite(pin_wr, HIGH);
  digitalWrite(pin_iorq, HIGH);
  digitalWrite(pin_mrq, LOW);
}

void prepareToReadOrWrite() {
  takeBus();
  setControlOutput();
  selectRom();
  setAddrOutput();
}

void giveBus() {
  setDataInput();
  setAddrInput();
  setControlInput();
  digitalWrite(pin_rst, LOW);
  pinMode(pin_bsrq, INPUT);
  delayMicroseconds(10);
  pinMode(pin_rst, INPUT);
}

void ReadEEPROM() // R<address>  - read kMaxBufferSize bytes from EEPROM, beginning at <address> (in hex)
{
  if (g_cmd[1] == 0)
  {
    Serial.println("ERR");
    return;
  }
  prepareToReadOrWrite();
  // decode ASCII representation of address (in hex) into an actual value
  int addr = 0;
  int x = 1;
  while (x < 5 && g_cmd[x] != 0)
  {
    addr = addr << 4;
    addr |= HexToVal(g_cmd[x++]);
  }

  digitalWrite(pin_nWE, HIGH); // disables write
  setDataInput();
  digitalWrite(pin_nOE, LOW); // makes the EEPROM output the byte
  delayMicroseconds(5);

  ReadEEPROMIntoBuffer(addr, kMaxBufferSize);

  // now print the results, starting with the address as hex ...
  Serial.print(hex[ (addr & 0xF000) >> 12 ]);
  Serial.print(hex[ (addr & 0x0F00) >> 8  ]);
  Serial.print(hex[ (addr & 0x00F0) >> 4  ]);
  Serial.print(hex[ (addr & 0x000F)       ]);
  Serial.print(":");
  PrintBuffer(kMaxBufferSize);

  Serial.println("OK");

  digitalWrite(pin_nOE, HIGH); // stops the EEPROM outputting the byte
  delayMicroseconds(10);
  giveBus();
}

void WriteEEPROM() // W<four byte hex address>:<data in hex, two characters per byte, max of 16 bytes per line>
{
  if (g_cmd[1] == 0)
  {
    Serial.println("ERR");
    return;
  }

  prepareToReadOrWrite();
  delayMicroseconds(10);
  int addr = 0;
  int x = 1;
  while (g_cmd[x] != ':' && g_cmd[x] != 0)
  {
    addr = addr << 4;
    addr |= HexToVal(g_cmd[x]);
    ++x;
  }

  // g_cmd[x] should now be a :
  if (g_cmd[x] != ':')
  {
    Serial.println("ERR");
    return;
  }

  x++; // now points to beginning of data
  uint8_t iBufferUsed = 0;
  while (g_cmd[x] && g_cmd[x+1] && iBufferUsed < kMaxBufferSize && g_cmd[x] != ',')
  {
    uint8_t c = (HexToVal(g_cmd[x]) << 4) | HexToVal(g_cmd[x+1]);
    buffer[iBufferUsed++] = c;
    x += 2;
  }

  // if we're pointing to a comma, then the optional checksum has been provided!
  if (g_cmd[x] == ',' && g_cmd[x+1] && g_cmd[x+2])
  {
    byte checksum = (HexToVal(g_cmd[x+1]) << 4) | HexToVal(g_cmd[x+2]);

    byte our_checksum = CalcBufferChecksum(iBufferUsed);

    if (our_checksum != checksum)
    {
      // checksum fail!
      iBufferUsed = -1;
      Serial.print("ERR ");
      Serial.print(checksum, HEX);
      Serial.print(" ");
      Serial.print(our_checksum, HEX);
      Serial.println("");
      return;
    }
  }

  // buffer should now contains some data
  if (iBufferUsed > 0)
  {
    WriteBufferToEEPROM(addr, iBufferUsed);
  }

  if (iBufferUsed > -1)
  {
    Serial.println("OK");
  }
  delayMicroseconds(10);
  giveBus();
}

// Important note: the EEPROM needs to have data written to it immediately after sending the "unprotect" command, so that the buffer is flushed.
// So we read byte 0 from the EEPROM first, then use that as the dummy write afterwards.
// It wouldn't matter if this facility was used immediately before writing an EEPROM anyway ... but it DOES matter if you use this option
// in isolation (unprotecting the EEPROM but not changing it).

void SetSDPState(bool bWriteProtect)
{

  digitalWrite(pin_nWE, HIGH); // disables write
  digitalWrite(pin_nOE, LOW); // makes the EEPROM output the byte
  setDataInput();

  byte bytezero = ReadByteFrom(0);

  digitalWrite(pin_nOE, HIGH); // stop EEPROM from outputting byte
  digitalWrite(pin_nCE, HIGH);
  setDataOutput();

  // Different chips can have different byte sequences.
  // Check the data sheet if your chip doesn't match on of the below.

  // Use this is for the AT28C64B
  // if (bWriteProtect)
  // {
  //   WriteByteTo(0x1555, 0xAA);
  //   WriteByteTo(0x0AAA, 0x55);
  //   WriteByteTo(0x1555, 0xA0);
  // }
  // else
  // {
  //   WriteByteTo(0x1555, 0xAA);
  //   WriteByteTo(0x0AAA, 0x55);
  //   WriteByteTo(0x1555, 0x80);
  //   WriteByteTo(0x1555, 0xAA);
  //   WriteByteTo(0x0AAA, 0x55);
  //   WriteByteTo(0x1555, 0x20);
  // }

  // This is for the AT28C256
  if (bWriteProtect)
  {
    WriteByteTo(0x5555, 0xAA);
    WriteByteTo(0x2AAA, 0x55);
    WriteByteTo(0x5555, 0xA0);
  }
  else
  {
    WriteByteTo(0x5555, 0xAA);
    WriteByteTo(0x2AAA, 0x55);
    WriteByteTo(0x5555, 0x80);
    WriteByteTo(0x5555, 0xAA);
    WriteByteTo(0x2AAA, 0x55);
    WriteByteTo(0x5555, 0x20);
  }

  WriteByteTo(0x0000, bytezero); // this "dummy" write is required so that the EEPROM will flush its buffer of commands.

  digitalWrite(pin_nCE, LOW); // return to on by default for the rest of the code

  Serial.print("OK SDP ");
  if (bWriteProtect)
  {
    Serial.println("enabled");
  }
  else
  {
    Serial.println("disabled");
  }
}

// ----------------------------------------------------------------------------------------

void ReadEEPROMIntoBuffer(int addr, int size)
{
  digitalWrite(pin_nWE, HIGH);
  setDataInput();
  digitalWrite(pin_nOE, LOW);

  for (int x = 0; x < size; ++x)
  {
    buffer[x] = ReadByteFrom(addr + x);
  }

  digitalWrite(pin_nOE, HIGH);
}

void WriteBufferToEEPROM(int addr, int size)
{
  digitalWrite(pin_nOE, HIGH); // stop EEPROM from outputting byte
  digitalWrite(pin_nWE, HIGH); // disables write
  setDataOutput();

  for (uint8_t x = 0; x < size; ++x)
  {
    WriteByteTo(addr + x, buffer[x]);
  }

}

// ----------------------------------------------------------------------------------------

// this function assumes that data lines have already been set as INPUTS, and that
// nOE is set LOW.
byte ReadByteFrom(int addr)
{
  SetAddress(addr);
  digitalWrite(pin_nCE, LOW);
  delayMicroseconds(k_uTime_ReadPulse_uS);
  byte b = ReadData();
  digitalWrite(pin_nCE, HIGH);

  return b;
}

// this function assumes that data lines have already been set as OUTPUTS, and that
// nOE is set HIGH.
void WriteByteTo(int addr, byte b)
{
  SetAddress(addr);
  SetData(b);

  digitalWrite(pin_nCE, LOW);
  digitalWrite(pin_nWE, LOW); // enable write
  delayMicroseconds(k_uTime_WritePulse_uS);

  digitalWrite(pin_nWE, HIGH); // disable write
  digitalWrite(pin_nCE, HIGH);
}

// ----------------------------------------------------------------------------------------

void SetAddress(int a)
{
  digitalWrite(pin_addr[0],  (a&1)?HIGH:LOW    );
  digitalWrite(pin_addr[1],  (a&2)?HIGH:LOW    );
  digitalWrite(pin_addr[2],  (a&4)?HIGH:LOW    );
  digitalWrite(pin_addr[3],  (a&8)?HIGH:LOW    );
  digitalWrite(pin_addr[4],  (a&16)?HIGH:LOW   );
  digitalWrite(pin_addr[5],  (a&32)?HIGH:LOW   );
  digitalWrite(pin_addr[6],  (a&64)?HIGH:LOW   );
  digitalWrite(pin_addr[7],  (a&128)?HIGH:LOW  );
  digitalWrite(pin_addr[8],  (a&256)?HIGH:LOW  );
  digitalWrite(pin_addr[9],  (a&512)?HIGH:LOW  );
  digitalWrite(pin_addr[10], (a&1024)?HIGH:LOW );
  digitalWrite(pin_addr[11], (a&2048)?HIGH:LOW );
  digitalWrite(pin_addr[12], (a&4096)?HIGH:LOW );
  digitalWrite(pin_addr[13], (a&8192)?HIGH:LOW );
  digitalWrite(pin_addr[14], (a&16384)?HIGH:LOW);
}

// this function assumes that data lines have already been set as OUTPUTS.
void SetData(byte b)
{
  digitalWrite(pin_data[0], (b&1)?HIGH:LOW  );
  digitalWrite(pin_data[1], (b&2)?HIGH:LOW  );
  digitalWrite(pin_data[2], (b&4)?HIGH:LOW  );
  digitalWrite(pin_data[3], (b&8)?HIGH:LOW  );
  digitalWrite(pin_data[4], (b&16)?HIGH:LOW );
  digitalWrite(pin_data[5], (b&32)?HIGH:LOW );
  digitalWrite(pin_data[6], (b&64)?HIGH:LOW );
  digitalWrite(pin_data[7], (b&128)?HIGH:LOW);
}

// this function assumes that data lines have already been set as INPUTS.
byte ReadData()
{
  byte b = 0;

  if (digitalRead(pin_data[0]) == HIGH) b |= 1;
  if (digitalRead(pin_data[1]) == HIGH) b |= 2;
  if (digitalRead(pin_data[2]) == HIGH) b |= 4;
  if (digitalRead(pin_data[3]) == HIGH) b |= 8;
  if (digitalRead(pin_data[4]) == HIGH) b |= 16;
  if (digitalRead(pin_data[5]) == HIGH) b |= 32;
  if (digitalRead(pin_data[6]) == HIGH) b |= 64;
  if (digitalRead(pin_data[7]) == HIGH) b |= 128;

  return(b);
}

// ----------------------------------------------------------------------------------------

void PrintBuffer(int size)
{
  uint8_t chk = 0;

  for (uint8_t x = 0; x < size; ++x)
  {
    Serial.print(hex[ (buffer[x] & 0xF0) >> 4 ]);
    Serial.print(hex[ (buffer[x] & 0x0F)      ]);

    chk = chk ^ buffer[x];
  }

  Serial.print(",");
  Serial.print(hex[ (chk & 0xF0) >> 4 ]);
  Serial.print(hex[ (chk & 0x0F)      ]);
  Serial.println("");
}

void ReadString()
{
  int i = 0;
  byte c;

  g_cmd[0] = 0;
  do
  {
    if (Serial.available())
    {
      c = Serial.read();
      if (c > 31)
      {
        g_cmd[i++] = c;
        g_cmd[i] = 0;
      }
    }
  }
  while (c != 10);
}

uint8_t CalcBufferChecksum(uint8_t size)
{
  uint8_t chk = 0;

  for (uint8_t x = 0; x < size; ++x)
  {
    chk = chk ^  buffer[x];
  }

  return(chk);
}

// converts one character of a HEX value into its absolute value (nibble)
byte HexToVal(byte b)
{
  if (b >= '0' && b <= '9') return(b - '0');
  if (b >= 'A' && b <= 'F') return((b - 'A') + 10);
  if (b >= 'a' && b <= 'f') return((b - 'a') + 10);
  return(0);
}
