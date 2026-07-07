#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <math.h>

// -------------------- PINS --------------------
#define PITCH_ADDR 0x30
#define VOLUME_ADDR 0x31

#define I2C_SDA 8
#define I2C_SCL 46

#define PITCH_XSHUT 4
#define VOLUME_XSHUT 5

#define TFT_CS 10
#define TFT_RST 9
#define TFT_DC 14

#define SCREEN_W 128
#define SCREEN_H 160

// -------------------- AUDIO STREAMING --------------------
#define SAMPLE_RATE 22050
#define FRAME_SAMPLES 64
#define SERIAL_BAUD 230400
#define SYNC_BYTE1 0xAA
#define SYNC_BYTE2 0x55
#define SINE_LUT_SIZE 256

// -------------------- SENSOR LIMITS --------------------
#define PITCH_MIN_MM 30
#define PITCH_MAX_MM 320
#define NOTE_MIN 48
#define NOTE_MAX 84

#define VOLUME_MIN_MM 30
#define VOLUME_MAX_MM 480
#define VOLUME_ON_MM 70
#define VOLUME_OFF_MM 110

// -------------------- GLOBALS --------------------
Adafruit_VL53L0X pitchSensor;
Adafruit_VL53L0X volumeSensor;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

bool sounding = false;
uint16_t lastValidPitchMm = 150;
uint16_t lastValidVolumeMm = 300;

float pitchSmooth = 0;

// Audio synthesis state (shared with ISR)
volatile float g_baseFreq = 440.0f;
volatile float g_velocity = 0.0f;
volatile bool g_sounding = false;

// Sine lookup table
int16_t sineLut[SINE_LUT_SIZE];

// Timer and frame buffer
hw_timer_t *audioTimer = NULL;
volatile int16_t frameBuffer[FRAME_SAMPLES];
volatile uint8_t frameIndex = 0;
volatile bool frameReady = false;

// Phase accumulators for 3 oscillators
volatile uint32_t phase1 = 0, phase2 = 0, phase3 = 0;
volatile uint32_t phaseInc1 = 0, phaseInc2 = 0, phaseInc3 = 0;

// -------------------- HELPERS --------------------
int noteToFreq(int note) {
	return (int)(440.0 * pow(2.0, (note - 69) / 12.0));
}

int mapPitchToNote(uint16_t mm) {
	return map(mm, PITCH_MIN_MM, PITCH_MAX_MM, NOTE_MIN, NOTE_MAX);
}

int mapVolumeToVelocity(uint16_t mm) {
	if (mm < VOLUME_ON_MM) return 0;
	if (mm > VOLUME_MAX_MM) return 127;
	return map(mm, VOLUME_ON_MM, VOLUME_MAX_MM, 0, 127);
}

// Fast sine from lookup table (linear interpolation)
inline int16_t sineLerp(uint32_t phase) {
	uint16_t idx = phase >> 24;
	uint8_t frac = (phase >> 16) & 0xFF;
	int16_t a = sineLut[idx];
	int16_t b = sineLut[(idx + 1) & (SINE_LUT_SIZE - 1)];
	return a + ((int32_t)(b - a) * frac >> 8);
}

// -------------------- AUDIO ISR --------------------
void IRAM_ATTR onAudioTimer() {
	if (!g_sounding || g_velocity <= 0.0f) {
		frameBuffer[frameIndex++] = 0;
		if (frameIndex >= FRAME_SAMPLES) {
			frameIndex = 0;
			frameReady = true;
		}
		return;
	}

	phase1 += phaseInc1;
	phase2 += phaseInc2;
	phase3 += phaseInc3;

	int32_t sum = sineLerp(phase1) + sineLerp(phase2) + sineLerp(phase3);
	int16_t sample = (int32_t)(sum * g_velocity * 0.33333f * 32767.0f) >> 15;

	frameBuffer[frameIndex++] = sample;

	if (frameIndex >= FRAME_SAMPLES) {
		frameIndex = 0;
		frameReady = true;
	}
}

// -------------------- SERIAL FRAMING --------------------
void sendFrame() {
	if (Serial.availableForWrite() < FRAME_SAMPLES * 2 + 5) return;

	uint8_t checksum = 0;
	Serial.write(SYNC_BYTE1);
	Serial.write(SYNC_BYTE2);
	Serial.write(FRAME_SAMPLES);
	checksum ^= SYNC_BYTE1;
	checksum ^= SYNC_BYTE2;
	checksum ^= FRAME_SAMPLES;

	for (uint8_t i = 0; i < FRAME_SAMPLES; i++) {
		uint8_t lo = frameBuffer[i] & 0xFF;
		uint8_t hi = (frameBuffer[i] >> 8) & 0xFF;
		Serial.write(lo);
		Serial.write(hi);
		checksum ^= lo;
		checksum ^= hi;
	}
	Serial.write(checksum);
}

