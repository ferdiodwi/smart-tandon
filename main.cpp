#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Definisi Pin ---
const int PIN_TRIG = 8;
const int PIN_ECHO = 9;
const int PIN_RELAY = 4; // Relay terhubung ke Pompa Air

// --- KONFIGURASI TANDON AIR ---
const int TINGGI_TANDON_CM = 100; // Total tinggi tandon dari dasar ke sensor
const int LEVEL_KRITIS_CM = 20;   // Pompa akan ON jika level air di bawah nilai ini
const int LEVEL_PENUH_CM = 90;    // Pompa akan OFF jika level air di atas nilai ini

// --- Pengaturan Layar OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Variabel Global ---
long durasi;
float jarakKePermukaan;
float tinggiAir;
float levelPersen;
String statusPeringatan = "NORMAL";
bool isPumpOn = false;

// --- Variabel untuk kontrol mode ---
enum ControlMode { AUTO, MANUAL };
ControlMode currentMode = AUTO;
bool manualPumpState = false;

// --- Variabel untuk filtering ---
const int NUM_READINGS = 5;
float readings[NUM_READINGS];
int readIndex = 0;
float total = 0;
bool filterInitialized = false;

// --- Variabel untuk kontrol waktu ---
unsigned long lastSensorRead = 0;
unsigned long lastSerialSend = 0;
unsigned long lastDisplayUpdate = 0;
const unsigned long SENSOR_INTERVAL = 200;   // Baca sensor setiap 200ms
const unsigned long SERIAL_INTERVAL = 1000;  // Kirim data setiap 1 detik
const unsigned long DISPLAY_INTERVAL = 500;  // Update display setiap 500ms

// --- Variabel untuk hysteresis pompa ---
bool pumpOverride = false;
unsigned long pumpChangeTime = 0;
const unsigned long PUMP_MIN_RUNTIME = 10000; // Pompa minimal jalan 10 detik

void setup() {
  Serial.begin(9600);
  
  // Tunggu serial ready
  while (!Serial) {
    delay(10);
  }
  
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);

  // Inisialisasi OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("{\"error\":\"OLED initialization failed\"}"));
    for (;;);
  }
  
  // Inisialisasi pompa dalam keadaan OFF
  isPumpOn = false;
  digitalWrite(PIN_RELAY, HIGH); // HIGH = OFF untuk relay aktif LOW
  
  // Inisialisasi array untuk moving average
  for (int i = 0; i < NUM_READINGS; i++) {
    readings[i] = 0;
  }

  // Tampilan awal
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(18, 20);
  display.println(F("Monitoring System"));
  display.setCursor(38, 35);
  display.println(F("Level Air"));
  display.display();
  
  Serial.println(F("{\"status\":\"System initialized\",\"mode\":\"AUTO\"}"));
  delay(2000);
}

float readUltrasonicDistance() {
  // Pengukuran jarak dengan error handling
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  durasi = pulseIn(PIN_ECHO, HIGH, 30000); // Timeout 30ms
  
  if (durasi == 0) {
    // Timeout terjadi, kembalikan nilai terakhir yang valid
    return jarakKePermukaan;
  }
  
  float jarak = durasi * 0.034 / 2;
  
  // Filter nilai yang tidak masuk akal
  if (jarak < 2.0 || jarak > 400.0) {
    return jarakKePermukaan; // Kembalikan nilai sebelumnya
  }
  
  return jarak;
}

float getFilteredDistance() {
  // Moving average filter untuk stabilitas pembacaan
  float newReading = readUltrasonicDistance();
  
  if (!filterInitialized) {
    // Inisialisasi filter dengan pembacaan pertama
    for (int i = 0; i < NUM_READINGS; i++) {
      readings[i] = newReading;
    }
    total = newReading * NUM_READINGS;
    filterInitialized = true;
    return newReading;
  }
  
  // Hapus pembacaan lama dari total
  total = total - readings[readIndex];
  // Simpan pembacaan baru
  readings[readIndex] = newReading;
  // Tambahkan ke total
  total = total + readings[readIndex];
  // Pindah ke index berikutnya
  readIndex = (readIndex + 1) % NUM_READINGS;
  
  // Hitung rata-rata
  return total / NUM_READINGS;
}

void updatePumpControl() {
  bool shouldPumpRun = false;
  
  if (currentMode == MANUAL) {
    // Mode manual: gunakan status yang ditetapkan manual
    shouldPumpRun = manualPumpState;
    statusPeringatan = manualPumpState ? "MANUAL ON" : "MANUAL OFF";
  } else {
    // Mode otomatis: logika kontrol pompa dengan hysteresis
    if (tinggiAir <= LEVEL_KRITIS_CM) {
      shouldPumpRun = true;
      statusPeringatan = "KRITIS";
    } 
    else if (tinggiAir >= LEVEL_PENUH_CM) {
      shouldPumpRun = false;
      statusPeringatan = "PENUH";
    } 
    else {
      // Di zona tengah, pertahankan status pompa saat ini (hysteresis)
      shouldPumpRun = isPumpOn;
      statusPeringatan = "NORMAL";
    }
    
    // Cek minimum runtime pompa hanya di mode otomatis
    unsigned long currentTime = millis();
    if (isPumpOn && !shouldPumpRun) {
      // Pompa sedang ON tapi seharusnya OFF
      if (currentTime - pumpChangeTime < PUMP_MIN_RUNTIME) {
        // Belum mencapai minimum runtime, tetap ON
        shouldPumpRun = true;
      }
    }
  }
  
  // Update status pompa jika berubah
  if (shouldPumpRun != isPumpOn) {
    isPumpOn = shouldPumpRun;
    digitalWrite(PIN_RELAY, isPumpOn ? LOW : HIGH); // LOW = ON, HIGH = OFF
    pumpChangeTime = millis();
    
    Serial.print(F("{\"pump_change\":\""));
    Serial.print(isPumpOn ? "ON" : "OFF");
    Serial.print(F("\",\"mode\":\""));
    Serial.print(currentMode == AUTO ? "AUTO" : "MANUAL");
    Serial.println(F("\"}"));
  }
}

