    #include <Adafruit_VL53L0X.h>
    #include <Adafruit_TinyUSB.h>

    Adafruit_USBD_MIDI usb_midi;
    Adafruit_VL53L0X pitchSensor;
    Adafruit_VL53L0X volumeSensor;


    void setup() {
        Serial.begin(9600); //may increase when need faster comms
        Wire.begin();
        pitchSensor.begin();
        volumeSensor.begin();
    }

    void loop() {
        VL53L0X_RangingMeasurementData_t pitchMeasure;
        pitchSensor.rangingTest(&pitchMeasure, false);

        Serial.println(pitchMeasure.RangeMilliMeter);
    }



    void sendNote(uint8_t note)
    {
        uint8_t msg[3];

        msg[0] = 0x90;
        msg[1] = 61;
        msg[2] = 127;

        usb_midi.send(msg, 3);
    }




