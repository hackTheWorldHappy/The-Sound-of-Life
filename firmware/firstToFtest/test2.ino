#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Wire.h>

// XSHUT pins for each sensor (pick free GPIOs, avoid D4/D5 which are I2C)
#define PITCH_XSHUT  2   // D2
#define VOLUME_XSHUT 3   // D3

Adafruit_USBD_MIDI usb_midi;
Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// Distance range to map (mm) — tune to your setup after testing
const int MIN_DIST = 30;
const int MAX_DIST = 400;

// MIDI state tracking
uint8_t currentNote = 0;
bool noteIsOn = false;

const uint8_t MIDI_CHANNEL = 1;

void handleNoteOn(byte channel, byte note, byte velocity) {
  // Handle incoming notes if needed
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  // Handle incoming note-offs if needed
}

void setup() {
  Serial.begin(115200);

  pinMode(PITCH_XSHUT, OUTPUT);
  pinMode(VOLUME_XSHUT, OUTPUT);

  // Hold both sensors in reset
  digitalWrite(PITCH_XSHUT, LOW);
  digitalWrite(VOLUME_XSHUT, LOW);
  delay(10);

  Wire.begin(); // Xiao RP2040 default: SDA = D4, SCL = D5

  // Bring up pitch sensor first, at default address
  digitalWrite(PITCH_XSHUT, HIGH);
