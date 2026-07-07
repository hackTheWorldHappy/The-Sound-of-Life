his is a DIY project to create a MIDI Theremin . So lets go....
Supplies
Supplies
Supplies
Supplies

Arduino Leonardo

TOF sensor VL53LOX(2)

Berch pins(8)

Jumper wires

Micro USB cable
Step 1: WHY LEONARDO AND VL530LOX SENSOR
WHY LEONARDO AND VL530LOX SENSOR
WHY LEONARDO AND VL530LOX SENSOR


MIDI (Musical Instrument Digital Interface) devices are electronic instruments, controllers, and hardware that communicate musical performance data using the MIDI protocol (introduced in 1983). Instead of sending audio, MIDI sends digital instructions that describe music (e.g., notes, velocity, pitch bends, control changes).

The Arduino Leonardo is a microcontroller board based on the ATmega32u4. Unlike many other Arduino boards, it has built-in USB communication, allowing it to appear to a computer as a keyboard, mouse, or MIDI device without extra hardware. It features 20 digital I/O pins, 12 analog inputs, PWM outputs, a 16 MHz clock, and can be powered via USB or an external supply.

The reason for choosing VL530LOX Sensor are:


The VL53L0X time-of-flight distance sensor is far more suitable for building a theremin than traditional ultrasonic sensors because of its precision, speed, and stability. It measures distance using a tiny laser pulse, giving it millimeter-level accuracy and smooth, consistent readings. This is critical in a theremin, where even small fluctuations in distance translate into noticeable changes in pitch.

In contrast, ultrasonic sensors often produce jittery or unstable outputs, with limited resolution (around 1 cm) and slower response times, which can result in choppy or unpleasant sound transitions. The VL53L0X also features a very narrow sensing cone, allowing it to detect just the player’s hand without being confused by nearby objects, whereas ultrasonic sensors have a wide cone that easily picks up unintended reflections. Additionally, the VL53L0X is less affected by environmental factors like temperature, humidity, or surface texture, making its performance far more reliable. n.



Step 2: Test Run
Test Run

Here we tested the working of the concept using one sensor .


The code :


#include <Wire.h>

#include <Adafruit_VL53L0X.h>

// Create sensor object

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

void setup() {

Serial.begin(115200);

while (!Serial); // Wait for Serial Monitor to open

Serial.println("VL53L0X Single Sensor Test");

// Initialize I²C

Wire.begin();

// Try to start sensor

if (!lox.begin()) {

Serial.println("Failed to find VL53L0X sensor!");

while (1); // Stop here if not found

}

Serial.println("VL53L0X sensor ready!");

}

void loop() {

VL53L0X_RangingMeasurementData_t measure;

// Perform a measurement

lox.rangingTest(&measure, false);

if (measure.RangeStatus != 4) { // Phase failures have error code 4

Serial.print("Distance: ");

Serial.print(measure.RangeMilliMeter);

Serial.println(" mm");

} else {

Serial.println("Out of range");

}

delay(200);

}
Step 3: CIRCUIT MAKING
CIRCUIT MAKING

LEONARDO HAS ONLY I^2C CHANNEL SO WHEN CONNECTING TWO VL530LOX WOULD BE RECOGNIZED AS ONE SENSOR RATHER THAN TWO SEPARATE ONES.


SO TO SOLVE THIS ISSUE WE HAVE INITIALIZE BOTH THE SENSORS IN SEPARETELY SO THAT THEY COULD HAVE THEIR UNIQUE I^2C ID.THEREFORE, THEIR XSHUT PINS ARE CONNECTED TO PIN 7,8 RESPECTIVELY SO ONE COULD BE MOMENTARILY SHUT TO INITIALIZE THE OTHER ONE .
Step 4: Coding

CODE TO INITIALIZE TWO VL530LOX SENSOR :

pinMode(XSHUT1, OUTPUT);

pinMode(XSHUT2, OUTPUT);

// Reset both

digitalWrite(XSHUT1, LOW);

digitalWrite(XSHUT2, LOW);

delay(10);

// Init Pitch sensor

digitalWrite(XSHUT1, HIGH);

delay(10);

if (!sensorPitch.begin(0x30)) {

Serial.println("Failed to detect Pitch sensor");

while (1);

}

// Init CC sensor

digitalWrite(XSHUT2, HIGH);

delay(10);

if (!sensorCC.begin(0x31)) {

Serial.println("Failed to detect CC sensor");

while (1);

}

Serial.println("Two VL53L0X ready!");

}



CODE OF PITCH BEND FUNCTION :

// Pitch bend calculation

float semitoneOffset = ((float)(pitchDist - pitchCenterDist) / (maxDistPitch - minDistPitch)) * pitchBendRangeSemitones * 2;

semitoneOffset = constrain(semitoneOffset, -pitchBendRangeSemitones, pitchBendRangeSemitones);

int bendValue = (int)(semitoneOffset * (8192.0 / pitchBendRangeSemitones));

bendValue = constrain(bendValue, -8192, 8191);

sendPitchBend(bendValue, midiChannel);

Serial.print("Distance: "); Serial.print(pitchDist);

Serial.print(" mm | Bend: "); Serial.println(bendValue); 

Code:
leothermin.ino