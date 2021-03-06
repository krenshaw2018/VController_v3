// Please read VController_v3.ino for information about the license and authors

// This page has the following parts:
// Section 1: MIDI Port Initialization
// Section 2: MIDI In Functions
// Section 3: MIDI Out Functions
// Section 4: MIDI Functions for Device Detection
// Section 5: MIDI PC and CC Ledger

// ********************************* Section 1: MIDI Port Initialization ********************************************

// Setup of usbMIDI and Serial midi in and out
// * midi ports are setup and the sysex buffer size is custom set to 256.
// * the connection/disconnection of devices is triggered from here.
// * the midi ports are read here and the devices are called after receiving PC, CC or sysex messages
// * the basic procedures for sending PC, CC and sysex messages to a specific midi port are specified here
// * the control of virtual ports via VCbridge on a raspberry pi is controlled from here. A virtual port is selected
//   with CC #119 on MIDI channel 16. Both the VController and VCbridge will send and receive this message to select the port.
// * A MIDI port consists of two parts. First hexadecimal digit is the real port number: 0x00 = USB_MIDI, 0x10 is serial port 1, 0x20 is serial port 2, etc.
//   The second nibble is the virtual port number from VCbridge: 0x10 is all virtual ports, 0x11 is virtual port 1, 0x12 is virtual port 2,etc

// Change buffersize of usbMIDI:
// Edit on Mac: /Applications/Arduino.app/Contents/Resources/Java/hardware/teensy/avr/cores/teensy3/usb_midi.h in Teksteditor and change USB_MIDI_SYSEX_MAX 256

#include <MIDI.h>

#define CHECK4DEVICES_TIMER_LENGTH 250 // Check every second which Roland devices are connected
#define MAX_TIMES_NO_RESPONSE 4 // The number of times the device may not respond before a disconnect
unsigned long Check4DevicesTimer = 0;
bool no_device_check = false; // This check should not occur during page sysex reading.

#define NUMBER_OF_MIDI_PORTS 4 // USB MIDI, serial 1 and 2, and 3

uint8_t Current_MIDI_port; // The MIDI port that is being read.
uint8_t VCbridge_in_port[NUMBER_OF_MIDI_PORTS] = { 0 };
uint8_t VCbridge_out_port[NUMBER_OF_MIDI_PORTS] = { 0 };
bool bank_selection_active = false;

// Setup MIDI ports. The USB port is set from the Arduino menu.
struct MySettings : public midi::DefaultSettings
{
  //static const bool UseRunningStatus = false; // Messes with my old equipment!
  //static const bool Use1ByteParsing = false; // More focus on reading messages - will this help the equipment from stopping with receiving data?
  static const unsigned SysExMaxSize = 256; // Change sysex buffersize - Zoom devices send packets up to 146 bytes
};

struct MySettings3 : public midi::DefaultSettings
{
  //static const bool UseRunningStatus = false; // Messes with my old equipment!
  static const bool Use1ByteParsing = false; // More focus on reading messages - will this help the equipment from stopping with receiving data?
  static const unsigned SysExMaxSize = 256; // Change sysex buffersize - Zoom G3 sends packets up to 120 bytes
  //static const long BaudRate = MIDI3_BAUD_RATE; // Set in hardware.h
};

MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial1, MIDI1, MySettings); // Enables serial1 port for MIDI communication with custom settings
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial2, MIDI2, MySettings); // Enables serial2 port for MIDI communication with custom settings
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial3, MIDI3, MySettings3); // Enables serial3 port for MIDI communication with custom settings

