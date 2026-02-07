#include <Wire.h>
#include <RTClib.h>

// --- CONFIGURAZIONE PIN ---
const int PIN_MOSFET = 16;  // IL TUO PIN FUNZIONANTE
const int PIN_TOUCH = 4;    // Il pin del sensore touch (se lo hai collegato)

// --- IMPOSTAZIONI TRAMONTO ---
const int SUNSET_HOUR = 20;     // A che ora inizia (es. 20 = 8 di sera)
const int SUNSET_MINUTE = 30;   // A che minuto inizia
const int FADE_DELAY = 100;     // Velocità dissolvenza (più alto = più lento)

// --- SETUP TECNICO (NON TOCCARE) ---
const int PWM_FREQ = 5000;
const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8; 

RTC_DS1307 rtc; // Se usi un DS3231 cambia in: RTC_DS3231 rtc;

int currentBrightness = 0;
bool isLightOn = false;
bool isSunsetMode = false;
unsigned long lastFadeStep = 0;
int lastTouchState = LOW;

void setup() {
  Serial.begin(115200);

  // Configurazione PWM
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0); // Parte spento

  // Avvio Orologio
  if (!rtc.begin()) {
    Serial.println("ERRORE: RTC non trovato! Controlla i cavi SDA/SCL.");
    // Se non hai l'RTC collegato, il codice si blocca qui.
    // Se vuoi testare SENZA orologio, commenta il 'while(1)' qui sotto.
    while (1);
  }

  // --- IMPOSTAZIONE ORA (DA USARE SOLO LA PRIMA VOLTA) ---
  // Togli le // qui sotto per sincronizzare l'ora, carica, poi rimettile e ricarica.
  // rtc.adjust(DateTime(F_DATE, F_TIME));

  pinMode(PIN_TOUCH, INPUT);
  Serial.println("Sistema Avviato. In attesa...");
}

void loop() {
  DateTime now = rtc.now();
  
  // --- CONTROLLO MANUALE (TOUCH) ---
  int touchState = digitalRead(PIN_TOUCH);
  
  if (touchState == HIGH && lastTouchState == LOW) {
    isSunsetMode = false; // Interrompe il tramonto se tocchi
    isLightOn = !isLightOn; // Inverte on/off
    
    currentBrightness = isLightOn ? 255 : 0;
    ledcWrite(PWM_CHANNEL, currentBrightness);
    
    Serial.print("Touch rilevato: Luce ");
    Serial.println(isLightOn ? "ON" : "OFF");
    delay(500); 
  }
  lastTouchState = touchState;

  // --- CONTROLLO ORARIO (TRAMONTO) ---
  // Verifica se è l'ora X e se la luce è accesa
  if (now.hour() == SUNSET_HOUR && now.minute() == SUNSET_MINUTE && isLightOn && !isSunsetMode) {
    if (currentBrightness > 0) {
      isSunsetMode = true;
      Serial.println(">>> Inizio Tramonto Automatico");
    }
  }

  // --- ESECUZIONE DISSOLVENZA ---
  if (isSunsetMode) {
    if (millis() - lastFadeStep > FADE_DELAY) {
      lastFadeStep = millis();
      if (currentBrightness > 0) {
        currentBrightness--;
        ledcWrite(PWM_CHANNEL, currentBrightness);
        // Serial.println(currentBrightness); // Debug
      } else {
        isSunsetMode = false;
        isLightOn = false;
        Serial.println("Tramonto finito. Buonanotte.");
      }
    }
  }
}
