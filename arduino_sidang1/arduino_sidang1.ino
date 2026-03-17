#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SoftwareSerial.h>

// ===== FORWARD DECLARATIONS (AVR needs this) =====
void camBegin();
void camService();
void camEvent(uint8_t pulses);
void waitMs(unsigned long ms);

// ================= VIB LEVEL =================
enum VibLevel { VIB_NONE, VIB_LIGHT, VIB_MED, VIB_STRONG };

// =====================================================
//  ESP32-CAM LINK (GETPIN / SETPIN) - SYNC DB PIN
//  PROTOCOL:
//   UNO -> "GETPIN\n"                      => ESP32 -> "PIN=1234\n"
//   UNO -> "SETPIN,old=1234,new=2580\n"    => ESP32 -> "OK\n" / "ERR\n"
// =====================================================
static const uint8_t ESP_RX_PIN = A1; // UNO RX  <- ESP32 TX0
static const uint8_t ESP_TX_PIN = A2; // UNO TX  -> ESP32 RX0
SoftwareSerial espLink(ESP_RX_PIN, ESP_TX_PIN); // RX, TX
static const unsigned long ESP_IO_TIMEOUT_MS = 2500;

static void espFlushInput() {
  while (espLink.available()) espLink.read();
}

static bool espReadLine(char* out, size_t outSize, unsigned long timeoutMs) {
  if (!out || outSize < 2) return false;
  out[0] = '\0';

  unsigned long t0 = millis();
  size_t len = 0;

  while (millis() - t0 < timeoutMs) {
    while (espLink.available()) {
      char c = (char)espLink.read();
      if (c == '\r') continue;

      if (c == '\n') {
        while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t')) {
          out[len - 1] = '\0';
          len--;
        }
        return (len > 0);
      }

      if (len < outSize - 1) {
        out[len++] = c;
        out[len] = '\0';
      }
    }
    delay(2);
  }

  while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t')) {
    out[len - 1] = '\0';
    len--;
  }
  return (len > 0);
}

static bool espGetPin(char pinOut[5]) {
  espFlushInput();
  espLink.print("GETPIN\n");

  char line[64];
  if (!espReadLine(line, sizeof(line), ESP_IO_TIMEOUT_MS)) return false;

  if (strncmp(line, "PIN=", 4) != 0) return false;

  const char* p = line + 4;
  while (*p == ' ' || *p == '\t') p++;

  for (int i = 0; i < 4; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    pinOut[i] = p[i];
  }
  pinOut[4] = '\0';

  if (p[4] != '\0' && p[4] != ' ' && p[4] != '\t') return false;

  return true;
}

static bool espSetPin(const char oldPin[5], const char newPin[5]) {
  espFlushInput();
  espLink.print("SETPIN,old=");
  espLink.print(oldPin);
  espLink.print(",new=");
  espLink.print(newPin);
  espLink.print("\n");

  char line[32];
  if (!espReadLine(line, sizeof(line), ESP_IO_TIMEOUT_MS)) return false;

  const char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  return (strcmp(p, "OK") == 0);
}

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= Keypad =================
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {2,3,4,5};
byte colPins[COLS] = {6,7,8,9};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= PIN =================
const int PIN_LEN = 4;
char userPIN[5] = "1234";   // fallback awal, akan disync dari DB via ESP32-CAM saat setup

char entered[5] = "";
uint8_t enteredLen = 0;

int failCount = 0;

static inline void clearEntered() { enteredLen = 0; entered[0] = '\0'; }
static inline bool enteredIs4() { return enteredLen == 4; }

static inline void appendDigit(char buf[5], uint8_t &len, char d) {
  if (len < 4) {
    buf[len++] = d;
    buf[len] = '\0';
  }
}

static inline void clearBuf(char buf[5], uint8_t &len) {
  len = 0; buf[0] = '\0';
}

// ================= Relay / Solenoid =================
const int RELAY_PIN = 12;
const bool RELAY_ACTIVE_LOW = true; // relay ON = LOW

