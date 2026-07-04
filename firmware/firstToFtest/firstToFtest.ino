#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// XSHUT pins
#define PITCH_XSHUT 2
#define VOLUME_XSHUT 3

// Unique I2C addresses
#define PITCH_ADDR 0x30
#define VOLUME_ADDR 0x31

Adafruit_USBD_MIDI usb_midi;
Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);


void setup() {
  Serial.begin(115200);  //may increase when need faster comms
  while (!Serial) { delay(1); }

  Wire.begin();

  pinMode(PITCH_XSHUT, OUTPUT);
  pinMode(VOLUME_XSHUT, OUTPUT);

  digitalWrite(PITCH_XSHUT, LOW);
  digitalWrite(VOLUME_XSHUT, LOW);
  delay(10);

  digitalWrite(PITCH_XSHUT, HIGH);
  delay(10);
  if (!pitchSensor.begin(PITCH_ADDR)) {
    Serial.println("Failed to boot pitch VL53L0X");
    while (1) { delay(1); }
  }

  digitalWrite(VOLUME_XSHUT, HIGH);
  delay(10);
  if (!volumeSensor.begin(VOLUME_ADDR)) {
    Serial.println("Failed to boot volume VL53L0X");
    while (1) { delay(1); }
  }

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

  Serial.print("pitch mm: ");
  if (pitchMeasure.RangeStatus != 4) {
    Serial.print(pitchMeasure.RangeMilliMeter);
  } else {
    Serial.print("out of range");
  }

  Serial.print("  volume mm: ");
  if (volumeMeasure.RangeStatus != 4) {
    Serial.println(volumeMeasure.RangeMilliMeter);
  } else {
    Serial.println("out of range");
  }

  delay(100);
  /*
  delay(1000);
  Serial.println("MIDI sending");

*/


  //Testing purpose
  // Send a single note, then continuously adjust pitch bend
  MIDI.sendNoteOn(60, 100, 1);  // Start at middle C

/*
  for (int bend = 0; bend < 16384; bend += 10) {
    MIDI.sendPitchBend(bend, 1);  // Smooth pitch change
    delay(50);
  }
*/
  MIDI.sendNoteOff(60, 0, 1);




  MIDI.read();  // Process incoming messages
}

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  // Log when a note is pressed.

  /*
  Serial.print("Note on: channel = ");
  Serial.print(channel);

  Serial.print(" pitch = ");
  Serial.print(pitch);

  Serial.print(" velocity = ");
  Serial.println(velocity);
  */
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  // Log when a note is released.

  /*
  Serial.print("Note off: channel = ");
  Serial.print(channel);

  Serial.print(" pitch = ");
  Serial.print(pitch);

  Serial.print(" velocity = ");
  Serial.println(velocity);
  */
}