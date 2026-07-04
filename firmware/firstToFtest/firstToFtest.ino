  #include <Adafruit_VL53L0X.h>
  #include <Adafruit_TinyUSB.h>
  #include <MIDI.h>

  Adafruit_USBD_MIDI usb_midi;
  Adafruit_VL53L0X pitchSensor;
  Adafruit_VL53L0X volumeSensor;

  MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);


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
    delay(1000);
    Serial.println("MIDI sending");
    MIDI.sendNoteOn(60, 127, 1);  // Send middle C
    MIDI.sendNoteOff(60, 0, 1);
    MIDI.read();                  // Process incoming messages
  }


  void handleNoteOn(byte channel, byte pitch, byte velocity) {
    // Log when a note is pressed.
    Serial.print("Note on: channel = ");
    Serial.print(channel);

    Serial.print(" pitch = ");
    Serial.print(pitch);

    Serial.print(" velocity = ");
    Serial.println(velocity);
  }

  void handleNoteOff(byte channel, byte pitch, byte velocity) {
    // Log when a note is released.
    Serial.print("Note off: channel = ");
    Serial.print(channel);

    Serial.print(" pitch = ");
    Serial.print(pitch);

    Serial.print(" velocity = ");
    Serial.println(velocity);
  }