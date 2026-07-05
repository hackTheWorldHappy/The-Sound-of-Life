#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <SPI.h>
#include <Adafruit_GFX.h>	 // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

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

#define TFT_CS 14
#define TFT_DC 15
#define TFT_RST 16

#define SCREEN_W 128
#define SCREEN_H 64

float p = 3.1415926;

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;

SPI.begin(18, -1, 23, TFT_CS); 

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
bool noteHeld = false;
int heldNote = 0;
bool sounding = false; // persists across loop iterations for hysteresis

// USB/MIDI state tracking to avoid stuck notes when host disconnects
bool prevMidiMounted = false;
int lastSentNoteOn = 0; // remember last note on to send noteoff on reconnect

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
	// lower distance -> lower pitch. Map PITCH_MIN->NOTE_MIN, PITCH_MAX->NOTE_MAX
	return (int)map(constrain((long)mm, (long)PITCH_MIN_MM, (long)PITCH_MAX_MM), PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MIN, NOTE_MAX);
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
	// if hand beyond VOLUME_MAX_MM, force volume down
	if (mm > VOLUME_MAX_MM)
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

void setup()
{

	// Setup of firstToFTest

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

	buildSongTiming();
	songStartMs = millis();

	// Setup of Display

	Serial.print(F("Hello! ST77xx TFT Test"));

	// Use this initializer if using a 1.8" TFT screen:
	tft.initR(INITR_BLACKTAB); // Init ST7735S chip, black tab

	Serial.println(F("ST77xx TFT Initialized"));

	uint16_t time = millis();
	tft.fillScreen(ST77XX_BLACK);
	time = millis() - time;

	Serial.println(time, DEC);
	delay(500);

	// large block of text
	tft.fillScreen(ST77XX_BLACK);
	testdrawtext("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur adipiscing ante sed nibh tincidunt feugiat. Maecenas enim massa, fringilla sed malesuada et, malesuada sit amet turpis. Sed porttitor neque ut ante pretium vitae malesuada nunc bibendum. Nullam aliquet ultrices massa eu hendrerit. Ut sed nisi lorem. In vestibulum purus a tortor imperdiet posuere. ", ST77XX_WHITE);
	delay(1000);

	// tft print function!
	tftPrintTest();
	delay(4000);

	// a single pixel
	tft.drawPixel(tft.width() / 2, tft.height() / 2, ST77XX_GREEN);
	delay(500);

	// line draw test
	testlines(ST77XX_YELLOW);
	delay(500);

	// optimized lines
	testfastlines(ST77XX_RED, ST77XX_BLUE);
	delay(500);

	testdrawrects(ST77XX_GREEN);
	delay(500);

	testfillrects(ST77XX_YELLOW, ST77XX_MAGENTA);
	delay(500);

	tft.fillScreen(ST77XX_BLACK);
	testfillcircles(10, ST77XX_BLUE);
	testdrawcircles(10, ST77XX_WHITE);
	delay(500);

	testroundrects();
	delay(500);

	testtriangles();
	delay(500);

	mediabuttons();
	delay(500);

	Serial.println("done");
	delay(1000);
}

void loop()
{

	// Display
	tft.invertDisplay(true);
	delay(500);
	tft.invertDisplay(false);
	delay(500);

	// Other
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

	// USB mount transition handling to avoid ghost notes on host
	if (midiReady && !prevMidiMounted)
	{
		// just reconnected. clear any stuck notes on host
		MIDI.sendControlChange(123, 0, 1); // All Notes Off
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
		// just disconnected. remember last held note to clear on reconnect
		if (noteHeld)
		{
			lastSentNoteOn = heldNote;
		}
		noteHeld = false;
		heldNote = 0;
	}
	prevMidiMounted = midiReady;

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
	// debug expression value
	Serial.print(" Expr: ");
	Serial.print(expression);

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

		// Always update expression controller so host sees volume changes
		MIDI.sendControlChange(11, expression, 1);
	}
	else
	{
		noteHeld = false;
		heldNote = 0;
	}

	MIDI.read();
}

void testlines(uint16_t color)
{
	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = 0; x < tft.width(); x += 6)
	{
		tft.drawLine(0, 0, x, tft.height() - 1, color);
		delay(0);
	}
	for (int16_t y = 0; y < tft.height(); y += 6)
	{
		tft.drawLine(0, 0, tft.width() - 1, y, color);
		delay(0);
	}

	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = 0; x < tft.width(); x += 6)
	{
		tft.drawLine(tft.width() - 1, 0, x, tft.height() - 1, color);
		delay(0);
	}
	for (int16_t y = 0; y < tft.height(); y += 6)
	{
		tft.drawLine(tft.width() - 1, 0, 0, y, color);
		delay(0);
	}

	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = 0; x < tft.width(); x += 6)
	{
		tft.drawLine(0, tft.height() - 1, x, 0, color);
		delay(0);
	}
	for (int16_t y = 0; y < tft.height(); y += 6)
	{
		tft.drawLine(0, tft.height() - 1, tft.width() - 1, y, color);
		delay(0);
	}

	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = 0; x < tft.width(); x += 6)
	{
		tft.drawLine(tft.width() - 1, tft.height() - 1, x, 0, color);
		delay(0);
	}
	for (int16_t y = 0; y < tft.height(); y += 6)
	{
		tft.drawLine(tft.width() - 1, tft.height() - 1, 0, y, color);
		delay(0);
	}
}