void setup_MIDI_common()
{
  usbMIDI.setHandleNoteOff(OnNoteOff);
  usbMIDI.setHandleNoteOn(OnNoteOn) ;
  usbMIDI.setHandleProgramChange(OnProgramChange);
  usbMIDI.setHandleControlChange(OnControlChange);
  usbMIDI.setHandleSysEx(OnSysEx);

  //pinMode(0, INPUT_PULLUP); //Add the internal pullup resistor to pin 0 (Rx)
  delay(100);
  MIDI1.begin(MIDI_CHANNEL_OMNI);
  MIDI1.turnThruOff();
  MIDI1.setHandleNoteOff(OnNoteOff);
  MIDI1.setHandleNoteOn(OnNoteOn) ;
  MIDI1.setHandleProgramChange(OnProgramChange);
  MIDI1.setHandleControlChange(OnControlChange);
  MIDI1.setHandleSystemExclusive(OnSerialSysEx);
  //MIDI1.setHandleActiveSensing(OnActiveSenseMIDI1);

  delay(100);
  MIDI2.begin(MIDI_CHANNEL_OMNI);
  MIDI2.turnThruOff();
  MIDI2.setHandleNoteOff(OnNoteOff);
  MIDI2.setHandleNoteOn(OnNoteOn) ;
  MIDI2.setHandleProgramChange(OnProgramChange);
  MIDI2.setHandleControlChange(OnControlChange);
  MIDI2.setHandleSystemExclusive(OnSerialSysEx);
  //MIDI2.setHandleActiveSensing(OnActiveSenseMIDI2);

  delay(100);
  MIDI3.begin(MIDI_CHANNEL_OMNI);
  MIDI3.turnThruOff();
  MIDI3.setHandleNoteOff(OnNoteOff);
  MIDI3.setHandleNoteOn(OnNoteOn) ;
  MIDI3.setHandleProgramChange(OnProgramChange);
  MIDI3.setHandleControlChange(OnControlChange);
  MIDI3.setHandleSystemExclusive(OnSerialSysEx);
  //MIDI3.setHandleActiveSensing(OnActiveSenseMIDI3);

}

void main_MIDI_common()
{
  Current_MIDI_port = USBMIDI_PORT;
  usbMIDI.read();
  Current_MIDI_port = MIDI1_PORT;
  MIDI1.read();
  Current_MIDI_port = MIDI2_PORT;
  MIDI2.read();
  Current_MIDI_port = MIDI3_PORT;
  MIDI3.read();

  MIDI_check_for_devices();  // Check actively if any devices are out there
  PAGE_check_sysex_watchdog(); // check if the watchdog has not expired
}

void MIDI_check_MIDI3() // Because serial3 is not FIFO, we do an extra check in the loop, so it can keep up.
{
  Current_MIDI_port = MIDI3_PORT;
  MIDI3.read(); 
}

// ********************************* Section 2: MIDI In Functions ********************************************

// Sysex for detecting MIDI devices
#define Anybody_out_there {0xF0, 0x7E, 0x10, 0x06, 0x01, 0xF7}  // Ask all MIDI devices to respond with their Manufacturing ID, Device ID and version number

void OnNoteOn(byte channel, byte note, byte velocity)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  DEBUGMIDI("NoteOn #" + String(note) + " with velocity " + String(velocity) + " received on channel " + String(channel) + " and port " + String(Current_MIDI_port >> 4) + ":" + String(VCbridge_in_port[VCbridge_index])); // Show on serial debug screen
  SCO_bass_mode_note_on(note, velocity, channel, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
}

void OnNoteOff(byte channel, byte note, byte velocity)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  DEBUGMIDI("NoteOff #" + String(note) + " with velocity " + String(velocity) + " received on channel " + String(channel) + " and port " + String(Current_MIDI_port >> 4) + ":" + String(VCbridge_in_port[VCbridge_index])); // Show on serial debug screen
  SCO_bass_mode_note_off(note, velocity, channel, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
}

void OnProgramChange(byte channel, byte program)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  DEBUGMIDI("PC #" + String(program) + " received on channel " + String(channel) + " and port " + String(Current_MIDI_port >> 4) + ":" + String(VCbridge_in_port[VCbridge_index])); // Show on serial debug screen
  for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
    Device[d]->check_PC_in(program, channel, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
  }
}

void OnControlChange(byte channel, byte control, byte value)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  DEBUGMIDI("CC #" + String(control) + " Value:" + String(value) + " received on channel " + String(channel) + " and port " + String(Current_MIDI_port >> 4) + ":" + String(VCbridge_in_port[VCbridge_index])); // Show on serial debug screen
  if ((channel == VCONTROLLER_MIDI_CHANNEL) && (control == LINE_SELECT_CC_NUMBER)) {
    VCbridge_in_port[VCbridge_index] = value;
    DEBUGMIDI("Changed virtual input channel for port " + String(VCbridge_index) +" to: " + String(VCbridge_in_port[VCbridge_index]));
  }
  else {
    for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
      Device[d]->check_CC_in(control, value, channel, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
    }
  }
}

