#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <math.h>

// --- WIFI CREDENTIALS ---
const char* ssid = "WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// --- PIN CONFIGURATION ---
const int PIN_MOSFET = 16;
const int PIN_TOUCH = 4;

// --- DURATION SETTINGS (Minutes) ---
const unsigned long DURATION_SUNSET_SLEEP = 10;   // Short sunset triggered by Sleep App
const unsigned long DURATION_SUNRISE_ALARM = 30;  // Sunrise triggered by Alarm
unsigned long standardDurationMinutes = 30;       // Manual sunset triggered via browser/button

// --- OBJECTS ---
RTC_DS1307 rtc; 
WebServer server(80);

// --- PWM SETTINGS ---
const int PWM_FREQ = 5000;
const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8;

// --- SYSTEM STATES ---
int currentBrightness = 0;
bool isLightOn = false;
bool isSunsetMode = false;
bool isSunriseMode = false;

// --- TIMING VARIABLES ---
unsigned long animationStartTime = 0;
unsigned long currentDurationMillis = 0;
int lastTouchState = LOW;

// --- FUNCTION PROTOTYPES ---
void handleTouch();
void startSunset(unsigned long durationMinutes);
void startSunrise(unsigned long durationMinutes);
void updateSunset();
void updateSunrise();

void setup() {
  Serial.begin(115200);

  // 1. PWM Configuration (LED Control)
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // 2. Touch Sensor Configuration
  pinMode(PIN_TOUCH, INPUT);

  // 3. RTC Configuration (Smart Adjust)
  if (!rtc.begin()) {
    Serial.println("ERROR: RTC not found");
  }

  // CHECK: If RTC is not running (e.g., new battery), set the time to compile time.
  // If it IS running, do NOT change the time. This prevents the "reset loop".
  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running. Setting time to compile time...");
    rtc.adjust(DateTime(__DATE__, __TIME__));
  } else {
    Serial.println("RTC is running. Time is preserved.");
  }

  // 4. Wi-Fi Connection
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi Offline. Manual mode only.");
  }

  // --- WEB SERVER ENDPOINTS ---

  // Endpoint: SLEEP (Triggered by Android App when user falls asleep)
  server.on("/sleep", []() {
    // Logic: If light is already OFF, ignore sleep command.
    if (!isLightOn && currentBrightness == 0) {
      server.send(200, "text/plain", "Ignored: Light is already OFF.");
      return; 
    }
    
    // Logic: If Sunrise is active (morning), ignore sleep command.
    if (isSunriseMode) {
       server.send(200, "text/plain", "Ignored: Sunrise in progress.");
       return;
    }

    Serial.println(">>> SLEEP TRIGGERED: Starting short sunset <<<");
    startSunset(DURATION_SUNSET_SLEEP);
    server.send(200, "text/plain", "Goodnight. Sunset started (10 min).");
  });

  // Endpoint: SUNRISE (Triggered by Android App Alarm)
  server.on("/sunrise", []() {
    Serial.println(">>> WAKE UP: Starting sunrise <<<");
    
    // Logic: If light is already MAX, ignore.
    if (isLightOn && currentBrightness == 255) {
      server.send(200, "text/plain", "Ignored: Light already ON.");
      return;
    }

    startSunrise(DURATION_SUNRISE_ALARM);
    server.send(200, "text/plain", "Good morning. Sunrise started (30 min).");
  });

  // Endpoint: MANUAL (Triggered via Browser for reading/relaxing)
  server.on("/manual", []() {
    // If light is off, turn it on first
    if (!isLightOn) { 
      isLightOn = true; 
      ledcWrite(PWM_CHANNEL, 255); 
    }
    startSunset(standardDurationMinutes);
    server.send(200, "text/plain", "Manual Sunset Started (30 min).");
  });

  // Endpoint: ROOT (Status check)
  server.on("/", []() {
    String status = "System Online.\n";
    status += "Light: " + String(isLightOn ? "ON" : "OFF") + "\n";
    status += "Time: " + String(rtc.now().hour()) + ":" + String(rtc.now().minute());
    server.send(200, "text/plain", status);
  });

  server.begin();
  Serial.println("Web Server Started.");
}

void loop() {
  server.handleClient();
  handleTouch();
  
  if (isSunsetMode) updateSunset();
  if (isSunriseMode) updateSunrise();
}

// --- CORE FUNCTIONS ---

void startSunset(unsigned long durationMinutes) {
  isSunsetMode = true;
  isSunriseMode = false; // Stop sunrise if active
  animationStartTime = millis();
  currentDurationMillis = durationMinutes * 60 * 1000;
  isLightOn = true; // Technically on during dimming
}

void startSunrise(unsigned long durationMinutes) {
  isSunriseMode = true;
  isSunsetMode = false; // Stop sunset if active
  animationStartTime = millis();
  currentDurationMillis = durationMinutes * 60 * 1000;
  
  isLightOn = true; 
  currentBrightness = 0; // Start from black
  ledcWrite(PWM_CHANNEL, 0);
}

void handleTouch() {
  int touchState = digitalRead(PIN_TOUCH);
  
  // Detect Rising Edge (Touch detected)
  if (touchState == HIGH && lastTouchState == LOW) {
    // MANUAL OVERRIDE: Touch stops everything.
    isSunsetMode = false;
    isSunriseMode = false;
    
    // Toggle Light
    isLightOn = !isLightOn;
    currentBrightness = isLightOn ? 255 : 0;
    ledcWrite(PWM_CHANNEL, currentBrightness);
    
    Serial.println(isLightOn ? "Touch: Light ON" : "Touch: Light OFF");
    delay(300); // Debounce
  }
  lastTouchState = touchState;
}

// --- MATH & ANIMATION ---

void updateSunset() {
  unsigned long timeElapsed = millis() - animationStartTime;
  
  if (timeElapsed >= currentDurationMillis) {
    // Animation Finished
    ledcWrite(PWM_CHANNEL, 0);
    isSunsetMode = false;
    isLightOn = false;
    currentBrightness = 0;
    Serial.println("Sunset Complete. Light OFF.");
  } else {
    float progress = (float)timeElapsed / currentDurationMillis;
    float remaining = 1.0 - progress;
    
    // Gamma 5.0 Curve: Drops fast initially, long tail at the end.
    // Ideal for sleep induction.
    int newBrightness = (int)(pow(remaining, 5.0) * 255);
    
    if (newBrightness != currentBrightness) {
      currentBrightness = newBrightness;
      ledcWrite(PWM_CHANNEL, currentBrightness);
    }
  }
}

void updateSunrise() {
  unsigned long timeElapsed = millis() - animationStartTime;
  
  if (timeElapsed >= currentDurationMillis) {
    // Animation Finished
    ledcWrite(PWM_CHANNEL, 255);
    isSunriseMode = false;
    isLightOn = true;
    currentBrightness = 255;
    Serial.println("Sunrise Complete. Light MAX.");
  } else {
    float progress = (float)timeElapsed / currentDurationMillis;
    
    // Inverse Gamma 3.0 Curve: Rises very slowly initially.
    // Prevents waking up with a shock.
    int newBrightness = (int)(pow(progress, 3.0) * 255);
    
    if (newBrightness != currentBrightness) {
      currentBrightness = newBrightness;
      ledcWrite(PWM_CHANNEL, currentBrightness);
    }
  }
}