void relaySet(bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else                  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

// Lama solenoid ON (singkat biar gak panas)
static const unsigned long SOLENOID_ON_MS = 2000;

// ================= Reed Switch =================
const int REED_PIN = 10;
const bool REED_OPEN_IS_HIGH = true;  // kalau kebalik, ubah false

bool reedRawOpen() {
  int v = digitalRead(REED_PIN);
  return REED_OPEN_IS_HIGH ? (v == HIGH) : (v == LOW);
}

const unsigned long REED_STABLE_MS = 250;

static bool reedStableOpen = false;
static bool reedLastRaw = false;
static unsigned long reedLastChangeMs = 0;

static void reedService() {
  bool rawOpen = reedRawOpen();
  if (rawOpen != reedLastRaw) {
    reedLastRaw = rawOpen;
    reedLastChangeMs = millis();
  }
  if (millis() - reedLastChangeMs >= REED_STABLE_MS) {
    reedStableOpen = reedLastRaw;
  }
}

bool doorOpenStable()   { return reedStableOpen; }
bool doorClosedStable() { return !reedStableOpen; }

bool lastDoorOpen = false;

// ================= VIBRATION (SW-420) =================
const int VIB_PIN = 11;
const bool VIB_ACTIVE_LOW = false; // fallback: aktif HIGH

static bool vibActiveHigh = true;
static unsigned long vibIgnoreUntilMs = 0;

static inline bool vibRawActive() {
  int v = digitalRead(VIB_PIN);
  return vibActiveHigh ? (v == HIGH) : (v == LOW);
}

static const unsigned long VIB_WINDOW_MS      = 1500;
static const unsigned long VIB_STRONG_HOLD_MS = 300;
static const uint8_t       VIB_MED_HITS       = 2;
static const uint8_t       VIB_STRONG_HITS    = 6;
static const unsigned long VIB_MIN_ON_MS      = 25;

static unsigned long vibWinStartMs = 0;
static unsigned long vibOnStartMs  = 0;
static unsigned long vibTotalOnMs  = 0;
static uint8_t vibHits             = 0;
static bool vibPrev                = false;

static const unsigned long vibPhotoCooldownMs  = 1200;
static const unsigned long vibStrongCooldownMs = 2500;
static unsigned long vibLastPhotoMs  = 0;
static unsigned long vibLastStrongMs = 0;

static void vibResetAgg() {
  vibWinStartMs = 0; vibOnStartMs = 0; vibTotalOnMs = 0; vibHits = 0;
}

static VibLevel vibUpdateAndClassify(bool rawActive) {
  unsigned long now = millis();

  if (rawActive && !vibPrev) {
    vibPrev = true;
    vibOnStartMs = now;
    if (vibWinStartMs == 0) vibWinStartMs = now;
    vibHits++;
  }

  if (!rawActive && vibPrev) {
    vibPrev = false;
    if (vibOnStartMs > 0) {
      unsigned long dur = now - vibOnStartMs;
      if (dur >= VIB_MIN_ON_MS) vibTotalOnMs += dur;
    }
    vibOnStartMs = 0;
  }

  if (rawActive && vibOnStartMs > 0) {
    if ((now - vibOnStartMs) >= VIB_STRONG_HOLD_MS) {
      vibResetAgg();
      return VIB_STRONG;
    }
  }

  if (vibWinStartMs > 0 && (now - vibWinStartMs) >= VIB_WINDOW_MS) {
    VibLevel out = VIB_NONE;

    if (vibHits >= VIB_STRONG_HITS || vibTotalOnMs >= 320) out = VIB_STRONG;
    else if (vibHits >= VIB_MED_HITS || vibTotalOnMs >= 140) out = VIB_MED;
    else if (vibHits >= 1 || vibTotalOnMs > 0) out = VIB_LIGHT;

    vibResetAgg();
    return out;
  }

  return VIB_NONE;
}

static void vibAutoCalibrate() {
  unsigned long t0 = millis();
  while (millis() - t0 < 300) delay(1);

  uint16_t highCnt = 0, lowCnt = 0;
  for (uint8_t i = 0; i < 80; i++) {
    int v = digitalRead(VIB_PIN);
    if (v == HIGH) highCnt++; else lowCnt++;
    delay(4);
  }

  bool idleHigh = (highCnt >= lowCnt);
  vibActiveHigh = !idleHigh;

  if (highCnt == lowCnt) {
    vibActiveHigh = (VIB_ACTIVE_LOW ? false : true);
  }

  vibResetAgg();
  vibPrev = false;
  vibOnStartMs = 0;
}

// ================= BUZZER =================
const int BUZ_PIN = 13;

// Beeper non-blocking
struct Beeper {
  int pin;
  const uint16_t* pat = nullptr;
  uint8_t patLen = 0;
  uint8_t idx = 0;
  unsigned long tMark = 0;

  void begin(int p) {
    pin = p;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  void stop() { pat=nullptr; patLen=0; idx=0; digitalWrite(pin, LOW); }

  void play(const uint16_t* pattern, uint8_t length) {
    pat = pattern; patLen = length; idx = 0; tMark = millis();
    digitalWrite(pin, HIGH);
  }

  void update() {
    if (!pat || patLen == 0) return;
    unsigned long now = millis();
    if (now - tMark < pat[idx]) return;

    tMark = now;
    idx++;
    if (idx >= patLen) { stop(); return; }

    bool nextOn = (idx % 2 == 0);
    digitalWrite(pin, nextOn ? HIGH : LOW);
  }
};

Beeper beeper;

const uint16_t PAT_KEY[]   = { 18, 10 };
const uint16_t PAT_OK[]    = { 50, 30, 50, 30 };
const uint16_t PAT_WRONG[] = { 120, 60 };
const uint16_t PAT_WARN[]  = { 80, 60 };
const uint16_t PAT_END[]   = { 160, 70 };

void beepKey()   { beeper.play(PAT_KEY,   (uint8_t)(sizeof(PAT_KEY)/sizeof(PAT_KEY[0]))); }
void beepOk()    { beeper.play(PAT_OK,    (uint8_t)(sizeof(PAT_OK)/sizeof(PAT_OK[0]))); }
void beepWrong() { beeper.play(PAT_WRONG, (uint8_t)(sizeof(PAT_WRONG)/sizeof(PAT_WRONG[0]))); }
void beepWarn()  { beeper.play(PAT_WARN,  (uint8_t)(sizeof(PAT_WARN)/sizeof(PAT_WARN[0]))); }
void beepEnd()   { beeper.play(PAT_END,   (uint8_t)(sizeof(PAT_END)/sizeof(PAT_END[0]))); }

// =====================================================
//  CAMERA TRIGGER (RELAY-ONLY) - FINAL CONSISTENT TIMING
//  MATCH ESP32 FINAL DECODER:
//    ON 120ms, OFF 90ms, GAP end 900ms
// =====================================================
const int CAM_TRIG_PIN = A0; // UNO A0 = D14
const bool CAM_RELAY_ACTIVE_LOW = true;

static inline void camRelaySet(bool on){
  if (CAM_RELAY_ACTIVE_LOW) digitalWrite(CAM_TRIG_PIN, on ? LOW : HIGH);
  else                      digitalWrite(CAM_TRIG_PIN, on ? HIGH : LOW);
}

// ===== FINAL timing =====
static const uint16_t CAM_PRE_IDLE_MS          = 70;   // stabil
static const uint16_t CAM_ON_MS                = 100;  // relay pasti sempat "nempel"
static const uint16_t CAM_OFF_MS               = 90;   // jeda cukup
static const uint16_t CAM_GAP_AFTER_BURST_MS   = 650;  // lebih cepat dari 900 tapi tetap aman
static const uint16_t CAM_EVENT_COOLDOWN_MS    = 120;

static volatile bool camBusy = false;
static unsigned long camLastEventMs = 0;

static uint8_t camRemaining = 0;
static bool camStateOn = false;
static unsigned long camMarkMs = 0;

static bool camGapPhase = false;
static bool camPreIdlePhase = false;

// ===== EVENT QUEUE =====
static const uint8_t CAM_Q_SIZE = 8;
static uint8_t camQ[CAM_Q_SIZE];
static uint8_t camQHead = 0, camQTail = 0, camQCount = 0;

static inline bool isCriticalEvent(uint8_t p) {
  return (p == 5 || p == 10 || p == 11);
}

static void camQClear(){
  camQHead = camQTail = camQCount = 0;
}

static bool camQPush(uint8_t p){
  if (camQCount >= CAM_Q_SIZE) return false;
  camQ[camQTail] = p;
  camQTail = (camQTail + 1) % CAM_Q_SIZE;
  camQCount++;
  return true;
}

static bool camQPop(uint8_t &out){
  if (camQCount == 0) return false;
  out = camQ[camQHead];
  camQHead = (camQHead + 1) % CAM_Q_SIZE;
  camQCount--;
  return true;
}

void camBegin(){
  pinMode(CAM_TRIG_PIN, OUTPUT);
  camRelaySet(false);
  camQClear();
  camBusy = false;
  camGapPhase = false;
  camPreIdlePhase = false;
  camRemaining = 0;
  camStateOn = false;
}

void camService(){
  unsigned long now = millis();

  // gap setelah burst
  if (camBusy && camGapPhase) {
    if (now - camMarkMs >= CAM_GAP_AFTER_BURST_MS) {
      camGapPhase = false;
      camBusy = false;
      camRelaySet(false);
    }
    return;
  }

  // ambil event baru dari queue
  if (!camBusy && camQCount > 0) {
    uint8_t pulses;
    if (camQPop(pulses)) {
      camBusy = true;
      camRemaining = pulses;

      camRelaySet(false);
      camStateOn = false;
      camPreIdlePhase = true;
      camGapPhase = false;
      camMarkMs = now;
    }
    return;
  }

  if (!camBusy) return;

  // pre idle
  if (camPreIdlePhase) {
    if (now - camMarkMs >= CAM_PRE_IDLE_MS) {
      camPreIdlePhase = false;
      camRelaySet(true);
      camStateOn = true;
      camMarkMs = now;
    }
    return;
  }

  // burst
  if (camStateOn) {
    if (now - camMarkMs >= CAM_ON_MS) {
      camRelaySet(false);
      camStateOn = false;
      camMarkMs = now;

      if (camRemaining > 0) camRemaining--;
      if (camRemaining == 0) {
        camGapPhase = true;
        camMarkMs = now;
      }
    }
  } else {
    if (camRemaining > 0 && (now - camMarkMs >= CAM_OFF_MS)) {
      camRelaySet(true);
      camStateOn = true;
      camMarkMs = now;
    }
  }
}

void camEvent(uint8_t pulses){
  unsigned long now = millis();

  // cooldown anti-double
  if (now - camLastEventMs < CAM_EVENT_COOLDOWN_MS) {
    if (!isCriticalEvent(pulses)) return;
  }
  camLastEventMs = now;

  // queue penuh: event kritis replace tail, non-kritis drop
  if (camQCount >= CAM_Q_SIZE) {
    if (isCriticalEvent(pulses)) {
      camQTail = (camQTail == 0) ? (CAM_Q_SIZE - 1) : (camQTail - 1);
      camQCount--;
      camQPush(pulses);
    }
    return;
  }

  camQPush(pulses);
}

// delay pengganti: beeper + camService tetap jalan
void waitMs(unsigned long ms){
  unsigned long start = millis();
  while (millis() - start < ms) {
    beeper.update();
    camService();
    delay(1);
  }
}

// ================= STATE =================
enum SafeState { STATE_LOCKED, STATE_UNLOCKED, STATE_ALARM };
SafeState state = STATE_LOCKED;

bool waitingDoorCloseToLock = false;
bool doorOpenedSinceUnlock = false;

unsigned long ignoreReedUntil = 0;
unsigned long lastKeyMs = 0;
const unsigned long FORCED_OPEN_GRACE_AFTER_KEY_MS    = 1500;
const unsigned long FORCED_OPEN_GRACE_AFTER_UNLOCK_MS = 6000;

bool forcedOpenLatched = false;

// ================= MOVE MODE =================
bool moveMode = false;
unsigned long moveModeUntil = 0;
const unsigned long MOVE_MODE_MS = 60000;

bool moveArming = false;
char movePin[5] = "";
uint8_t movePinLen = 0;
unsigned long moveArmingLastInputMs = 0;
const unsigned long MOVE_ARM_TIMEOUT_MS = 15000;

// ================= LCD Helpers =================
char lastLine0[17] = "";
char lastLine1[17] = "";
unsigned long lastLcdDrawMs = 0;
const unsigned long LCD_REFRESH_MS = 120;

static void lcdResetCache(){
  lastLine0[0] = '\0';
  lastLine1[0] = '\0';
}

static void padTo16(char out16[17], const char* src) {
  uint8_t i = 0;
  if (src) {
    while (i < 16 && src[i] != '\0') { out16[i] = src[i]; i++; }
  }
  while (i < 16) out16[i++] = ' ';
  out16[16] = '\0';
}

static void lcdDraw2Lines(const char* l0, const char* l1, bool force=false){
  unsigned long now = millis();
  if (!force && now - lastLcdDrawMs < LCD_REFRESH_MS) return;
  lastLcdDrawMs = now;

  char b0[17], b1[17];
  padTo16(b0, l0);
  padTo16(b1, l1);

  if (force || strcmp(b0, lastLine0) != 0) {
    lcd.setCursor(0,0);
    lcd.print(b0);
    strcpy(lastLine0, b0);
  }
  if (force || strcmp(b1, lastLine1) != 0) {
    lcd.setCursor(0,1);
    lcd.print(b1);
    strcpy(lastLine1, b1);
  }
}

static void makeStars(char out16[17], const char* prefix, uint8_t count){
  char tmp[32];
  uint8_t i = 0;

  if (prefix) {
    while (prefix[i] && i < sizeof(tmp)-1) {
      tmp[i] = prefix[i];
      i++;
    }
  }

  while (count > 0 && i < sizeof(tmp)-1) {
    tmp[i++] = '*';
    count--;
  }
  tmp[i] = '\0';

  padTo16(out16, tmp);
}

static void showMainScreen() {
  char l0[17];
  char l1[17];

  if (moveMode) {
    long remMs = (long)(moveModeUntil - millis());
    if (remMs < 0) remMs = 0;
    int totalSec = (int)((remMs + 999) / 1000);
    int mm = totalSec / 60;
    int ss = totalSec % 60;

    char buf[17];
    snprintf(buf, sizeof(buf), "MOVE %02d:%02d", mm, ss);
    padTo16(l0, buf);
  } else {
    padTo16(l0, doorOpenStable() ? "PINTU: TERBUKA" : "PINTU: TERTUTUP");
  }

  if (moveArming) {
    makeStars(l1, "MOVE PIN: ", movePinLen);
  } else if (moveMode) {
    makeStars(l1, "STOP PIN: ", enteredLen);
  } else {
    makeStars(l1, "PIN: ", enteredLen);
  }

  lcdDraw2Lines(l0, l1);
}

// ================= Lock / Unlock =================
void lockSafe() {
  relaySet(false);
  state = STATE_LOCKED;

  clearEntered();
  failCount = 0;
  waitingDoorCloseToLock = false;
  doorOpenedSinceUnlock = false;

  ignoreReedUntil = millis() + 800;
  lcd.clear(); lcdResetCache();
}

void unlockSafe() {
  relaySet(true);
  waitMs(SOLENOID_ON_MS);
  relaySet(false);

  state = STATE_UNLOCKED;
  waitingDoorCloseToLock = true;
  doorOpenedSinceUnlock = false;
  ignoreReedUntil = millis() + FORCED_OPEN_GRACE_AFTER_UNLOCK_MS;

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("PIN BENAR", "UNLOCK (BUKA)", true);
}

// ================= ALARM =================
static unsigned long keyClickOffUntil = 0;
static unsigned long keyClickOnUntil  = 0;
const unsigned long KEY_CLICK_OFF_MS = 18;
const unsigned long KEY_CLICK_ON_MS  = 45;

void alarmKeyClick() {
  unsigned long now = millis();
  keyClickOffUntil = now + KEY_CLICK_OFF_MS;
  keyClickOnUntil  = keyClickOffUntil + KEY_CLICK_ON_MS;

  digitalWrite(BUZ_PIN, LOW);
  delay(25);
  digitalWrite(BUZ_PIN, HIGH);
  delay(15);
}

void alarmLoop(const char* reason) {
  state = STATE_ALARM;
  beeper.stop();
  clearEntered();
  bool stopMode = false;

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("!!! ALARM !!!", reason ? reason : "", true);

  const unsigned long stepDur[4] = {360, 160, 360, 520};
  const bool stepOn[4] = {true, false, true, false};
  int step = 0;
  unsigned long stepStart = millis();

  while (state == STATE_ALARM) {
    unsigned long now = millis();
    camService();

    if (now - stepStart >= stepDur[step]) { step = (step + 1) % 4; stepStart = now; }

    bool sirenOn = stepOn[step];
    if (now < keyClickOffUntil) sirenOn = false;
    else if (now < keyClickOnUntil) sirenOn = true;

    digitalWrite(BUZ_PIN, sirenOn ? HIGH : LOW);

    char k = keypad.getKey();
    if (!k) continue;

    alarmKeyClick();

    if (k == 'D') {
      stopMode = true;
      clearEntered();
      lcd.clear(); lcdResetCache();
      lcdDraw2Lines("STOP ALARM (D)", "PIN:", true);
      continue;
    }

    if (!stopMode) { if (k == '*') clearEntered(); continue; }

    if (k == '*') {
      clearEntered();
      lcdDraw2Lines("STOP ALARM (D)", "PIN:", true);
      continue;
    }

    if (k >= '0' && k <= '9') {
      appendDigit(entered, enteredLen, k);
      char l1[17];
      makeStars(l1, "PIN: ", enteredLen);
      lcdDraw2Lines("STOP ALARM (D)", l1, true);
      continue;
    }

    if (k == '#') {
      if (!enteredIs4()) {
        lcdDraw2Lines("PIN HARUS 4 DIG", "Coba lagi...", true);
        waitMs(450);
        clearEntered();
        lcdDraw2Lines("STOP ALARM (D)", "PIN:", true);
        continue;
      }

      if (strcmp(entered, userPIN) == 0) {
        digitalWrite(BUZ_PIN, LOW);
        beepOk();
        lcdDraw2Lines("ALARM STOP", "PIN BENAR", true);
        waitMs(550);
        lockSafe();
        return;
      } else {
        beepWrong();
        clearEntered();
        lcdDraw2Lines("PIN SALAH", "Coba lagi...", true);
        waitMs(450);
        lcdDraw2Lines("STOP ALARM (D)", "PIN:", true);
      }
    }
  }
}

// ================= MOVE MODE =================
void stopMoveModeNow(const char* msgLine0 = "MOVE MODE OFF") {
  moveMode = false;
  clearEntered();

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines(msgLine0, "", true);

  camEvent(8); // MOVE_MODE_OFF
  beepEnd();
  waitMs(320);

  lcd.clear(); lcdResetCache();
}

void startMoveMode() {
  moveMode = true;
  moveModeUntil = millis() + MOVE_MODE_MS;
  clearEntered();

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("MOVE MODE ON", "60 detik", true);

  camEvent(7); // MOVE_MODE_ON
  beepOk();
  waitMs(380);

  lcd.clear(); lcdResetCache();
}

void updateMoveMode() {
  if (!moveMode) return;
  if (millis() > moveModeUntil) stopMoveModeNow("MOVE MODE HABIS");
}

void cancelMoveArming() {
  moveArming = false;
  clearBuf(movePin, movePinLen);
}

// =====================================================
//  PIN CHANGE MODE (A) - SYNC UPDATE DB VIA ESP32-CAM
// =====================================================
bool pinChanging = false;
uint8_t pinChangeStep = 0; // 0=OLD, 1=NEW
char pinOld[5] = "";
char pinNew[5] = "";
uint8_t pinOldLen = 0;
uint8_t pinNewLen = 0;

void pinChangeStart() {
  pinChanging = true;
  pinChangeStep = 0;
  clearBuf(pinOld, pinOldLen);
  clearBuf(pinNew, pinNewLen);
  clearEntered();

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("UBAH PIN (A)", "PIN LAMA:", true);
}

void pinChangeCancel(const char* msg="BATAL") {
  pinChanging = false;
  pinChangeStep = 0;
  clearBuf(pinOld, pinOldLen);
  clearBuf(pinNew, pinNewLen);
  clearEntered();

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("UBAH PIN", msg, true);
  waitMs(450);

  lcd.clear(); lcdResetCache();
}

void pinChangeDraw() {
  if (!pinChanging) return;
  char l1[17];
  if (pinChangeStep == 0) {
    makeStars(l1, "LAMA: ", pinOldLen);
    lcdDraw2Lines("UBAH PIN (A)", l1, true);
  } else {
    makeStars(l1, "BARU: ", pinNewLen);
    lcdDraw2Lines("UBAH PIN (A)", l1, true);
  }
}

// =====================================================
//  SYNC PIN FROM DB (B) - GETPIN VIA ESP32-CAM
// =====================================================
static unsigned long lastSyncPinMs = 0;
static const unsigned long SYNC_PIN_COOLDOWN_MS = 2000;

static void syncPinFromDbUI() {
  unsigned long now = millis();
  if (now - lastSyncPinMs < SYNC_PIN_COOLDOWN_MS) return;
  lastSyncPinMs = now;

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("SYNC PIN DB...", "Tunggu", true);

  char dbPin[5];
  if (espGetPin(dbPin)) {
    strcpy(userPIN, dbPin);
    clearEntered();
    failCount = 0;

    beepOk();
    lcd.clear(); lcdResetCache();
    lcdDraw2Lines("SYNC OK", "PIN DIPERBARUI", true);
    waitMs(900);
    lcd.clear(); lcdResetCache();
  } else {
    beepWrong();
    lcd.clear(); lcdResetCache();
    lcdDraw2Lines("SYNC GAGAL", "CEK ESP/WIFI", true);
    waitMs(900);
    lcd.clear(); lcdResetCache();
  }
}

// ================= Setup =================
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  relaySet(false);

  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(VIB_PIN, INPUT_PULLUP);

  beeper.begin(BUZ_PIN);
  camBegin();

  lcd.init();
  lcd.backlight();

  keypad.setDebounceTime(30);
  keypad.setHoldTime(500);

  // ESP32-CAM link
  espLink.begin(115200);

  // init reed
  reedLastRaw = reedRawOpen();
  reedStableOpen = reedLastRaw;
  reedLastChangeMs = millis();
  for (uint8_t i=0; i<5; i++) { reedService(); delay(10); }
  lastDoorOpen = doorOpenStable();

  // init vib
  vibIgnoreUntilMs = millis() + 3000;
  vibResetAgg();
  vibPrev = false;
  vibOnStartMs = 0;
  vibAutoCalibrate();

  lcd.clear(); lcdResetCache();
  lcdDraw2Lines("BRANKAS START", "SYNC DB PIN...", true);

  // sync PIN db
  char dbPin[5];
  bool ok = false;

  waitMs(800);

  for (uint8_t i = 0; i < 8; i++) {
    if (espGetPin(dbPin)) { ok = true; break; }
    waitMs(700);
  }

  if (ok) {
    strcpy(userPIN, dbPin);
    lcd.clear(); lcdResetCache();
    lcdDraw2Lines("SYNC OK", "PIN TERBARU", true);
    beepOk();
    waitMs(650);
  } else {
    lcd.clear(); lcdResetCache();
    lcdDraw2Lines("SYNC GAGAL", "PAKAI DEFAULT", true);
    beepWarn();
    waitMs(650);
  }
}

// ================= Loop =================
void loop() {
  beeper.update();
  updateMoveMode();
  camService();

  // reed service
  reedService();

  // event pintu tertutup
  bool nowOpen = doorOpenStable();
  if (nowOpen != lastDoorOpen) {
    lastDoorOpen = nowOpen;
    if (!nowOpen) camEvent(6);
  }

  // pembukaan paksa
  if (state == STATE_LOCKED) {
    bool forcedOpen =
      (enteredLen == 0) &&
      (millis() > ignoreReedUntil) &&
      (millis() - lastKeyMs > FORCED_OPEN_GRACE_AFTER_KEY_MS) &&
      doorOpenStable();

    if (forcedOpen && !forcedOpenLatched) {
      forcedOpenLatched = true;
      camEvent(11);
      waitMs(120);
      alarmLoop("PAKSA");
      return;
    }
    if (!doorOpenStable()) forcedOpenLatched = false;
  }

  // auto-lock
  if (state == STATE_UNLOCKED && waitingDoorCloseToLock) {
    if (doorOpenStable()) doorOpenedSinceUnlock = true;

    if (doorOpenedSinceUnlock && doorClosedStable()) {
      lcd.clear(); lcdResetCache();
      lcdDraw2Lines("PINTU TERTUTUP", "AUTO LOCK...", true);
      waitMs(420);
      lockSafe();
      return;
    }
  }

  // vibration
  if (!moveMode && state != STATE_ALARM) {
    unsigned long now = millis();

    if (now < vibIgnoreUntilMs) {
      vibResetAgg();
      vibPrev = false;
    } else {
      VibLevel lvl = vibUpdateAndClassify(vibRawActive());

      if (lvl != VIB_NONE) {
        if (state == STATE_LOCKED) {
          if (lvl == VIB_STRONG) {
            if (now - vibLastStrongMs >= vibStrongCooldownMs) {
              vibLastStrongMs = now;
              camEvent(10);
              waitMs(120);
              alarmLoop("GETAR KUAT");
              return;
            }
          } else {
            if (now - vibLastPhotoMs >= vibPhotoCooldownMs) {
              vibLastPhotoMs = now;
              camEvent(9);
              beepWarn();
            }
          }
        } else {
          if (now - vibLastPhotoMs >= vibPhotoCooldownMs) {
            vibLastPhotoMs = now;
            camEvent(9);
          }
        }
      }
    }
  }

  // move arming timeout
  if (moveArming && (millis() - moveArmingLastInputMs > MOVE_ARM_TIMEOUT_MS)) {
    cancelMoveArming();
    lcd.clear(); lcdResetCache();
  }

  // UI
  if (!pinChanging) showMainScreen();
  else pinChangeDraw();

  // keypad
  char k = keypad.getKey();
  if (!k) return;

  lastKeyMs = millis();
  ignoreReedUntil = millis() + 300;
  beepKey();

  // sync pin db
  if (k == 'B' && state != STATE_ALARM && !pinChanging && !moveArming) {
    syncPinFromDbUI();
    return;
  }

  // pin change mode
  if (k == 'A' && !pinChanging && state != STATE_ALARM && !moveArming) {
    pinChangeStart();
    return;
  }

  // pin changing flow
  if (pinChanging) {
    if (k == 'C' || k == 'D') { pinChangeCancel("BATAL (C/D)"); return; }

    if (k == '*') {
      if (pinChangeStep == 0) clearBuf(pinOld, pinOldLen);
      else                    clearBuf(pinNew, pinNewLen);
      pinChangeDraw();
      return;
    }

    if (k >= '0' && k <= '9') {
      if (pinChangeStep == 0) appendDigit(pinOld, pinOldLen, k);
      else                    appendDigit(pinNew, pinNewLen, k);
      pinChangeDraw();
      return;
    }

    if (k == '#') {
      if (pinChangeStep == 0) {
        if (pinOldLen != PIN_LEN) {
          beepWrong();
          lcdDraw2Lines("PIN LAMA 4 DIG", "Coba lagi...", true);
          waitMs(520);
          clearBuf(pinOld, pinOldLen);
          lcdDraw2Lines("UBAH PIN (A)", "PIN LAMA:", true);
          return;
        }
        if (strcmp(pinOld, userPIN) != 0) {
          beepWrong();
          lcdDraw2Lines("PIN LAMA SALAH", "", true);
          waitMs(650);
          clearBuf(pinOld, pinOldLen);
          lcdDraw2Lines("UBAH PIN (A)", "PIN LAMA:", true);
          return;
        }
        pinChangeStep = 1;
        lcd.clear(); lcdResetCache();
        lcdDraw2Lines("UBAH PIN (A)", "PIN BARU:", true);
        return;
      }

      if (pinNewLen != PIN_LEN) {
        beepWrong();
        lcdDraw2Lines("PIN BARU 4 DIG", "Coba lagi...", true);
        waitMs(520);
        clearBuf(pinNew, pinNewLen);
        lcdDraw2Lines("UBAH PIN (A)", "PIN BARU:", true);
        return;
      }

      lcdDraw2Lines("UPDATE DB...", "Tunggu", true);
      bool ok = espSetPin(pinOld, pinNew);

      if (ok) {
        strcpy(userPIN, pinNew);
        beepOk();
        lcdDraw2Lines("PIN DIUBAH", "BERHASIL", true);
        waitMs(800);
        pinChangeCancel("SELESAI");
        return;
      } else {
        beepWrong();
        lcdDraw2Lines("UPDATE GAGAL", "CEK ESP/WIFI", true);
        waitMs(900);
        pinChangeCancel("GAGAL");
        return;
      }
    }
    return;
  }

  // start move mode: D + PIN + #
  if (k == 'D' && !moveArming && state != STATE_ALARM) {
    moveArming = true;
    clearBuf(movePin, movePinLen);
    moveArmingLastInputMs = millis();
    clearEntered();
    lcd.clear(); lcdResetCache();
    showMainScreen();
    return;
  }

  if (moveArming) {
    moveArmingLastInputMs = millis();

    if (k == '*') { clearBuf(movePin, movePinLen); showMainScreen(); return; }

    if (k >= '0' && k <= '9') {
      appendDigit(movePin, movePinLen, k);
      showMainScreen();
      return;
    }

    if (k == '#') {
      if (movePinLen != PIN_LEN) {
        beepWrong();
        lcd.clear(); lcdResetCache();
        lcdDraw2Lines("MOVE PIN 4 DIG", "Coba lagi...", true);
        waitMs(520);
        clearBuf(movePin, movePinLen);
        lcd.clear(); lcdResetCache();
        showMainScreen();
        return;
      }

      if (strcmp(movePin, userPIN) == 0) {
        beepOk();
        moveArming = false;
        startMoveMode();
        return;
      } else {
        beepWrong();
        lcd.clear(); lcdResetCache();
        lcdDraw2Lines("MOVE PIN SALAH", "", true);
        waitMs(520);
        clearBuf(movePin, movePinLen);
        lcd.clear(); lcdResetCache();
        showMainScreen();
        return;
      }
    }
    return;
  }

  // normal pin buffer / stop move mode
  if (k == '*') { clearEntered(); showMainScreen(); return; }

  if (k >= '0' && k <= '9') {
    appendDigit(entered, enteredLen, k);
    showMainScreen();
    return;
  }

  if (k == '#') {
    if (moveMode) {
      if (!enteredIs4()) {
        beepWrong();
        lcd.clear(); lcdResetCache();
        lcdDraw2Lines("STOP PIN 4 DIG", "", true);
        waitMs(520);
        clearEntered();
        lcd.clear(); lcdResetCache();
        return;
      }
      if (strcmp(entered, userPIN) == 0) {
        stopMoveModeNow("MOVE MODE STOP");
        return;
      } else {
        beepWrong();
        clearEntered();
        lcd.clear(); lcdResetCache();
        lcdDraw2Lines("STOP PIN SALAH", "", true);
        waitMs(520);
        lcd.clear(); lcdResetCache();
        return;
      }
    }

    if (!enteredIs4()) {
      lcd.clear(); lcdResetCache();
      lcdDraw2Lines("PIN harus 4", "digit", true);
      waitMs(650);
      clearEntered();
      lcd.clear(); lcdResetCache();
      return;
    }

    if (strcmp(entered, userPIN) == 0) {
      failCount = 0;
      camEvent(2); // PIN_BENAR
      unlockSafe();

      lcd.clear(); lcdResetCache();
      lcdDraw2Lines("PIN BENAR", "BRANKAS UNLOCK", true);
      beepOk();

      clearEntered();
      waitMs(420);
      lcd.clear(); lcdResetCache();
      return;
    } else {
      failCount++;

      if (failCount == 1) camEvent(3);
      else if (failCount == 2) camEvent(4);
      else {
        camEvent(5);
        waitMs(120);
        alarmLoop("PIN 3x SALAH");
        return;
      }

      char line1[17];
      char buf[17];
      snprintf(buf, sizeof(buf), "Count: %d", failCount);
      padTo16(line1, buf);

      lcd.clear(); lcdResetCache();
      lcdDraw2Lines("PIN SALAH", line1, true);
      beepWrong();
      waitMs(520);
      clearEntered();
      lcd.clear(); lcdResetCache();
      return;
    }
  }
}
