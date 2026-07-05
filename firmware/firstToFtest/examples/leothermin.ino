
 #include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <MIDIUSB.h>

Adafruit_VL53L0X sensorPitch = Adafruit_VL53L0X();
Adafruit_VL53L0X sensorCC = Adafruit_VL53L0X();

const int XSHUT1 = 7;
const int XSHUT2 = 8;

// Pitch sensor range
const int minDistPitch = 50;
const int maxDistPitch = 400;

// CC sensor range
const int minDistCC = 50;
const int maxDistCC = 400;

// MIDI settings
const byte midiChannel = 1;
const byte fxCC = 74; // Default CC for filter cutoff

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(XSHUT1, OUTPUT);
  pinMode(XSHUT2, OUTPUT);

  // Turn both off
  digitalWrite(XSHUT1, LOW);
  digitalWrite(XSHUT2, LOW);
  delay(10);

  // Init sensor 1
  digitalWrite(XSHUT1, HIGH);
  delay(10);
  if (!sensorPitch.begin(0x30)) {
    Serial.println("Failed to detect Sensor 1");
    while (1);
  }

  // Init sensor 2
  digitalWrite(XSHUT2, HIGH);
  delay(10);
  if (!sensorCC.begin(0x31)) {
    Serial.println("Failed to detect Sensor 2");
    while (1);
  }

  Serial.println("Two VL53L0X ready!");
}

void loop() {
  VL53L0X_RangingMeasurementData_t measurePitch;
  VL53L0X_RangingMeasurementData_t measureCC;

  // Read Sensor 1 (Pitch)
  sensorPitch.rangingTest(&measurePitch, false);
  int pitchDist = (measurePitch.RangeStatus != 4) ? measurePitch.RangeMilliMeter : -1;

  // Read Sensor 2 (Generic CC)
  sensorCC.rangingTest(&measureCC, false);
  int ccDist = (measureCC.RangeStatus != 4) ? measureCC.RangeMilliMeter : -1;

  // Print both distances to Serial
  Serial.print("Pitch Sensor: ");
  if (pitchDist >= 0) Serial.print(pitchDist); else Serial.print("Out of range");
  Serial.print(" mm\t");

  Serial.print("CC Sensor: ");
  if (ccDist >= 0) Serial.print(ccDist); else Serial.print("Out of range");
  Serial.println(" mm");

  // --- Send MIDI if in range ---
  if (pitchDist >= 0) {
    int note = map(pitchDist, minDistPitch, maxDistPitch, 72, 48); // C5 to C3
    note = constrain(note, 48, 72);
    sendNoteOn(note, 100, midiChannel);
  }

  if (ccDist >= 0) {
    int ccValue = map(ccDist, minDistCC, maxDistCC, 127, 0);
    ccValue = constrain(ccValue, 0, 127);
    sendControlChange(fxCC, ccValue, midiChannel);
  }

  delay(50);
}

// ----------- MIDIUSB Functions -----------
void sendNoteOn(byte note, byte velocity, byte channel) {
  midiEventPacket_t noteOn = {0x09, static_cast<byte>(0x90 | ((channel - 1) & 0x0F)), note, velocity};
  MidiUSB.sendMIDI(noteOn);
  MidiUSB.flush();
}

void sendControlChange(byte control, byte value, byte channel) {
  midiEventPacket_t event = {0x0B, static_cast<byte>(0xB0 | ((channel - 1) & 0x0F)), control, value};
  MidiUSB.sendMIDI(event);
  MidiUSB.flush();
}