void OnSysEx(const unsigned char* sxdata, short unsigned int sxlength, bool sx_comp)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  //MIDI1.sendSysEx(sxlength, sxdata); // MIDI through usb to serial
  //MIDI2.sendSysEx(sxlength, sxdata); // MIDI through usb to serial
  MIDI_debug_sysex(sxdata, sxlength, USBMIDI_PORT, false);

  if (sxdata[1] == 0x7E) { //Check if it is a Universal Non-Real Time message
    MIDI_check_SYSEX_in_universal(sxdata, sxlength, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
  }
  else {
    for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
      Device[d]->check_SYSEX_in(sxdata, sxlength, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
    }
  }
}

void OnSerialSysEx(byte *sxdata, unsigned sxlength)
{
  uint8_t VCbridge_index = Current_MIDI_port >> 4;
  //usbMIDI.sendSysEx(sxlength, sxdata); // MIDI through serial to usb

  if (sxdata[1] == 0x7E) { //Check if it is a Universal Non-Real Time message
    MIDI_check_SYSEX_in_universal(sxdata, sxlength, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
  }
  else {
    for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
      Device[d]->check_SYSEX_in(sxdata, sxlength, Current_MIDI_port | VCbridge_in_port[VCbridge_index]);
    }
  }
  
  MIDI_debug_sysex(sxdata, sxlength, Current_MIDI_port | VCbridge_in_port[VCbridge_index], false);  
}


// ********************************* Section 3: MIDI Out Functions ********************************************

// Send Program Change message
void MIDI_send_PC(uint8_t Program, uint8_t Channel, uint8_t Port) {
  DEBUGMIDI("PC #" + String(Program) + " sent on channel " + String(Channel) + " and port " + String(Port >> 4) + ":" + String(Port & 0x0F)); // Show on serial debug screen
  MIDI_check_port_message(Port);
  if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendProgramChange(Program, Channel);
  if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendProgramChange(Program, Channel);
  if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendProgramChange(Program, Channel);
  if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendProgramChange(Program, Channel);
  if ((Port & 0xF0) == ALL_PORTS) {
    usbMIDI.sendProgramChange(Program, Channel);
    MIDI1.sendProgramChange(Program, Channel);
    MIDI2.sendProgramChange(Program, Channel);
    MIDI3.sendProgramChange(Program, Channel);
  }
}

void MIDI_send_CC(uint8_t Controller, uint8_t Value, uint8_t Channel, uint8_t Port) {
  DEBUGMIDI("CC #" + String(Controller) + " with value " + String(Value) + " sent on channel " + String(Channel) + " and port " + String(Port >> 4) + ":" + String(Port & 0x0F)); // Show on serial debug screen
  MIDI_check_port_message(Port);
  if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendControlChange(Controller, Value, Channel);
  if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendControlChange(Controller, Value, Channel);
  if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendControlChange(Controller, Value, Channel);
  if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendControlChange(Controller, Value, Channel);
  if ((Port & 0xF0) == ALL_PORTS) {
    usbMIDI.sendControlChange(Controller, Value, Channel);
    MIDI1.sendControlChange(Controller, Value, Channel);
    MIDI2.sendControlChange(Controller, Value, Channel);
    MIDI3.sendControlChange(Controller, Value, Channel);
  }
}

