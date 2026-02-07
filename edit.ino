#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <math.h>

// --- CREDENZIALI WI-FI (MODIFICA QUI!) ---
const char* ssid = "IL_TUO_NOME_WIFI";
const char* password = "LA_TUA_PASSWORD_WIFI";

// --- CONFIGURAZIONE PIN ---
const int PIN_MOSFET = 16;
const int PIN_TOUCH = 4;

// --- PARAMETRI TRAMONTO ---
// Durata standard se attivato manualmente (es. timer prima di dormire)
unsigned long standardDurationMinutes = 30; 
// Durata "BIOFEEDBACK": se ti addormenti, sfuma in questi minuti
const unsigned long SLEEP_DETECTED_DURATION_MIN = 15; 

// --- SETUP TECNICO ---
RTC_DS1307 rtc; // Usa RTC_DS3231 se hai quello
WebServer server(80); // Il server che ascolta il comando "Dorme"

const int PWM_FREQ = 5000;
const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8;

// Variabili di Stato
int currentBrightness = 0;
bool isLightOn = false;
bool isSunsetMode = false;
unsigned long sunsetStartTime = 0;
unsigned long currentDurationMillis = 0; // Durata variabile (può cambiare se ti addormenti)
int lastTouchState = LOW;

void setup() {
  Serial.begin(115200);

  // 1. Configurazione PWM (Luci)
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // 2. Configurazione Touch
  pinMode(PIN_TOUCH, INPUT);

  // 3. Avvio RTC
  if (!rtc.begin()) {
    Serial.println("ERRORE: RTC non trovato (Funzionerà solo in modalità manuale)");
  }
  rtc.adjust(DateTime(__DATE__, __TIME__)); // Scommenta solo per settare l'ora

  // 4. Connessione Wi-Fi
  Serial.print("Connessione al Wi-Fi");
  WiFi.begin(ssid, password);
  int tentativi = 0;
  while (WiFi.status() != WL_CONNECTED && tentativi < 20) {
    delay(500);
    Serial.print(".");
    tentativi++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connesso!");
    Serial.print("Indirizzo IP Lampada: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi non connesso. La lampada lavorerà offline.");
  }

  // 5. Configurazione Comandi "Biofeedback" (Web Server)
  
  // Pagina base per testare se è viva
  server.on("/", []() {
    server.send(200, "text/plain", "Lampada ESP32 Online. Comandi disponibili: /manual, /sleep");
  });

  // COMANDO MANUALE: Avvia tramonto standard (es. 30 min)
  server.on("/manual", []() {
    if (!isLightOn) { 
      isLightOn = true; 
      currentBrightness = 255; 
    }
    avviaTramonto(standardDurationMinutes);
    server.send(200, "text/plain", "Tramonto Manuale Avviato");
  });

  // COMANDO BIOFEEDBACK: "L'utente si è addormentato!"
  server.on("/sleep", []() {
    Serial.println(">>> SEGNALE BIOFEEDBACK RICEVUTO: UTENTE DORME <<<");
    
    // Se la luce è spenta, non fare nulla (inutile accenderla per spegnerla)
    if (!isLightOn && currentBrightness == 0) {
      server.send(200, "text/plain", "Luce già spenta. Buonanotte.");
      return;
    }
    
    // Logica Intelligente:
    // Ricalibra il tramonto per durare esattamente 15 minuti da ORA.
    // Indipendentemente da quanto mancava prima.
    avviaTramonto(SLEEP_DETECTED_DURATION_MIN);
    
    server.send(200, "text/plain", "Biofeedback Ricevuto. Spegnimento in 15 min.");
  });

  server.begin();
  Serial.println("Server Avviato.");
}

void loop() {
  // --- OROLOGIO PARLANTE (DEBUG) ---
  static unsigned long lastDebugPrint = 0;
  
  // Stampa solo se è passato 1 secondo
  if (millis() - lastDebugPrint > 1000) {
    lastDebugPrint = millis();
    DateTime now = rtc.now();
    
    Serial.print("ORA ATTUALE RTC: ");
    Serial.print(now.hour());
    Serial.print(":");
    if (now.minute() < 10) Serial.print("0"); // Aggiunge lo zero estetico
    Serial.print(now.minute());
    Serial.print(":");
    if (now.second() < 10) Serial.print("0");
    Serial.println(now.second());
  }
  // Gestione richieste Wi-Fi (non bloccante)
  server.handleClient();
  
  // Gestione Touch
  handleTouch();

  // Gestione Tramonto (Logica Matematica)
  if (isSunsetMode) {
    gestisciDissolvenza();
  }
}

// --- FUNZIONI AUSILIARIE ---

void handleTouch() {
  int touchState = digitalRead(PIN_TOUCH);
  if (touchState == HIGH && lastTouchState == LOW) {
    isSunsetMode = false; // Il tocco annulla sempre il tramonto
    isLightOn = !isLightOn;
    currentBrightness = isLightOn ? 255 : 0;
    ledcWrite(PWM_CHANNEL, currentBrightness);
    Serial.println(isLightOn ? "Touch: ON" : "Touch: OFF");
    delay(300); // Debounce
  }
  lastTouchState = touchState;
}

void avviaTramonto(unsigned long minutiDurata) {
  isSunsetMode = true;
  sunsetStartTime = millis();
  currentDurationMillis = minutiDurata * 60 * 1000;
  Serial.print("Inizio Tramonto. Durata prevista: ");
  Serial.print(minutiDurata);
  Serial.println(" minuti.");
}

void gestisciDissolvenza() {
  unsigned long timeElapsed = millis() - sunsetStartTime;

  if (timeElapsed >= currentDurationMillis) {
    // Fine del tempo
    currentBrightness = 0;
    isSunsetMode = false;
    isLightOn = false;
    ledcWrite(PWM_CHANNEL, 0);
    Serial.println("Tramonto completato (Buonanotte).");
  } else {
    // --- MATEMATICA LOGARITMICA (Gamma Correction) ---
    // Questa formula è molto più morbida per il sonno rispetto alla sezione aurea pura.
    // Scende veloce all'inizio (toglie il bagliore) e lentissimo alla fine.
    
    float progress = (float)timeElapsed / currentDurationMillis;
    float linearValue = 1.0 - progress;
    
    // pow(valore, 2.5) crea la curva "pancia in basso" (logaritmica inversa)
    // Più alto è il numero (es. 3.0), più scuro diventa velocemente restando fioco a lungo.
    int newBrightness = (int)(pow(linearValue, 2.5) * 255);

    if (newBrightness < 0) newBrightness = 0;

    if (newBrightness != currentBrightness) {
      currentBrightness = newBrightness;
      ledcWrite(PWM_CHANNEL, currentBrightness);
      // Serial.println(currentBrightness); // Debug
    }
  }
}
