/*
 *  Typically, the "Servo.h" library would be used, but
 *  because servos and programmable LEDs both rely on interrupts,
 *  the "Adafruit_TiCoServo.h" is used instead. This library
 *  generates PWM pulses for servo control via hardware peripherals
 *  instead of timer interrupts. If the normal "Servo.h" library
 *  is used, the servos jitter uncontrollably and the LEDs
 *  generally don't respond as expected.
 */
#include <Adafruit_NeoPixel.h>    // RGB LED Library
#include <Adafruit_TiCoServo.h>   // Servo library
#include <known_16bit_timers.h>   // Servo library dependency

// Constants for LEDs
#define RGB_LED_DATA_PIN  4  // Arduino data pin
#define LED_COUNT         16 // Total number of LEDs

// Possible state machine states
typedef enum _mode {
  MODE_NORMAL = 0,
  MODE_AUTO,
  MODE_LED
} MODE;

// Arduino pin connections
const int BUTTON = 12;
const int LED = 6;
const int PAN_POT = A0;
const int TILT_POT = A1;
const int LASER = 5;
// Servo pan and tilt MUST be 9 and 10 because of Adafruit_TiCoServo library limitations
const int PAN_PIN = 9;
const int TILT_PIN = 10;

// Servo range limits in degrees
// These prevent the servos from moving to useless physical positions
const int MIN_PAN = 23;
const int MAX_PAN = 150;
const int MIN_TILT = 2;
const int MAX_TILT = 37;

// Servo objects
Adafruit_TiCoServo pan;
Adafruit_TiCoServo tilt;

// Current servo position vars
int tilt_pos = 0;
int pan_pos = 0;

// RGB LED strand object
Adafruit_NeoPixel strip(LED_COUNT, RGB_LED_DATA_PIN, NEO_GRB + NEO_KHZ800);

// State machine state
MODE run_mode = MODE_NORMAL;

// Initial laser PWM value
int laser_brightness = 255;

// Debug var for toggling serial printing
const bool POSITION_DEBUG = true;

/*
 *  Initial configuration
 */
void setup() {
  // Setup serial
  Serial.begin(115200);

  // Setup servos
  pan.attach(PAN_PIN);
  tilt.attach(TILT_PIN);
  
  // Setup RGB LEDs
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)

  // Setup other peripherals
  pinMode(LASER, OUTPUT);
  pinMode(LED, OUTPUT);
  // Use internal pullup resistor to avoid floating pin when button is not pressed
  pinMode(BUTTON, INPUT_PULLUP);
  digitalWrite(LED, LOW);
  analogWrite(LASER, laser_brightness);

  // Seed the random number generator with noise on an unused analog pin
  randomSeed(analogRead(3));

}

/*
 *  Main loop
 */
void loop() {

  // See if we need to switch states
  checkButton();

  // Run the current state
  switch (run_mode) {
    case MODE_NORMAL:
      runNormalMode();
      break;
    case MODE_AUTO:
      runAutoMode();
      break;
    case MODE_LED:
      runLEDMode();
      break;
  }

}

/*
 *  Checks to see if button is pressed and handles
 *  state machine transitions.
 */
void checkButton() {
  static bool button_was_pressed = false;
  static long last_button_check = millis();
  const int BUTTON_CHECK_INTERVAL = 100;

  // Only check once in a while to prevent reading phyical button "bounce"
  if (millis() - last_button_check > BUTTON_CHECK_INTERVAL) {

    // Because the default (unpressed) state on the button pin is high, 
    // due to the pull-up resistor, flip the logic
    bool button_pressed = !digitalRead(BUTTON);

    // While the button is held down...
    while (button_pressed && button_was_pressed) {
      int laser_brightness = map(analogRead(TILT_POT), 0, 1023, 0, 255);
      analogWrite(LASER, laser_brightness);
      button_pressed = !digitalRead(BUTTON);
    }

    // When the button is released
    if (!button_pressed && button_was_pressed) {
      Serial.println("Button pressed!");
      Serial.println(run_mode);

      // Transition to the next state
      switch (run_mode) {
        case MODE_NORMAL:
          {
            Serial.println("Entering auto mode");

            digitalWrite(LED, HIGH);

            run_mode = MODE_AUTO;

            break;
          }

        case MODE_AUTO:
          {
            Serial.println("Entering LED mode");

            digitalWrite(LED, LOW);

            // Convert the current potentiometer positions into a color
            // and brightness for the programmable RGB LEDs and set them
            int pan_raw = analogRead(PAN_POT);
            int tilt_raw = analogRead(TILT_POT);
            int fade_val = map(pan_raw, 0, 1023, 60, 255);
            int pixel_hue = map(tilt_raw, 0, 1023, 0, 65536);
            colorWipe(strip.gamma32(strip.ColorHSV(pixel_hue, 255, fade_val)), 40); // Off

            run_mode = MODE_LED;
            break;
          }

        case MODE_LED:
          {
            Serial.println("Entering normal mode");

            colorWipe(strip.Color(  0,   0, 0), 40); // Clear the programmable RGB LEDs

            run_mode = MODE_NORMAL;
            break;
          }

        default:
          {
            // We shouldn't ever end up here, but just in case...
            Serial.println("Invalid case");
            break;
          }
      }
    }

    // Save the current time to check against when computing the next interval
    last_button_check = millis();
    // Save the current button state to compare against next time
    button_was_pressed = button_pressed;
  }
}


