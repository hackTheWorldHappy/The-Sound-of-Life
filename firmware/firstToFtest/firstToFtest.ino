#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>   // change to Adafruit_ST7789.h if that's your actual panel

#define PITCH_ADDR 0x30
#define VOLUME_ADDR 0x31

#define I2C_SDA 11
#define I2C_SCL 12

#define PITCH_XSHUT 4
#define VOLUME_XSHUT 5

#define PITCH_MIN_MM 30
#define PITCH_MAX_MM 480
#define NOTE_MIN 48
#define NOTE_MAX 84
#define VOLUME_MIN_MM 30
#define VOLUME_MAX_MM 480

#define VOLUME_ON_MM 130
#define VOLUME_OFF_MM 110

#define TFT_CS 10
#define TFT_RST 9
#define TFT_DC 14
// SPI pins used with SPI.begin(sck, miso, mosi, cs) below

#define SCREEN_W 128
#define SCREEN_H 160   // most ST7735 1.8" panels are 128x160 -- change if yours differs

float p = 3.1415926;

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

// THIS WAS MISSING -- tft was used everywhere but never declared
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

struct SongNote
{
	uint8_t note;
	uint16_t durationMs;
};

const SongNote song[] = {
	{60, 400}, {60, 400}, {67, 400}, {67, 400}, {69, 400}, {69, 400}, {67, 800},
	{65, 400}, {65, 400}, {64, 400}, {64, 400}, {62, 400}, {62, 400}, {60, 800},
};
const uint8_t songLength = sizeof(song) / sizeof(song[0]);

uint32_t songCumMs[sizeof(song) / sizeof(song[0]) + 1];
uint32_t totalSongMs = 0;
uint32_t songStartMs = 0;

uint32_t lastUpdateMs = 0;
bool noteHeld = false;
int heldNote = 0;
bool sounding = false;

bool prevMidiMounted = false;
int lastSentNoteOn = 0;

uint16_t lastValidPitchMm = (PITCH_MIN_MM + PITCH_MAX_MM) / 2;

bool bringUpSensor(uint8_t xshutPin, uint8_t address, Adafruit_VL53L0X &sensor)
{
	digitalWrite(xshutPin, HIGH);
	delay(10);
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
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MIN, NOTE_MAX);
}

int mapPitchToBend(uint16_t mm)
{
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM),
					PITCH_MIN_MM, PITCH_MAX_MM, -8192, 8191);
}

byte mapVolumeToExpression(uint16_t mm)
{
	if (mm == 0) return 0;
	if (mm > VOLUME_MAX_MM) return 0;
	return (byte)map(constrain((long)mm, (long)VOLUME_MIN_MM, (long)VOLUME_MAX_MM), VOLUME_MIN_MM, VOLUME_MAX_MM, 127, 0);
}

uint16_t noteToMm(uint8_t note)
{
	int n = constrain((int)note, NOTE_MIN, NOTE_MAX);
	return (uint16_t)map(n, NOTE_MAX, NOTE_MIN, PITCH_MIN_MM, PITCH_MAX_MM);
}

int16_t mmToY(uint16_t mm)
{
	return (int16_t)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM),
						PITCH_MIN_MM, PITCH_MAX_MM, 0, SCREEN_H - 1);
}

void buildSongTiming()
{
	songCumMs[0] = 0;
	for (uint8_t i = 0; i < songLength; i++)
	{
		songCumMs[i + 1] = songCumMs[i] + song[i].durationMs;
	}
	totalSongMs = songCumMs[songLength];
}

uint8_t noteAtTime(uint32_t t)
{
	t %= totalSongMs;
	for (uint8_t i = 0; i < songLength; i++)
	{
		if (t < songCumMs[i + 1]) return song[i].note;
	}
	return song[songLength - 1].note;
}