void testdrawtext(char *text, uint16_t color)
{
	tft.setCursor(0, 0);
	tft.setTextColor(color);
	tft.setTextWrap(true);
	tft.print(text);
}

void testfastlines(uint16_t color1, uint16_t color2)
{
	tft.fillScreen(ST77XX_BLACK);
	for (int16_t y = 0; y < tft.height(); y += 5)
	{
		tft.drawFastHLine(0, y, tft.width(), color1);
	}
	for (int16_t x = 0; x < tft.width(); x += 5)
	{
		tft.drawFastVLine(x, 0, tft.height(), color2);
	}
}

void testdrawrects(uint16_t color)
{
	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = 0; x < tft.width(); x += 6)
	{
		tft.drawRect(tft.width() / 2 - x / 2, tft.height() / 2 - x / 2, x, x, color);
	}
}

void testfillrects(uint16_t color1, uint16_t color2)
{
	tft.fillScreen(ST77XX_BLACK);
	for (int16_t x = tft.width() - 1; x > 6; x -= 6)
	{
		tft.fillRect(tft.width() / 2 - x / 2, tft.height() / 2 - x / 2, x, x, color1);
		tft.drawRect(tft.width() / 2 - x / 2, tft.height() / 2 - x / 2, x, x, color2);
	}
}

void testfillcircles(uint8_t radius, uint16_t color)
{
	for (int16_t x = radius; x < tft.width(); x += radius * 2)
	{
		for (int16_t y = radius; y < tft.height(); y += radius * 2)
		{
			tft.fillCircle(x, y, radius, color);
		}
	}
}

void testdrawcircles(uint8_t radius, uint16_t color)
{
	for (int16_t x = 0; x < tft.width() + radius; x += radius * 2)
	{
		for (int16_t y = 0; y < tft.height() + radius; y += radius * 2)
		{
			tft.drawCircle(x, y, radius, color);
		}
	}
}

void testtriangles()
{
	tft.fillScreen(ST77XX_BLACK);
	uint16_t color = 0xF800;
	int t;
	int w = tft.width() / 2;
	int x = tft.height() - 1;
	int y = 0;
	int z = tft.width();
	for (t = 0; t <= 15; t++)
	{
		tft.drawTriangle(w, y, y, x, z, x, color);
		x -= 4;
		y += 4;
		z -= 4;
		color += 100;
	}
}

void testroundrects()
{
	tft.fillScreen(ST77XX_BLACK);
	uint16_t color = 100;
	int i;
	int t;
	for (t = 0; t <= 4; t += 1)
	{
		int x = 0;
		int y = 0;
		int w = tft.width() - 2;
		int h = tft.height() - 2;
		for (i = 0; i <= 16; i += 1)
		{
			tft.drawRoundRect(x, y, w, h, 5, color);
			x += 2;
			y += 3;
			w -= 4;
			h -= 6;
			color += 1100;
		}
		color += 100;
	}
}

void tftPrintTest()
{
	tft.setTextWrap(false);
	tft.fillScreen(ST77XX_BLACK);
	tft.setCursor(0, 30);
	tft.setTextColor(ST77XX_RED);
	tft.setTextSize(1);
	tft.println("Hello World!");
	tft.setTextColor(ST77XX_YELLOW);
	tft.setTextSize(2);
	tft.println("Hello World!");
	tft.setTextColor(ST77XX_GREEN);
	tft.setTextSize(3);
	tft.println("Hello World!");
	tft.setTextColor(ST77XX_BLUE);
	tft.setTextSize(4);
	tft.print(1234.567);
	delay(1500);
	tft.setCursor(0, 0);
	tft.fillScreen(ST77XX_BLACK);
	tft.setTextColor(ST77XX_WHITE);
	tft.setTextSize(0);
	tft.println("Hello World!");
	tft.setTextSize(1);
	tft.setTextColor(ST77XX_GREEN);
	tft.print(p, 6);
	tft.println(" Want pi?");
	tft.println(" ");
	tft.print(8675309, HEX); // print 8,675,309 out in HEX!
	tft.println(" Print HEX!");
	tft.println(" ");
	tft.setTextColor(ST77XX_WHITE);
	tft.println("Sketch has been");
	tft.println("running for: ");
	tft.setTextColor(ST77XX_MAGENTA);
	tft.print(millis() / 1000);
	tft.setTextColor(ST77XX_WHITE);
	tft.print(" seconds.");
}

void mediabuttons()
{
	// play
	tft.fillScreen(ST77XX_BLACK);
	tft.fillRoundRect(25, 10, 78, 60, 8, ST77XX_WHITE);
	tft.fillTriangle(42, 20, 42, 60, 90, 40, ST77XX_RED);
	delay(500);
	// pause
	tft.fillRoundRect(25, 90, 78, 60, 8, ST77XX_WHITE);
	tft.fillRoundRect(39, 98, 20, 45, 5, ST77XX_GREEN);
	tft.fillRoundRect(69, 98, 20, 45, 5, ST77XX_GREEN);
	delay(500);
	// play color
	tft.fillTriangle(42, 20, 42, 60, 90, 40, ST77XX_BLUE);
	delay(50);
	// pause color
	tft.fillRoundRect(39, 98, 20, 45, 5, ST77XX_RED);
	tft.fillRoundRect(69, 98, 20, 45, 5, ST77XX_RED);
	// play color
	tft.fillTriangle(42, 20, 42, 60, 90, 40, ST77XX_GREEN);
}