// Noraml mode state
void runNormalMode() {
  int pan_raw = analogRead(PAN_POT);
  int tilt_raw = analogRead(TILT_POT);
  if (POSITION_DEBUG) {
    Serial.print(pan_raw);
    Serial.print(", ");
    Serial.println(tilt_raw);
  }
  pan_pos = map(pan_raw, 0, 1023, MAX_PAN, MIN_PAN);
  tilt_pos = map(tilt_raw, 0, 1023, MAX_TILT, MIN_TILT);
  updateServos();
  delay(50);
}


// Auto Mode State
void runAutoMode() {

  const int MIN_RAPID_INTERVAL = 100;
  const int MAX_RAPID_INTERVAL = 800;
  const int MIN_NORMAL_INTERVAL = 1000;
  const int MAX_NORMAL_INTERVAL = 4000;

  const int MIN_RAPID_MOVES = 10;
  const int MAX_RAPID_MOVES = 15;
  const int MIN_NORMAL_MOVES = 3;
  const int MAX_NORMAL_MOVES = 8;

  static bool rapid_mode = false;
  static long last_move = millis();
  static int move_interval = 0;
  static int rapid_move_max = random(MIN_RAPID_MOVES, MAX_RAPID_MOVES);
  static int normal_move_max = random(MIN_NORMAL_MOVES, MAX_NORMAL_MOVES);
  static int cur_rapid_moves = 0;
  static int cur_normal_moves = 0;

  if (cur_rapid_moves >= rapid_move_max) {
    rapid_move_max = random(MIN_RAPID_MOVES, MAX_RAPID_MOVES);
    cur_rapid_moves = 0;
    rapid_mode = false;
  }
  if (cur_normal_moves >= normal_move_max) {
    normal_move_max = random(MIN_NORMAL_MOVES, MAX_NORMAL_MOVES);
    cur_normal_moves = 0;
    rapid_mode = true;
  }

  if (millis() - last_move >= move_interval) {
    if (rapid_mode) {
      move_interval = random(MIN_RAPID_INTERVAL, MAX_RAPID_INTERVAL);
      cur_rapid_moves++;
    }
    else {
      move_interval = random(MIN_NORMAL_INTERVAL, MAX_NORMAL_INTERVAL);
      cur_normal_moves++;
    }

    int pan_pos = random(MIN_PAN, MAX_PAN);
    int tilt_pos;

    // In order to avoid shining the laser onto the toy's frame, limit
    // the tilt ranges in pan positions where that's a possibility
    if (pan_pos >= 144) {
      tilt_pos = random(24, MAX_TILT);
    }
    else if (pan_pos <= 54) {
      tilt_pos = random(12, MAX_TILT);
    }
    else {
      tilt_pos = random(MIN_TILT, MAX_TILT);
    }
    pan.write(pan_pos);
    tilt.write(tilt_pos);

    Serial.print("Pan: ");
    Serial.print(pan_pos);
    Serial.print(" Tilt: ");
    Serial.println(tilt_pos);

    last_move = millis();
  }
}


// LED Mode State
void runLEDMode() {

  int pan_raw = analogRead(PAN_POT);
  int tilt_raw = analogRead(TILT_POT);

  int fade_val = map(pan_raw, 0, 1023, 60, 255);
  int pixel_hue = map(tilt_raw, 0, 1023, 0, 65536);

  for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip...
    // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
    // optionally add saturation and value (brightness) (each 0 to 255).
    // Here we're using just the three-argument variant, though the
    // second value (saturation) is a constant 255.
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixel_hue, 255, fade_val)));
  }
  strip.show();
  delay(10);
}


void updateServos() {
  static int old_pan_pos = 0;
  static int old_tilt_pos = 0;

  // Requiring a change of greater than 1 cuts down on jitter
  if (abs(old_pan_pos - pan_pos) > 1) {
    pan.write(pan_pos);
    if (POSITION_DEBUG) {
      Serial.print("Pan: ");
      Serial.print(pan_pos);
      Serial.print(" ");
    }
  }

  // Requiring a change of greater than 1 cuts down on jitter
  if (abs(old_tilt_pos - tilt_pos) > 1) {
    tilt.write(tilt_pos);
    if (POSITION_DEBUG) {
      Serial.print("Tilt: ");
      Serial.print(tilt_pos);
    }
  }
  if (POSITION_DEBUG) {
    Serial.println("\n");
  }
}


void colorWipe(uint32_t color, int wait) {
  for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
    delay(wait);                           //  Pause for a moment
  }
}