// --- FIX: Restructured this function for clarity and robustness ---
void processSerialCommand() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("{")) {
      // --- Mode Control ---
      if (command.indexOf("\"mode\":\"AUTO\"") != -1) {
        currentMode = AUTO;
        Serial.println(F("{\"response\":\"Mode changed to AUTO\"}"));
      }
      else if (command.indexOf("\"mode\":\"MANUAL\"") != -1) {
        currentMode = MANUAL;
        // Reset manual pump state when switching to manual mode
        manualPumpState = isPumpOn; 
        Serial.println(F("{\"response\":\"Mode changed to MANUAL\"}"));
      }
      // --- Manual Pump Control ---
      else if (command.indexOf("\"pump_manual\"") != -1) {
        if (currentMode == MANUAL) {
          if (command.indexOf("\"ON\"") != -1) {
            manualPumpState = true;
            Serial.println(F("{\"response\":\"Manual pump set to ON\"}"));
          } else if (command.indexOf("\"OFF\"") != -1) {
            manualPumpState = false;
            Serial.println(F("{\"response\":\"Manual pump set to OFF\"}"));
          }
        } else {
          // Send specific error if trying to control pump in AUTO mode
          Serial.println(F("{\"error\":\"Pump control disabled in AUTO mode\"}"));
        }
      }
      // --- Fallback for unknown commands ---
      else {
        Serial.println(F("{\"error\":\"Unknown JSON command\"}"));
      }
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Mode di baris pertama
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Mode: ");
  display.print(currentMode == AUTO ? "AUTO" : "MANUAL");
  
  // Status di baris kedua
  display.setCursor(0, 10);
  display.print("Status: ");
  display.print(statusPeringatan);
  
  // Level air dalam persen (besar)
  display.setTextSize(2);
  display.setCursor(25, 20);
  if (levelPersen < 10) {
    display.setCursor(35, 20); // Center single digit
  }
  display.print(levelPersen, 0);
  display.print("%");
  
  // Tinggi air dalam cm
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("Tinggi: ");
  display.print(tinggiAir, 1);
  display.print(" cm");
  
  // Status pompa
  display.setCursor(0, 50);
  display.print("Pompa  : ");
  display.print(isPumpOn ? "ON" : "OFF");
  
  display.display();
}

void sendDataToSerial() {
  // Buat JSON string yang valid
  Serial.print(F("{"));
  Serial.print(F("\"water_level_cm\":"));
  Serial.print(tinggiAir, 2);
  Serial.print(F(",\"water_level_percent\":"));
  Serial.print(levelPersen, 2);
  Serial.print(F(",\"pump_status\":\""));
  Serial.print(isPumpOn ? "ON" : "OFF");
  Serial.print(F("\",\"alert_status\":\""));
  Serial.print(statusPeringatan);
  Serial.print(F("\",\"distance_cm\":"));
  Serial.print(jarakKePermukaan, 2);
  Serial.print(F(",\"control_mode\":\""));
  Serial.print(currentMode == AUTO ? "AUTO" : "MANUAL");
  Serial.print(F("\",\"manual_pump_state\":"));
  Serial.print(manualPumpState ? "true" : "false");
  Serial.println(F("}"));
}

void loop() {
  unsigned long currentTime = millis();
  
  // Proses perintah serial
  processSerialCommand();
  
  // Baca sensor ultrasonik
  if (currentTime - lastSensorRead >= SENSOR_INTERVAL) {
    jarakKePermukaan = getFilteredDistance();
    
    // Hitung ketinggian air
    tinggiAir = TINGGI_TANDON_CM - jarakKePermukaan;
    
    // Batasi nilai dalam range yang valid
    if (tinggiAir < 0) tinggiAir = 0;
    if (tinggiAir > TINGGI_TANDON_CM) tinggiAir = TINGGI_TANDON_CM;
    
    // Hitung persentase
    levelPersen = (tinggiAir / TINGGI_TANDON_CM) * 100;
    if (levelPersen < 0) levelPersen = 0;
    if (levelPersen > 100) levelPersen = 100;
    
    // Update kontrol pompa
    updatePumpControl();
    
    lastSensorRead = currentTime;
  }
  
  // Update display
  if (currentTime - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = currentTime;
  }
  
  // Kirim data ke serial
  if (currentTime - lastSerialSend >= SERIAL_INTERVAL) {
    sendDataToSerial();
    lastSerialSend = currentTime;
  }
  
  // Delay kecil untuk stabilitas
  delay(10);
}