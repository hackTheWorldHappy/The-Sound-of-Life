#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>


#define PITCH_ADDR 0x30
#define VOLUME_ADDR 0x31

#define I2C_SDA 8
#define I2C_SCL 9

#define PITCH_XSHUT 4
#define VOLUME_XSHUT 5

#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  15
#define TFT_SCK  16
#define TFT_MOSI 17

#define PITCH_MIN_MM 50
#define PITCH_MAX_MM 450
#define NOTE_MIN 48
#define NOTE_MAX 84
#define VOLUME_MIN_MM 30
#define VOLUME_MAX_MM 400


Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

uint32_t lastUpdateMs = 0;
bool noteHeld = false;
int heldNote = 0;

const int XSHUT1 = 7;
const int XSHUT2 = 8;

void setup()
{
	Serial.begin(115200);
	Wire.begin();

	initSensor();

	if (!TinyUSBDevice.isInitialized())
	{
		TinyUSBDevice.begin(0);
	}

	usb_midi.setStringDescriptor("Theremin MIDI");
	MIDI.begin(MIDI_CHANNEL_OMNI);

	if (TinyUSBDevice.mounted())
	{
		TinyUSBDevice.detach();
		delay(10);
		TinyUSBDevice.attach();
	}
}

void loop()
{
#ifdef TINYUSB_NEED_POLLING_TASK
	TinyUSBDevice.task();
#endif

	if (millis() - lastUpdateMs < 25)
	{
		return;
	}
	lastUpdateMs = millis();

	const bool midiReady = TinyUSBDevice.mounted();

	const uint16_t pitchMm = readMm(pitchSensor);
	const uint16_t volumeMm = readMm(volumeSensor);

	// Print both distances to Serial
	Serial.print("Pitch Sensor: ");
	if (pitchMm >= 0)
		Serial.print(pitchMm);
	else
		Serial.print("Out of range");
	Serial.print(" mm\t");

	Serial.print("Volume Sensor: ");
	if (volumeMm >= 0)
		Serial.print(volumeMm);
	else
		Serial.print("Out of range");
	Serial.println(" mm");

	const int note = mapPitchToNote(pitchMm);
	const int bend = mapPitchToBend(pitchMm);
	const byte expression = mapVolumeToExpression(volumeMm);

	if (midiReady)
	{
		if (sounding)
		{
			if (!noteHeld)
			{
				MIDI.sendNoteOn(note, 100, 1);
				noteHeld = true;
				heldNote = note;
			}
			else if (heldNote != note)
			{
				MIDI.sendNoteOff(heldNote, 0, 1);
				MIDI.sendNoteOn(note, 100, 1);
				heldNote = note;
			}

			MIDI.sendPitchBend(bend, 1);
			MIDI.sendControlChange(11, expression, 1);
		}
		else if (noteHeld)
		{
			MIDI.sendNoteOff(heldNote, 0, 1);
			noteHeld = false;
			heldNote = 0;
		}
	}
	else
	{
		noteHeld = false;
		heldNote = 0;
	}

	MIDI.read();
}

void initSensor(uint8_t xshutPin, uint8_t address, Adafruit_VL53L0X &sensor)
{

	pinMode(XSHUT1, OUTPUT);
	pinMode(XSHUT2, OUTPUT);
	inMode(XSHUT2, OUTPUT);

	// Turn both off
	digitalWrite(XSHUT1, LOW);
	digitalWrite(XSHUT2, LOW);
	delay(10);

	// Init sensor 1
	digitalWrite(XSHUT1, HIGH);
	delay(10);
	if (!sensorPitch.begin(0x30))
	{
		Serial.println("Failed to detect Sensor 1");
		while (1)
			;
	}

	// Init sensor 2
	digitalWrite(XSHUT2, HIGH);
	delay(10);
	if (!sensorCC.begin(0x31))
	{
		Serial.println("Failed to detect Sensor 2");
		while (1)
			;
	}

	Serial.println("Two VL53L0X ready!");
	return sensor.begin(address);
}

uint16_t readMm(Adafruit_VL53L0X &sensor)
{
	VL53L0X_RangingMeasurementData_t measurement;
	sensor.rangingTest(&measurement, false);
	if (measurement.RangeStatus == 4)
	{
		return 0;
	}
	return measurement.RangeMilliMeter;
}

int mapPitchToNote(uint16_t mm)
{
	if (mm == 0)
		return 60;
	return (int)map(constrain((long)mm, 50L, 450L), 50L, 450L, 84, 48);
}

int mapPitchToBend(uint16_t mm)
{
	if (mm == 0)
		return 8192;
	return (int)map(constrain((long)mm, 50L, 450L), 50L, 450L, 0, 16383);
}

byte mapVolumeToExpression(uint16_t mm)
{
	if (mm == 0)
		return 0;
	return (byte)map(constrain((long)mm, 30L, 400L), 30L, 400L, 127, 0);
}