// -------------------- SENSOR --------------------
bool bringUpSensor(uint8_t xshutPin, uint8_t address, Adafruit_VL53L0X &sensor) {
	digitalWrite(xshutPin, HIGH);
	delay(10);
	return sensor.begin(address);
}

uint16_t readMm(Adafruit_VL53L0X &sensor) {
	VL53L0X_RangingMeasurementData_t m;
	sensor.rangingTest(&m, false);
	if (m.RangeStatus == 4) return 0;
	return m.RangeMilliMeter;
}

// -------------------- SETUP --------------------
void setup() {
	Serial.begin(SERIAL_BAUD);

	SPI.begin();

	tft.initR(INITR_GREENTAB);
	tft.fillScreen(ST77XX_BLACK);

	Wire.begin(I2C_SDA, I2C_SCL);

	pinMode(PITCH_XSHUT, OUTPUT);
	pinMode(VOLUME_XSHUT, OUTPUT);
	digitalWrite(PITCH_XSHUT, LOW);
	digitalWrite(VOLUME_XSHUT, LOW);
	delay(10);

	// Build sine lookup table
	for (int i = 0; i < SINE_LUT_SIZE; i++) {
		sineLut[i] = (int16_t)(sinf(2.0f * PI * i / SINE_LUT_SIZE) * 32767.0f);
	}

        // Configure audio timer (22050 Hz) - ESP32 Arduino core v3.x API
        audioTimer = timerBegin(1000000);  // 1 MHz base frequency
        timerAttachInterrupt(audioTimer, &onAudioTimer);
        timerAlarm(audioTimer, 1000000 / SAMPLE_RATE, true, 0);
        timerStart(audioTimer);

	if (!bringUpSensor(PITCH_XSHUT, PITCH_ADDR, pitchSensor)) {
		Serial.println("Pitch fail");
	}
	if (!bringUpSensor(VOLUME_XSHUT, VOLUME_ADDR, volumeSensor)) {
		Serial.println("Volume fail");
	}

	tft.setCursor(0, 0);
	tft.setTextColor(ST77XX_GREEN);
	tft.println("Theremin OK");
	delay(1000);
}

// -------------------- LOOP --------------------
void loop() {
    static uint32_t lastPitchRead = 0;
    static uint32_t lastVolumeRead = 0;
    static bool readPitch = true;
    uint32_t now = millis();

    // Stagger sensor reads to reduce peak current (50ms apart)
    if (readPitch && (now - lastPitchRead >= 50)) {
        uint16_t pitchMm = readMm(pitchSensor);
        if (pitchMm) lastValidPitchMm = pitchMm;
        lastPitchRead = now;
        readPitch = false;
    } else if (!readPitch && (now - lastVolumeRead >= 50)) {
        uint16_t volumeMm = readMm(volumeSensor);
        if (volumeMm) lastValidVolumeMm = volumeMm;
        lastVolumeRead = now;
        readPitch = true;
    }

    uint16_t pitchMm = lastValidPitchMm;
    uint16_t volumeMm = lastValidVolumeMm;

    int note = mapPitchToNote(pitchMm);
    int freq = noteToFreq(note);
    int velocity = mapVolumeToVelocity(volumeMm);

    // sound gate with hysteresis
    if (volumeMm > VOLUME_OFF_MM) sounding = true;
    else if (volumeMm < VOLUME_ON_MM) sounding = false;

    // smoothing pitch
    pitchSmooth = pitchSmooth * 0.85 + freq * 0.15;

    // Update ISR globals atomically
    noInterrupts();
    g_baseFreq = pitchSmooth;
    g_velocity = velocity / 127.0f;
    g_sounding = sounding;

    float phaseIncBase = g_baseFreq * (float)(1ULL << 32) / SAMPLE_RATE;
    phaseInc1 = (uint32_t)phaseIncBase;
    phaseInc2 = (uint32_t)(phaseIncBase * 1.01f);
    phaseInc3 = (uint32_t)(phaseIncBase * 0.5f);
    interrupts();

    // Send completed frames
    if (frameReady) {
        frameReady = false;
        sendFrame();
    }

    // Debug output (throttled)
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug > 100) {
        Serial.print("freq ");
        Serial.print(freq);
        Serial.print(" vel ");
        Serial.println(velocity);
        lastDebug = millis();
    }
}
