#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#define PITCH_MIN_MM 30
#define PITCH_MAX_MM 480
#define VOLUME_ADDR 0x31

#define VOLUME_MIN_MM 30
#define VOLUME_MAX_MM 480

#define PITCH_XSHUT 4
#define VOLUME_XSHUT 5
bool prevMidiMounted = false;

#define PITCH_MIN_MM 50
	if (mm == 0)
	{
		return NOTE_MIN;
	}
	// lower distance -> lower pitch
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MIN, NOTE_MAX);

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);
	if (mm == 0)
	{
		return 8192;
	}
	// keep bend mapping consistent (lower distance -> lower bend value)
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, 0, 16383);
bool noteHeld = false;
int heldNote = 0;

// --- sensor init ---

bool initSensor(uint8_t xshutPin, uint8_t address, Adafruit_VL53L0X &sensor)
	// within user requested range: 3cm..48cm (30..480mm)
	const bool sounding = (volumeMm >= VOLUME_MIN_MM && volumeMm <= VOLUME_MAX_MM);
	pinMode(xshutPin, OUTPUT);
	digitalWrite(xshutPin, LOW);
	delay(10);
	digitalWrite(xshutPin, HIGH);
	// handle USB mount changes: when reattached, send All Notes Off to clear stuck notes on host
	if (midiReady && !prevMidiMounted)
	{
		MIDI.sendControlChange(123, 0, 1); // All Notes Off
		noteHeld = false;
		heldNote = 0;
	}

	if (midiReady)
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
	{
		return 60;
	}
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MAX, NOTE_MIN);
}

int mapPitchToBend(uint16_t mm)
{
	if (mm == 0)
	{
		return 8192;
		// USB not mounted. Clear local state. Cannot send NoteOff because host disconnected.
		noteHeld = false;
		heldNote = 0;

byte mapVolumeToExpression(uint16_t mm)
{
	if (mm == 0)
	{
		return 0;
	}
	return (byte)map(constrain((long)mm, (long)VOLUME_MIN_MM, (long)VOLUME_MAX_MM), VOLUME_MIN_MM, VOLUME_MAX_MM, 127, 0);
}

void setup()
{
	Serial.begin(115200);

	Wire.begin(I2C_SDA, I2C_SCL);

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

	if (!initSensor(PITCH_XSHUT, PITCH_ADDR, pitchSensor))
	{
		Serial.println("Failed to detect Pitch sensor");
		while (1)
		{
			delay(1);
		}
	}

	if (!initSensor(VOLUME_XSHUT, VOLUME_ADDR, volumeSensor))
	{
		Serial.println("Failed to detect Volume sensor");
		while (1)
		{
			delay(1);
		}
	}

	Serial.println("Two VL53L0X ready!");
}

void loop()
{
#ifdef TINYUSB_NEED_POLLING_TASK
	TinyUSBDevice.task();
#endif

	Serial.println("test");

	uint32_t nowMillis = millis();
	if (nowMillis - lastUpdateMs < 25)
	{
		return;
	}
	lastUpdateMs = nowMillis;

	const bool midiReady = TinyUSBDevice.mounted();
	const uint16_t pitchMm = readMm(pitchSensor);
	const uint16_t volumeMm = readMm(volumeSensor);

	// debug print
	Serial.print("Pitch: ");
	Serial.print(pitchMm);
	Serial.print(" mm\tVolume: ");
	Serial.print(volumeMm);
	Serial.println(" mm");

	const bool sounding = volumeMm >= 120;
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