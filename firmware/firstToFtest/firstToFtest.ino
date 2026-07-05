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

#define TFT_CS 6
#define TFT_DC 7
#define TFT_RST 15
#define TFT_SCK 16
#define TFT_MOSI 17

#define PITCH_MIN_MM 50
#define PITCH_MAX_MM 450
#define NOTE_MIN 48
#define NOTE_MAX 84
#define VOLUME_MIN_MM 30
#define VOLUME_MAX_MM 400

// Hysteresis band around the "sounding" threshold to avoid MIDI note chatter
// when the hand sits right at the boundary.
#define VOLUME_ON_MM 130
#define VOLUME_OFF_MM 110

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

// TFT
SPIClass tftSPI(FSPI);
Adafruit_ST7735 tft(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

const int16_t SCREEN_W = 128;
const int16_t SCREEN_H = 160;
const int16_t CURSOR_ZONE_W = 14; // left strip: "now" + hand arrow
const int16_t BARCODE_X = CURSOR_ZONE_W;
const int16_t BARCODE_W = SCREEN_W - CURSOR_ZONE_W;
GFXcanvas16 canvas(SCREEN_W, SCREEN_H);
const float PIXELS_PER_MS = 0.06f;

// Song Data, currently twinkle twinkle little star
struct SongNote
{
	uint8_t note;
	uint16_t durationMs;
};

const SongNote song[] = {
	{60, 400},
	{60, 400},
	{67, 400},
	{67, 400},
	{69, 400},
	{69, 400},
	{67, 800},
	{65, 400},
	{65, 400},
	{64, 400},
	{64, 400},
	{62, 400},
	{62, 400},
	{60, 800},
};
const uint8_t songLength = sizeof(song) / sizeof(song[0]);

uint32_t songCumMs[sizeof(song) / sizeof(song[0]) + 1];
uint32_t totalSongMs = 0;
uint32_t songStartMs = 0;

uint32_t lastUpdateMs = 0;
uint32_t lastDisplayMs = 0;
bool noteHeld = false;
int heldNote = 0;
bool sounding = false; // persists across loop iterations for hysteresis

// Last known good pitch reading, used when the sensor momentarily loses
// its target (readMm returns 0) so the note/bend don't snap to a default.
uint16_t lastValidPitchMm = (PITCH_MIN_MM + PITCH_MAX_MM) / 2;

// --- sensor init ---

// Pulls this sensor's XSHUT high (assumes it was already held low) and boots
// it at the given I2C address.
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
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MAX, NOTE_MIN);
}

int mapPitchToBend(uint16_t mm)
{
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM),
					 PITCH_MIN_MM, PITCH_MAX_MM, -8192, 8191);
}
byte mapVolumeToExpression(uint16_t mm)
{
	if (mm == 0)
	{
		return 0;
	}
	return (byte)map(constrain((long)mm, (long)VOLUME_MIN_MM, (long)VOLUME_MAX_MM), VOLUME_MIN_MM, VOLUME_MAX_MM, 127, 0);
}

// given a target note, what mm should the hand be at
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

// Song helpers
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
		if (t < songCumMs[i + 1])
		{
			return song[i].note;
		}
	}
	return song[songLength - 1].note;
}

// Display
void drawFrame(uint16_t handMm)
{
	canvas.fillScreen(ST77XX_BLACK);

	uint32_t nowMs = (millis() - songStartMs) % totalSongMs;

	// Scrolling barcode: column 0 = "now"
	for (int16_t x = 0; x < BARCODE_W; x++)
	{
		uint32_t t = (nowMs + (uint32_t)(x / PIXELS_PER_MS)) % totalSongMs;
		uint8_t note = noteAtTime(t);
		uint16_t targetMm = noteToMm(note);
		int16_t y = mmToY(targetMm);
		uint16_t color = ST77XX_CYAN;
		canvas.fillRect(BARCODE_X + x, y - 1, 1, 3, color);
	}

	// "Now" reference line at the boundary between the cursor zone and barcode
	canvas.drawFastVLine(BARCODE_X, 0, SCREEN_H, ST77XX_YELLOW);

	// Current hand height, drawn as a right-pointing arrow in the left zone
	if (handMm > 0)
	{
		int16_t y = mmToY(handMm);
		canvas.fillTriangle(0, y - 4, 0, y + 4, CURSOR_ZONE_W - 2, y, ST77XX_RED);
	}

	tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_W, SCREEN_H);
}

void setup()
{
	Serial.begin(115200);

	Wire.begin(I2C_SDA, I2C_SCL);

	// Hold BOTH sensors in reset before bringing them up one at a time.
	// This guarantees they don't both boot at the shared default address
	// (0x29) simultaneously, which would cause an I2C address conflict.
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
		while (1)
		{
			delay(1);
		}
	}

	if (!bringUpSensor(VOLUME_XSHUT, VOLUME_ADDR, volumeSensor))
	{
		Serial.println("Failed to detect Volume sensor");
		while (1)
		{
			delay(1);
		}
	}

	Serial.println("Two VL53L0X ready!");

	tftSPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
	tft.initR(INITR_BLACKTAB);
	tft.setRotation(0);
	tft.fillScreen(ST77XX_BLACK);
	buildSongTiming();
	songStartMs = millis();
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

	// Hold the last valid pitch reading if the sensor momentarily loses
	// its target, instead of snapping to a hardcoded default note.
	if (pitchReading > 0)
	{
		lastValidPitchMm = pitchReading;
	}
	const uint16_t pitchMm = lastValidPitchMm;

	// debug print
	Serial.print("Pitch: ");
	Serial.print(pitchReading);
	Serial.print(" mm\tVolume: ");
	Serial.print(volumeMm);
	Serial.println(" mm");

	// Hysteresis: turn on above VOLUME_ON_MM, turn off below VOLUME_OFF_MM,
	// hold state in between. Prevents rapid note on/off chatter when the
	// hand hovers right at a single threshold.
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

	if (nowMillis - lastDisplayMs >= 50)
	{
		lastDisplayMs = nowMillis;
		drawFrame(pitchMm);
	}
}