void setup()
{
	Serial.begin(115200);

	// --- SPI / TFT bring-up (moved here from illegal file-scope call) ---
	pinMode(TFT_RST, OUTPUT);
	digitalWrite(TFT_RST, HIGH);
	delay(10);
	digitalWrite(TFT_RST, LOW);   // explicit hardware reset pulse
	delay(20);
	digitalWrite(TFT_RST, HIGH);
	delay(150);

	SPI.begin();

	Serial.println(F("Initializing TFT..."));
	tft.initR(INITR_BLACKTAB);   // <-- if screen stays white, try INITR_GREENTAB,
	                              //     INITR_144GREENTAB, or INITR_REDTAB here
	tft.setRotation(0);
	tft.fillScreen(ST77XX_BLACK);
	Serial.println(F("TFT initialized"));

	// --- Sensors ---
	Wire.begin(I2C_SDA, I2C_SCL);

	pinMode(PITCH_XSHUT, OUTPUT);
	pinMode(VOLUME_XSHUT, OUTPUT);
	digitalWrite(PITCH_XSHUT, LOW);
	digitalWrite(VOLUME_XSHUT, LOW);
	delay(10);

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

	if (!bringUpSensor(PITCH_XSHUT, PITCH_ADDR, pitchSensor))
	{
		Serial.println("Failed to detect Pitch sensor");
		while (1) delay(1);
	}

	if (!bringUpSensor(VOLUME_XSHUT, VOLUME_ADDR, volumeSensor))
	{
		Serial.println("Failed to detect Volume sensor");
		while (1) delay(1);
	}

	Serial.println("Two VL53L0X ready!");

	buildSongTiming();
	songStartMs = millis();

	// quick one-shot visual confirmation the panel is alive
	tft.fillScreen(ST77XX_BLACK);
	tft.setCursor(0, 0);
	tft.setTextColor(ST77XX_GREEN);
	tft.setTextSize(1);
	tft.println("Theremin ready");
	delay(1000);
	tft.fillScreen(ST77XX_BLACK);
}

void loop()
{
#ifdef TINYUSB_NEED_POLLING_TASK
	TinyUSBDevice.task();
#endif

	uint32_t nowMillis = millis();
	if (nowMillis - lastUpdateMs < 25)
	{
		return;
	}
	lastUpdateMs = nowMillis;

	const bool midiReady = TinyUSBDevice.mounted();
	const uint16_t pitchReading = readMm(pitchSensor);
	const uint16_t volumeMm = readMm(volumeSensor);

	if (midiReady && !prevMidiMounted)
	{
		MIDI.sendControlChange(123, 0, 1);
		if (lastSentNoteOn != 0)
		{
			MIDI.sendNoteOff(lastSentNoteOn, 0, 1);
			lastSentNoteOn = 0;
		}
		noteHeld = false;
		heldNote = 0;
	}
	else if (!midiReady && prevMidiMounted)
	{
		if (noteHeld)
		{
			lastSentNoteOn = heldNote;
		}
		noteHeld = false;
		heldNote = 0;
	}
	prevMidiMounted = midiReady;

	if (pitchReading > 0)
	{
		lastValidPitchMm = pitchReading;
	}
	const uint16_t pitchMm = lastValidPitchMm;

	Serial.print("Pitch: ");
	Serial.print(pitchReading);
	Serial.print(" mm\tVolume: ");
	Serial.print(volumeMm);
	Serial.println(" mm");

	if (!sounding && volumeMm >= VOLUME_ON_MM)
	{
		sounding = true;
	}
	else if (sounding && volumeMm <= VOLUME_OFF_MM)
	{
		sounding = false;
	}

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
				lastSentNoteOn = note;
			}
			else if (heldNote != note)
			{
				MIDI.sendNoteOff(heldNote, 0, 1);
				MIDI.sendNoteOn(note, 100, 1);
				heldNote = note;
				lastSentNoteOn = note;
			}
			MIDI.sendPitchBend(bend, 1);
		}
		else if (noteHeld)
		{
			MIDI.sendNoteOff(heldNote, 0, 1);
			noteHeld = false;
			heldNote = 0;
			lastSentNoteOn = 0;
		}
		MIDI.sendControlChange(11, expression, 1);
	}
	else
	{
		noteHeld = false;
		heldNote = 0;
	}

	// live pitch indicator on screen instead of blocking invert-blink demo
	static int16_t lastY = -1;
	int16_t y = mmToY(pitchMm);
	if (y != lastY)
	{
		tft.fillRect(0, 0, SCREEN_W, SCREEN_H, ST77XX_BLACK);
		tft.fillCircle(SCREEN_W / 2, y, 4, sounding ? ST77XX_GREEN : ST77XX_RED);
		lastY = y;
	}

  tft.fillScreen(ST77XX_BLACK);
  testfillcircles(10, ST77XX_BLUE);
  testdrawcircles(10, ST77XX_WHITE);
  Serial.println("NOW");
  delay(5000);
	MIDI.read();
}



void testfillrects(uint16_t color1, uint16_t color2) {
  tft.fillScreen(ST77XX_BLACK);
  for (int16_t x = tft.width() - 1; x > 6; x -= 6) {
    tft.fillRect(tft.width() / 2 - x / 2, tft.height() / 2 - x / 2, x, x, color1);
    tft.drawRect(tft.width() / 2 - x / 2, tft.height() / 2 - x / 2, x, x, color2);
  }
}

void testfillcircles(uint8_t radius, uint16_t color) {
  for (int16_t x = radius; x < tft.width(); x += radius * 2) {
    for (int16_t y = radius; y < tft.height(); y += radius * 2) {
      tft.fillCircle(x, y, radius, color);
    }
  }
}