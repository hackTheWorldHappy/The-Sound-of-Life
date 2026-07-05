#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define PITCH_XSHUT 2
#define VOLUME_XSHUT 3
#define PITCH_ADDR 0x30
#define VOLUME_ADDR 0x31

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

uint32_t lastUpdateMs = 0;
bool noteHeld = false;
int heldNote = 0;

bool initSensor(uint8_t xshutPin, uint8_t address, Adafruit_VL53L0X &sensor) {
	pinMode(xshutPin, OUTPUT);
	digitalWrite(xshutPin, LOW);
	delay(10);
	digitalWrite(xshutPin, HIGH);
	delay(10);
	return sensor.begin(address);
}

uint16_t readMm(Adafruit_VL53L0X &sensor) {
	VL53L0X_RangingMeasurementData_t measurement;
	sensor.rangingTest(&measurement, false);
	if (measurement.RangeStatus == 4) {
		return 0;
	}
	return measurement.RangeMilliMeter;
}

int mapPitchToNote(uint16_t mm) {
	if (mm == 0) {
		return 60;
	}
	return (int)map(constrain((long)mm, 50L, 450L), 50L, 450L, 84, 48);
}

int mapPitchToBend(uint16_t mm) {
	if (mm == 0) {
		return 8192;
	}
	return (int)map(constrain((long)mm, 50L, 450L), 50L, 450L, 0, 16383);
}

byte mapVolumeToExpression(uint16_t mm) {
	if (mm == 0) {
		return 0;
	}
	return (byte)map(constrain((long)mm, 30L, 400L), 30L, 400L, 127, 0);
}

void printMm(uint16_t mm) {
	if (mm == 0) {
		display.print(F("--"));
		return;
	}
	display.print(mm);
	display.print(F(" mm"));
}

void render(uint16_t pitchMm, uint16_t volumeMm, int note, bool sounding) {
	display.clearDisplay();
	display.setCursor(0, 0);
	display.print(F("Pitch: "));
	printMm(pitchMm);
	display.setCursor(0, 12);
	display.print(F("Vol:   "));
	printMm(volumeMm);
	display.setCursor(0, 24);
	display.print(F("Note:  "));
	display.print(note);
	display.setCursor(0, 36);
	display.print(F("State: "));
	display.print(sounding ? F("ON") : F("OFF"));
	display.display();
}

void setup() {
	Serial.begin(115200);

	Wire.begin();

	if (!TinyUSBDevice.isInitialized()) {
		TinyUSBDevice.begin(0);
	}

	usb_midi.setStringDescriptor("Theremin MIDI");
	MIDI.begin(MIDI_CHANNEL_OMNI);

	if (TinyUSBDevice.mounted()) {
		TinyUSBDevice.detach();
		delay(10);
		TinyUSBDevice.attach();
	}

	if (!initSensor(PITCH_XSHUT, PITCH_ADDR, pitchSensor)) {
		while (1) {
			delay(1);
		}
	}

	if (!initSensor(VOLUME_XSHUT, VOLUME_ADDR, volumeSensor)) {
		while (1) {
			delay(1);
		}
	}

	if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
		while (1) {
			delay(1);
		}
	}

	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.display();
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
	TinyUSBDevice.task();
#endif

	if (millis() - lastUpdateMs < 25) {
		return;
	}
	lastUpdateMs = millis();

	const bool midiReady = TinyUSBDevice.mounted();
	const uint16_t pitchMm = readMm(pitchSensor);
	const uint16_t volumeMm = readMm(volumeSensor);
	const bool sounding = volumeMm >= 120;
	const int note = mapPitchToNote(pitchMm);
	const int bend = mapPitchToBend(pitchMm);
	const byte expression = mapVolumeToExpression(volumeMm);

	if (midiReady) {
		if (sounding) {
			if (!noteHeld) {
				MIDI.sendNoteOn(note, 100, 1);
				noteHeld = true;
				heldNote = note;
			} else if (heldNote != note) {
				MIDI.sendNoteOff(heldNote, 0, 1);
				MIDI.sendNoteOn(note, 100, 1);
				heldNote = note;
			}

			MIDI.sendPitchBend(bend, 1);
			MIDI.sendControlChange(11, expression, 1);
		} else if (noteHeld) {
			MIDI.sendNoteOff(heldNote, 0, 1);
			noteHeld = false;
			heldNote = 0;
		}
	} else {
		noteHeld = false;
		heldNote = 0;
	}

	MIDI.read();
	render(pitchMm, volumeMm, note, sounding);
}
