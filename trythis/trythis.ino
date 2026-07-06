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

// -------------------- BUZZERS --------------------
#define BUZZER1_PIN 1
#define BUZZER2_PIN 2
#define BUZZER3_PIN 3

#define LEDC_CH1 0
#define LEDC_CH2 1
#define LEDC_CH3 2

#define LEDC_RES 8

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

float pitchSmooth = 0;

// -------------------- SETUP PWM --------------------
void setupPWM() {
	pinMode(BUZZER1_PIN, OUTPUT);
	pinMode(BUZZER2_PIN, OUTPUT);
	pinMode(BUZZER3_PIN, OUTPUT);
	// attach PWM freq resolution via new API
	ledcAttach(BUZZER1_PIN, 2000, LEDC_RES);
	ledcAttach(BUZZER2_PIN, 2000, LEDC_RES);
	ledcAttach(BUZZER3_PIN, 2000, LEDC_RES);

}
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

// -------------------- BUZZER OUTPUT --------------------
void updateSound(int baseFreq, int velocity) {
	if (!sounding || velocity <= 0) {
		ledcWriteTone(BUZZER1_PIN, 0);
		ledcWriteTone(BUZZER2_PIN, 0);
		ledcWriteTone(BUZZER3_PIN, 0);
		return;
	}

	float vol = velocity / 127.0;

	float f1 = baseFreq;
	float f2 = baseFreq * 1.01;
	float f3 = baseFreq * 0.5;
	uint32_t duty = (uint32_t)(vol * 255);

	ledcWriteTone(BUZZER1_PIN, (int)f1);
	ledcWriteTone(BUZZER2_PIN, (int)f2);
	ledcWriteTone(BUZZER3_PIN, (int)f3);

	ledcWrite(BUZZER1_PIN, duty);
	ledcWrite(BUZZER2_PIN, duty);
	ledcWrite(BUZZER3_PIN, duty / 2);

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
	Serial.begin(9600);

	SPI.begin();

	tft.initR(INITR_GREENTAB);
	tft.fillScreen(ST77XX_BLACK);

	Wire.begin(I2C_SDA, I2C_SCL);

	pinMode(PITCH_XSHUT, OUTPUT);
	pinMode(VOLUME_XSHUT, OUTPUT);
	digitalWrite(PITCH_XSHUT, LOW);
	digitalWrite(VOLUME_XSHUT, LOW);
	delay(10);

	setupPWM();

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

	uint16_t pitchMm = readMm(pitchSensor);
	uint16_t volumeMm = readMm(volumeSensor);

	if (pitchMm) lastValidPitchMm = pitchMm;
	pitchMm = lastValidPitchMm;

	int note = mapPitchToNote(pitchMm);
	int freq = noteToFreq(note);
	int velocity = mapVolumeToVelocity(volumeMm);

	// sound gate
	if (volumeMm > VOLUME_ON_MM) sounding = true;
	if (volumeMm < VOLUME_OFF_MM) sounding = false;

	// smoothing pitch
	pitchSmooth = pitchSmooth * 0.85 + freq * 0.15;

	updateSound((int)pitchSmooth, velocity);

	// minimal debug
	Serial.print("freq ");
	Serial.print(freq);
	Serial.print(" vel ");
	Serial.println(velocity);

	delay(10);
}
