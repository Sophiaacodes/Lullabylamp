#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <math.h>

const char* ssid = "EOLO_039618";
const char* password = "Q8AeshLR6";

const int PIN_MOSFET = 16;
const int PIN_TOUCH = 4;

// DURATE
const unsigned long DURATION_SUNSET = 10;
const unsigned long DURATION_SUNRISE = 30;
unsigned long standardDurationMinutes = 30;

RTC_DS1307 rtc; 
WebServer server(80);

const int PWM_FREQ = 5000;
const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8;

// STATI
int currentBrightness = 0;
bool isLightOn = false;
bool isSunsetMode = false;
bool isSunriseMode = false;

// DEBUG: Variabile per salvare l'ultimo messaggio ricevuto
String lastWebhookMessage = "Nessun segnale ricevuto ancora.";

unsigned long animationStartTime = 0;
unsigned long currentDurationMillis = 0;
int lastTouchState = LOW;

// PROTOTIPI
void startSunset(unsigned long minutes);
void startSunrise(unsigned long minutes);
void updateSunset();
void updateSunrise();
void handleTouch();

void setup() {
  Serial.begin(115200);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);
  pinMode(PIN_TOUCH, INPUT);

  if (!rtc.begin()) Serial.println("RTC ERROR");
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  // --- 1. WEBHOOK (Input dall'App) ---
  server.on("/webhook", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "No body data");
      return;
    }
    
    // SALVIAMO IL MESSAGGIO NELLA VARIABILE GLOBALE PER VISUALIZZARLO
    String body = server.arg("plain");
    lastWebhookMessage = body; // <--- Qui salviamo il testo per la pagina web
    Serial.println("WEBHOOK: " + body);

    if (body.indexOf("sleep_tracking_started") >= 0) {
       if (!isLightOn && currentBrightness == 0) {
         server.send(200, "text/plain", "Ignorato: Luce gia' spenta.");
       } else {
         startSunset(DURATION_SUNSET);
         server.send(200, "text/plain", "Tramonto OK");
       }
    }
    else if (body.indexOf("alarm_alert_start") >= 0 || body.indexOf("smart_period") >= 0) {
       if (isLightOn && currentBrightness == 255) {
         server.send(200, "text/plain", "Ignorato: Luce gia' accesa.");
       } else {
         startSunrise(DURATION_SUNRISE);
         server.send(200, "text/plain", "Alba OK");
       }
    }
    else {
      server.send(200, "text/plain", "Evento ricevuto ma non riconosciuto.");
    }
  });

  // --- 2. COMANDO MANUALE ---
  server.on("/manual", []() {
    if (!isLightOn) { isLightOn = true; ledcWrite(PWM_CHANNEL, 255); }
    startSunset(standardDurationMinutes);
    server.sendHeader("Location", "/");
    server.send(303); 
  });

  // --- 3. TEST ALBA ---
  server.on("/test-alba", []() {
    startSunrise(DURATION_SUNRISE); 
    server.sendHeader("Location", "/");
    server.send(303);
  });

  // --- 4. INTERFACCIA WEB (Con Debugger) ---
  server.on("/", []() {
    DateTime now = rtc.now();
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    // Aggiungo un refresh automatico ogni 5 secondi per vedere se arrivano messaggi
    html += "<meta http-equiv='refresh' content='5'>"; 
    html += "</head>";
    html += "<body style='font-family:sans-serif; text-align:center; padding:20px;'>";
    html += "<h1>Domotica Sonno</h1>";
    
    // Stato
    html += "<div style='border:1px solid #ccc; padding:10px; margin-bottom:20px;'>";
    html += "<p>Ora RTC: <b>" + String(now.hour()) + ":" + String(now.minute()) + "</b></p>";
    html += "<p>Stato Luce: <b>" + String(isLightOn ? "ACCESA" : "SPENTA") + "</b> (" + String(currentBrightness) + ")</p>";
    if (isSunsetMode) html += "<p style='color:blue; font-weight:bold;'>TRAMONTO IN CORSO</p>";
    if (isSunriseMode) html += "<p style='color:orange; font-weight:bold;'>ALBA IN CORSO</p>";
    html += "</div>";

    // Debugger Box (NUOVO)
    html += "<div style='background:#f0f0f0; padding:10px; margin-bottom:20px; border-radius:5px;'>";
    html += "<h3>Ultimo Segnale dall'App:</h3>";
    html += "<textarea rows='4' style='width:100%; font-family:monospace;' readonly>" + lastWebhookMessage + "</textarea>";
    html += "<p style='font-size:12px; color:#666;'>(La pagina si aggiorna ogni 5 sec)</p>";
    html += "</div>";

    // Pulsanti
    html += "<a href='/manual'><button style='padding:15px; background:#4CAF50; color:white; border:none; margin:5px; width:100%;'>TRAMONTO (30 min)</button></a><br>";
    html += "<a href='/test-alba'><button style='padding:15px; background:#FF9800; color:white; border:none; margin:5px; width:100%;'>TEST ALBA (30 min)</button></a>";
    
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.begin();
}

void loop() {
  server.handleClient();
  handleTouch();
  if (isSunsetMode) updateSunset();
  if (isSunriseMode) updateSunrise();
}

// LOGICA
void startSunset(unsigned long minutes) {
  isSunsetMode = true; isSunriseMode = false;
  animationStartTime = millis();
  currentDurationMillis = minutes * 60 * 1000;
  isLightOn = true;
}

void startSunrise(unsigned long minutes) {
  isSunriseMode = true; isSunsetMode = false;
  animationStartTime = millis();
  currentDurationMillis = minutes * 60 * 1000;
  isLightOn = true; 
  currentBrightness = 0; ledcWrite(PWM_CHANNEL, 0);
}

void handleTouch() {
  int touchState = digitalRead(PIN_TOUCH);
  if (touchState == HIGH && lastTouchState == LOW) {
    isSunsetMode = false; isSunriseMode = false; 
    isLightOn = !isLightOn;
    currentBrightness = isLightOn ? 255 : 0;
    ledcWrite(PWM_CHANNEL, currentBrightness);
    delay(300);
  }
  lastTouchState = touchState;
}

void updateSunset() {
  unsigned long t = millis() - animationStartTime;
  if (t >= currentDurationMillis) {
    ledcWrite(PWM_CHANNEL, 0); isSunsetMode = false; isLightOn = false; currentBrightness=0;
  } else {
    float rem = 1.0 - ((float)t / currentDurationMillis);
    int val = (int)(pow(rem, 5.0) * 255);
    if (val != currentBrightness) { currentBrightness = val; ledcWrite(PWM_CHANNEL, val); }
  }
}

void updateSunrise() {
  unsigned long t = millis() - animationStartTime;
  if (t >= currentDurationMillis) {
    ledcWrite(PWM_CHANNEL, 255); isSunriseMode = false; isLightOn = true; currentBrightness=255;
  } else {
    float prog = (float)t / currentDurationMillis;
    int val = (int)(pow(prog, 3.0) * 255);
    if (val != currentBrightness) { currentBrightness = val; ledcWrite(PWM_CHANNEL, val); }
  }
}