void MIDI_send_note_on(uint8_t Note, uint8_t Velocity, uint8_t Channel, uint8_t Port) {
  DEBUGMIDI("NoteOn #" + String(Note) + " with velocity " + String(Velocity) + " sent on channel " + String(Channel) + " and port " + String(Port >> 4) + ":" + String(Port & 0x0F)); // Show on serial debug screen
  MIDI_check_port_message(Port);
  if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendNoteOn(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendNoteOn(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendNoteOn(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendNoteOn(Note, Velocity, Channel);
  if ((Port & 0xF0) == ALL_PORTS) {
    usbMIDI.sendNoteOn(Note, Velocity, Channel);
    MIDI1.sendNoteOn(Note, Velocity, Channel);
    MIDI2.sendNoteOn(Note, Velocity, Channel);
    MIDI3.sendNoteOn(Note, Velocity, Channel);
  }
}

void  MIDI_send_note_off(uint8_t Note, uint8_t Velocity, uint8_t Channel, uint8_t Port) {
  DEBUGMIDI("NoteOff #" + String(Note) + " with velocity " + String(Velocity) + " sent on channel " + String(Channel) + " and port " + String(Port >> 4) + ":" + String(Port & 0x0F)); // Show on serial debug screen
  MIDI_check_port_message(Port);
  if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendNoteOff(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendNoteOff(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendNoteOff(Note, Velocity, Channel);
  if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendNoteOff(Note, Velocity, Channel);
  if ((Port & 0xF0) == ALL_PORTS) {
    usbMIDI.sendNoteOff(Note, Velocity, Channel);
    MIDI1.sendNoteOff(Note, Velocity, Channel);
    MIDI2.sendNoteOff(Note, Velocity, Channel);
    MIDI3.sendNoteOff(Note, Velocity, Channel);
  }
}

void MIDI_send_sysex(const unsigned char* sxdata, short unsigned int sxlength, uint8_t Port) {
  MIDI_check_port_message(Port);
  if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendSysEx(sxlength, sxdata); // Send full message including 0xF0 and 0xF7 boundaries
  if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendSysEx(sxlength - 2, &sxdata[1]); // Serial Midi library adds oxF0 and oxF7 sysex header itself, so we leave them out
  if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendSysEx(sxlength - 2, &sxdata[1]); // Otherwise they will be send twice.
  if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendSysEx(sxlength - 2, &sxdata[1]);
  if ((Port & 0xF0) == ALL_PORTS) {
    usbMIDI.sendSysEx(sxlength, sxdata);
    MIDI1.sendSysEx(sxlength - 2, &sxdata[1]);
    MIDI2.sendSysEx(sxlength - 2, &sxdata[1]);
    MIDI3.sendSysEx(sxlength - 2, &sxdata[1]);
  }
  MIDI_debug_sysex(sxdata, sxlength, Port, true);
}

void MIDI_check_port_message(uint8_t Port) { // Check if we need to tell the VCbridge on the RPi what port to use

  uint8_t VCbridge_index = Port >> 4; // First nibble is MIDI port number
  uint8_t new_port_number = Port & 0x0F;
  
  if (VCbridge_index > NUMBER_OF_MIDI_PORTS) return;

  if (new_port_number != VCbridge_out_port[VCbridge_index]) {
    VCbridge_out_port[VCbridge_index] = new_port_number;
    //Send the new port number to the VCbridge software
    if ((Port & 0xF0) == USBMIDI_PORT) usbMIDI.sendControlChange(LINE_SELECT_CC_NUMBER, new_port_number, VCONTROLLER_MIDI_CHANNEL); 
    if ((Port & 0xF0) == MIDI1_PORT) MIDI1.sendControlChange(LINE_SELECT_CC_NUMBER, new_port_number, VCONTROLLER_MIDI_CHANNEL); 
    if ((Port & 0xF0) == MIDI2_PORT) MIDI2.sendControlChange(LINE_SELECT_CC_NUMBER, new_port_number, VCONTROLLER_MIDI_CHANNEL); 
    if ((Port & 0xF0) == MIDI3_PORT) MIDI3.sendControlChange(LINE_SELECT_CC_NUMBER, new_port_number, VCONTROLLER_MIDI_CHANNEL);

    DEBUGMSG("!!!Changed virtual output channel for port " + String(VCbridge_index) +" to: " + String(new_port_number));
  }
  
  if ((Port & 0xF0) == ALL_PORTS) { // Call it recursively for all ports
     MIDI_check_port_message(USBMIDI_PORT | (Port & 0x0F));
     MIDI_check_port_message(MIDI1_PORT | (Port & 0x0F));
     MIDI_check_port_message(MIDI2_PORT | (Port & 0x0F));
     MIDI_check_port_message(MIDI3_PORT | (Port & 0x0F));
  }
}

// ********************************* Section 4: MIDI Functions for Device Detection ********************************************

void MIDI_check_SYSEX_in_universal(const unsigned char* sxdata, short unsigned int sxlength, uint8_t port) // Check for universal SYSEX message - identity reply
{
  // Check if it is an identity reply from a device
  // There is no check on the second byte (device ID), in case a device has a different device ID
  //if (no_device_check == false) {
  if ((sxdata[3] == 0x06) && (sxdata[4] == 0x02)) {
    for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
      Device[d]->identity_check(sxdata, sxlength, port);
    }
  }
  //}
}

// check for Roland devices on each MIDI port sequentially
uint8_t check_device_no = 0;
void MIDI_check_for_devices()
{
  // Check if timer needs to be set
  if (Check4DevicesTimer == 0) {
    Check4DevicesTimer = millis();
  }

  // Check if timer runs out
  if (millis() - Check4DevicesTimer > CHECK4DEVICES_TIMER_LENGTH) {
    Check4DevicesTimer = millis(); // Reset the timer

    // Send the message to all MIDI ports if no_device_check is not true!!
    if (no_device_check == false) {
      uint8_t sysexbuffer[6] = Anybody_out_there;
      if (check_device_no == 0 ) {
        MIDI_send_sysex(sysexbuffer, 6, USBMIDI_PORT);
        MIDI_check_all_devices_still_connected();
      }
      if (check_device_no == 1 ) MIDI_send_sysex(sysexbuffer, 6, MIDI1_PORT);
      if (check_device_no == 2 ) MIDI_send_sysex(sysexbuffer, 6, MIDI2_PORT);
      if (check_device_no == 3 ) MIDI_send_sysex(sysexbuffer, 6, MIDI3_PORT);

      //MIDI_debug_sysex(sysexbuffer, 6, "CKout");
      check_device_no++;
      if (check_device_no >= NUMBER_OF_MIDI_PORTS) check_device_no = 0;
    }
  }
}

void MIDI_check_all_devices_still_connected() {
  //DEBUGMSG("Check devices");
  for (uint8_t d = 0; d < NUMBER_OF_DEVICES; d++) {
    Device[d]->check_still_connected();
  }
}

// ********************************* Section 5: MIDI PC and CC Ledger ********************************************

// PC ledger - we will remember a number of PC values in memory, so the VController will show the correct state when returning to the PC values
#define PC_LEDGER_SIZE 25
#define NOT_FOUND 255

struct PC_ledger_struct {
  uint8_t program;
  uint8_t channel;
  uint8_t port;
};

PC_ledger_struct PC_ledger[PC_LEDGER_SIZE] = { 0 };
uint8_t PC_ledger_size = 0;
uint8_t PC_ledger_position = 0;

void MIDI_remember_PC(uint8_t program, uint8_t channel, uint8_t port) {
  // For speed reasons, we first check if the current position contains our data
  if ((PC_ledger[PC_ledger_position].channel == channel) && (PC_ledger[PC_ledger_position].port == port)) {
    PC_ledger[PC_ledger_position].program = program;
    DEBUGMSG("PC" + String(program) + " stored in position " + String(PC_ledger_position));
    return;
  }

  // Then we check the entire ledger
  for (uint8_t i = 0; i < PC_ledger_size; i++) { // First check if we already have this PC stored
    if ((PC_ledger[i].channel == channel) && (PC_ledger[i].port == port)) {
      PC_ledger[i].program = program;
      DEBUGMSG("PC" + String(program) + " stored in position " + String(i));
    return;
    }
  }

  // Add a new record.
  if (PC_ledger_size < PC_LEDGER_SIZE) PC_ledger_size++; // Once ledger is full, it cannot grow
  PC_ledger_position++;
  if (PC_ledger_position >= PC_LEDGER_SIZE) PC_ledger_position = 0; // The ledger position will go round
  PC_ledger[PC_ledger_position].program = program;
  PC_ledger[PC_ledger_position].channel = channel;
  PC_ledger[PC_ledger_position].port = port;
  DEBUGMSG("PC" + String(program) + " stored in position " + String(PC_ledger_position));
}

uint8_t MIDI_recall_PC(uint8_t channel, uint8_t port) {
  // For speed reasons, we first check if the current position contains our data
  if ((PC_ledger[PC_ledger_position].channel == channel) && (PC_ledger[PC_ledger_position].port == port)) {
    //DEBUGMSG("PC" + String(PC_ledger[PC_ledger_position].program) + " read from position " + String(PC_ledger_position));
    return PC_ledger[PC_ledger_position].program;
  }

  // Then we check the entire ledger
  for (uint8_t i = 0; i < PC_ledger_size; i++) {
    if ((PC_ledger[i].channel == channel) && (PC_ledger[i].port == port)) {
      //DEBUGMSG("PC" + String(PC_ledger[i].program) + " read from position " + String(i));
      return PC_ledger[i].program;
    }
  }
  return NOT_FOUND; // Not found
}


// CC ledger - we will remember a number of cc values in memory, so the VController will show the correct state when returning to the CC values
#define CC_LEDGER_SIZE 25
#define NOT_FOUND 255

struct CC_ledger_struct {
  uint8_t controller;
  uint8_t value;
  uint8_t channel;
  uint8_t port;
};

CC_ledger_struct CC_ledger[CC_LEDGER_SIZE] = { 0 };
uint8_t CC_ledger_size = 0;
uint8_t CC_ledger_position = 0;

void MIDI_remember_CC(uint8_t controller, uint8_t value, uint8_t channel, uint8_t port) {
  // For speed reasons, we first check if the current position contains our data
  if ((CC_ledger[CC_ledger_position].controller == controller) && (CC_ledger[CC_ledger_position].channel == channel) && (CC_ledger[CC_ledger_position].port == port)) {
    CC_ledger[CC_ledger_position].value = value;
    DEBUGMSG("CC" + String(controller) + "value " + String(value) + " stored in position " + String(CC_ledger_position));
    return;
  }

  // Then we check the entire ledger
  for (uint8_t i = 0; i < CC_ledger_size; i++) { // First check if we already have this CC stored
    if ((CC_ledger[i].controller == controller) && (CC_ledger[i].channel == channel) && (CC_ledger[i].port == port)) {
      CC_ledger[i].value = value;
      DEBUGMSG("CC" + String(controller) + "value " + String(value) + " stored in position " + String(i));
    return;
    }
  }

  // Add a new record.
  if (CC_ledger_size < CC_LEDGER_SIZE) CC_ledger_size++; // Once ledger is full, it cannot grow
  CC_ledger_position++;
  if (CC_ledger_position >= CC_LEDGER_SIZE) CC_ledger_position = 0; // The ledger position will go round
  CC_ledger[CC_ledger_position].controller = controller;
  CC_ledger[CC_ledger_position].value = value;
  CC_ledger[CC_ledger_position].channel = channel;
  CC_ledger[CC_ledger_position].port = port;
  DEBUGMSG("CC" + String(controller) + "value " + String(value) + " stored in position " + String(CC_ledger_position));
}

uint8_t MIDI_recall_CC(uint8_t controller, uint8_t channel, uint8_t port) {
  // For speed reasons, we first check if the current position contains our data
  if ((CC_ledger[CC_ledger_position].controller == controller) && (CC_ledger[CC_ledger_position].channel == channel) && (CC_ledger[CC_ledger_position].port == port)) {
    DEBUGMSG("CC" + String(controller) + "value " + String(CC_ledger[CC_ledger_position].value) + " read from position " + String(CC_ledger_position));
    return CC_ledger[CC_ledger_position].value;
  }

  // Then we check the entire ledger
  for (uint8_t i = 0; i < CC_ledger_size; i++) {
    if ((CC_ledger[i].controller == controller) && (CC_ledger[i].channel == channel) && (CC_ledger[i].port == port)) {
      DEBUGMSG("CC" + String(controller) + "value " + String(CC_ledger[i].value) + " read from position " + String(i));
      return CC_ledger[i].value;
    }
  }
  return NOT_FOUND; // Not found
}

