#include <Wire.h>
#include <RTClib.h>
#include <math.h> // Serve per il calcolo della curva esponenziale

// --- CONFIGURAZIONE PIN ---
const int PIN_MOSFET = 16;  
const int PIN_TOUCH = 4;    

// --- CONFIGURAZIONE TRAMONTO ---
const int SUNSET_HOUR = 20;      // Ora di inizio (es. 20)
const int SUNSET_MINUTE = 30;    // Minuto di inizio (es. 30)
const int SUNSET_DURATION_MIN = 15; // DURATA TOTALE in minuti (es. 15 minuti)

// --- PARAMETRI MATEMATICI (SEZIONE AUREA) ---
// 1.618 è il numero d'oro (Phi). 
// Più alzi questo numero (es. 2.0 o 3.0), più la luce resterà accesa a lungo per poi crollare alla fine.
// Se metti 1.0, il tramonto sarà perfettamente lineare.
const float PHI_EXPONENT = 1.618; 

// --- SETUP TECNICO ---
const int PWM_FREQ = 5000;
const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8; 

RTC_DS1307 rtc; // Cambia in RTC_DS3231 se usi quello

int currentBrightness = 0;
bool isLightOn = false;
bool isSunsetMode = false;
int lastTouchState = LOW;

// Variabili per il calcolo del tempo
unsigned long sunsetStartTime = 0;
unsigned long totalDurationMillis = 0;

void setup() {
  Serial.begin(115200);

  // Configurazione PWM
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0); 

  // Avvio Orologio
  if (!rtc.begin()) {
    Serial.println("ERRORE: RTC non trovato!");
    while (1);
  }
  
  // rtc.adjust(DateTime(__DATE__, __TIME__)); // Togli le barre solo se devi risincronizzare

  pinMode(PIN_TOUCH, INPUT);
  
  // Calcolo durata in millisecondi (Minuti * 60 * 1000)
  totalDurationMillis = (unsigned long)SUNSET_DURATION_MIN * 60 * 1000;
  
  Serial.println("Sistema Avviato con Curva Aurea.");
}

void loop() {
  DateTime now = rtc.now();
  
  // --- CONTROLLO TOUCH ---
  int touchState = digitalRead(PIN_TOUCH);
  
  if (touchState == HIGH && lastTouchState == LOW) {
    isSunsetMode = false; // Interrompe il tramonto
    isLightOn = !isLightOn; // Inverte
    
    currentBrightness = isLightOn ? 255 : 0;
    ledcWrite(PWM_CHANNEL, currentBrightness);
    
    Serial.print("Touch: Luce ");
    Serial.println(isLightOn ? "ON" : "OFF");
    delay(500); 
  }
  lastTouchState = touchState;

  // --- CONTROLLO ORARIO ---
  if (now.hour() == SUNSET_HOUR && now.minute() == SUNSET_MINUTE && isLightOn && !isSunsetMode) {
    if (currentBrightness > 0) {
      isSunsetMode = true;
      sunsetStartTime = millis(); // Memorizza l'istante preciso in cui inizia
      Serial.println(">>> Inizio Tramonto Aureo (Phi)");
    }
  }

  // --- ALGORITMO TRAMONTO AUREO ---
  if (isSunsetMode) {
    unsigned long timeElapsed = millis() - sunsetStartTime;

    // Se il tempo è scaduto, spegni tutto
    if (timeElapsed >= totalDurationMillis) {
      currentBrightness = 0;
      isSunsetMode = false;
      isLightOn = false;
      ledcWrite(PWM_CHANNEL, 0);
      Serial.println("Tramonto completato.");
    } 
    else {
      // CALCOLO DELLA CURVA
      // 1. Calcoliamo la percentuale di tempo passato (da 0.0 a 1.0)
      float progress = (float)timeElapsed / totalDurationMillis;
      
      // 2. Applichiamo la formula inversa della potenza basata su Phi
      // La formula "1 - pow(progress, PHI)" fa sì che la luminosità scenda piano all'inizio e veloce alla fine.
      float curveValue = 1.0 - pow(progress, 1.0 / PHI_EXPONENT * 4); 
      // Nota: ho moltiplicato per 4 l'esponente per accentuare l'effetto "lento poi veloce"
      
      // Se vuoi essere matematicamente puro usa: float curveValue = 1.0 - pow(progress, PHI_EXPONENT);
      // Ma visivamente l'occhio umano percepisce la luce in modo logaritmico, quindi dobbiamo compensare.
      
      // Mappiamo il valore della curva (0.0 - 1.0) su 255
      int newBrightness = (int)(curveValue * 255);
      
      // Sicurezza per non andare sotto zero
      if (newBrightness < 0) newBrightness = 0;

      // Applichiamo solo se il valore è cambiato
      if (newBrightness != currentBrightness) {
        currentBrightness = newBrightness;
        ledcWrite(PWM_CHANNEL, currentBrightness);
        // Serial.println(currentBrightness); // Scommenta per vedere i numeri scendere
      }
    }
  }
}
