#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>




// XSHUT pins 
#define PITCH_XSHUT  2
#define VOLUME_XSHUT 3

Adafruit_USBD_MIDI usb_midi;
Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

const int MIN_DIST = 30;
const int MAX_DIST = 400;


void setup() {
  Serial.begin(115200);  //may increase when need faster comms
  Wire.begin();
  pitchSensor.begin();
  volumeSensor.begin();

  //Set Address
  pitchSensor.setAddress(0x29);  //Default
  volumeSensor.setAddress(0x30);


  //midi init
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
}

void loop() {
  VL53L0X_RangingMeasurementData_t pitchMeasure;
  pitchSensor.rangingTest(&pitchMeasure, true);
  VL53L0X_RangingMeasurementData_t volumeMeasure;
  volumeSensor.rangingTest(&volumeMeasure, true);


  Serial.println(pitchMeasure.RangeMilliMeter);
  Serial.println(volumeMeasure.RangeMilliMeter);
  MIDI.sendNoteOn(60, 127, 1);  // Send middle C
  MIDI.read();                  // Process incoming messages
}

/*
void sendNote(uint8_t note) {
  uint8_t msg[3];

  msg[0] = 0x90;
  msg[1] = 61;
  msg[2] = 127;

  usb_midi.send(msg, 3);
}
*/


