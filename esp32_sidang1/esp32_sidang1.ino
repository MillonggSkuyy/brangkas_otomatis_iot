#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <time.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============================================================
// 1) CONFIG
// ============================================================

// ===== PIN CACHE (NVS) =====
Preferences prefs;
static char cachedPin[5] = "1234";

static bool is4DigitPin(const String& s){
  if (s.length() != 4) return false;
  for (int i=0;i<4;i++) if (!isDigit(s[i])) return false;
  return true;
}

static void loadPinCache(){
  prefs.begin("brankas", false);
  String p = prefs.getString("pin", "1234");
  if (!is4DigitPin(p)) p = "2580";
  cachedPin[0]=p[0]; cachedPin[1]=p[1]; cachedPin[2]=p[2]; cachedPin[3]=p[3]; cachedPin[4]='\0';
}

static void savePinCache(const String& p){
  if (!is4DigitPin(p)) return;
  cachedPin[0]=p[0]; cachedPin[1]=p[1]; cachedPin[2]=p[2]; cachedPin[3]=p[3]; cachedPin[4]='\0';
  prefs.putString("pin", p);
}

// ========= WIFI =========
static const char* WIFI_SSID     = "haruru";
static const char* WIFI_PASSWORD = "widd1104";

// ========= TELEGRAM =========
static const char* BOT_TOKEN = "8220630899:AAGiyIMb83mmGMM1edeLHf89mKBhCazeNmk";
static const char* CHAT_ID   = "935724657";

// ========= WEBSITE =========
static const char* WEB_HOST = "webmonitoringbrangkas.dipalji.com";
static const int   WEB_PORT = 443;
static const char* WEB_API_PATH = "/api/akses_brangkas.php";

// ========= WEB AUTH =========
static const char* WEB_API_FIELD = "api_key";
static const char* WEB_API_KEY   = "MYBRANKAS_KEY_12345";

// ========= PIN API =========
static const char* WEB_GET_PIN_PATH    = "/api/get_pin.php";
static const char* WEB_UPDATE_PIN_PATH = "/api/update_pin.php";

// ========= TIMEZONE (WITA GMT+8) =========
static const char* NTP_SERVER = "pool.ntp.org";
static const long  GMT_OFFSET_SEC = 8 * 3600;
static const int   DAYLIGHT_OFFSET_SEC = 0;

static String lastEventKey = "NONE";
static uint32_t lastEventMs = 0;

static long tg_last_update_id = 0;
static uint32_t tg_last_poll_ms = 0;

// ========= TRIGGER INPUT =========
#define TRIG_PIN 13
static const bool TRIG_ACTIVE_LOW = true;  // relay ON -> LOW

// ========= AI Thinker pins =========
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define FLASH_GPIO_NUM     4

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ========= CAMERA SETTINGS =========
static framesize_t CAM_FRAMESIZE = FRAMESIZE_QVGA;
static int CAM_JPEG_QUALITY = 12;

// ========= VIDEO SETTINGS =========
static const uint32_t MAX_AVI_BYTES = 3 * 1024 * 1024;
static const int FPS = 3;
static const int SECONDS = 5;
static const int MAX_FRAMES = FPS * SECONDS;

// ============================================================
// 2) PULSE DECODER (RELAY FRIENDLY + BOOT GUARD)
//  - Hitung pulse hanya jika durasi ON valid (buang bounce relay)
//  - Burst selesai jika idle > BURST_GAP_US
//  - BOOT_IGNORE + ARM jika TRIG idle stabil (anti event salah saat boot)
// ============================================================

// --- decoder params (cocok Arduino: ON 100ms / OFF 90ms / GAP 650ms)
static const uint32_t DEBOUNCE_US        = 50000;   // 50ms (dari 30ms)
static const uint32_t MIN_ON_US          = 80000;   // 80ms (dari 55ms)
static const uint32_t MAX_ON_US          = 180000;  // 180ms (boleh tetap 200ms)
static const uint32_t BURST_GAP_US       = 500000;  // 500ms (dari 450ms)
static const uint32_t MAX_BURST_TIME_US  = 6000000; // 6s

static inline bool trigIsActiveLevel(int v) {
  return TRIG_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

static int stableReadTrig() {
  int a = digitalRead(TRIG_PIN);
  delayMicroseconds(200);
  int b = digitalRead(TRIG_PIN);
  return (a == b) ? a : b;
}

// decoder state
static uint16_t pulseCount = 0;
static uint32_t lastEdgeUs = 0;
static uint32_t burstStartUs = 0;

static int lastStable = HIGH;
static uint32_t lastChangeUs = 0;

static bool inActive = false;
static uint32_t activeStartUs = 0;

// ===== BOOT GUARD =====
static const uint32_t BOOT_IGNORE_MS = 1500;     // abaikan 3.5 detik awal setelah boot
static bool decoderArmed = false;
static uint32_t bootAtMs = 0;
static uint32_t idleStableStartUs = 0;
static const uint32_t IDLE_STABLE_US = 900000;  // harus idle stabil 0.9 detik

static void resetDecoderState(uint32_t nowUs, int stableNow) {
  pulseCount = 0;
  lastEdgeUs = 0;
  burstStartUs = 0;
  inActive = false;
  activeStartUs = 0;
  lastStable = stableNow;
  lastChangeUs = nowUs;
}

// ============================================================
// 3) EVENT MAPPING FINAL (2..11)
// ============================================================
static bool isValidPulse(int p) {
  return (p >= 2 && p <= 11);
}

static String eventKeyFromPulse(int p) {
  switch (p) {
    case 2:  return "PIN_BENAR";
    case 3:  return "PIN_SALAH_1";
    case 4:  return "PIN_SALAH_2";
    case 5:  return "PIN_SALAH_3";
    case 6:  return "PINTU_TERTUTUP";
    case 7:  return "MOVE_MODE_ON";
    case 8:  return "MOVE_MODE_OFF";
    case 9:  return "GETARAN_RINGAN";
    case 10: return "GETARAN_KUAT";
    case 11: return "PEMBUKAAN_PAKSA";
    default: return "UNKNOWN";
  }
}

static bool needVideo(const String& key) {
  return (key == "PIN_SALAH_3" || key == "GETARAN_KUAT" || key == "PEMBUKAAN_PAKSA");
}

static bool isSuspicious(const String& key) {
  return (key == "PIN_SALAH_1" || key == "PIN_SALAH_2" || key == "PIN_SALAH_3" ||
          key == "GETARAN_RINGAN" || key == "GETARAN_KUAT" || key == "PEMBUKAAN_PAKSA");
}

static String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Waktu tidak tersedia";
  char buf[20];
  strftime(buf, sizeof(buf), "%d-%m-%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

static String makeStatusText() {
  String s;
  s += "📶 WiFi: ";
  s += (WiFi.status() == WL_CONNECTED) ? "CONNECTED\n" : "DISCONNECTED\n";
  s += "📷 Camera: READY (cek init log)\n";
  s += "🔐 PIN cache: ";
  s += String(cachedPin) + "\n";
  s += "⏱️ Uptime: ";
  s += String(millis() / 1000) + " detik\n";
  s += "🧾 Last event: ";
  s += lastEventKey;
  if (lastEventMs > 0) s += " (" + String((millis() - lastEventMs) / 1000) + " dtk lalu)";
  s += "\n";
  s += "🧷 Decoder: ";
  s += decoderArmed ? "ARMED\n" : "WAITING\n";
  return s;
}

static String makeCaption(const String& key) {
  String cap;
  cap += isSuspicious(key) ? "⚠️ WASPADA! " : "ℹ️ INFO: ";

  if      (key == "PIN_BENAR")         cap += "PIN benar. Brankas UNLOCK (akses sah).";
  else if (key == "PIN_SALAH_1")       cap += "Percobaan PIN salah (ke-1).";
  else if (key == "PIN_SALAH_2")       cap += "Percobaan PIN salah (ke-2).";
  else if (key == "PIN_SALAH_3")       cap += "ALARM! PIN salah 3 kali.";
  else if (key == "PINTU_TERTUTUP")    cap += "Pintu brankas tertutup.";
  else if (key == "MOVE_MODE_ON")      cap += "Move Mode AKTIF (brankas dipindahkan).";
  else if (key == "MOVE_MODE_OFF")     cap += "Move Mode NONAKTIF.";
  else if (key == "GETARAN_RINGAN")    cap += "Terdeteksi getaran (foto saja).";
  else if (key == "GETARAN_KUAT")      cap += "Terdeteksi GETARAN KUAT (tamper/pencurian)!";
  else if (key == "PEMBUKAAN_PAKSA")   cap += "ALARM! Brankas dibuka secara paksa.";
  else                                 cap += "Aktivitas tidak dikenal.";

  cap += "\n\n📍Brankas Pintar\n";
  cap += "⏰ " + getTimeString() + " WITA";
  return cap;
}

// ============================================================
// 4) TELEGRAM HELPERS
// ============================================================
static String readAllTLS(WiFiClientSecure &client, uint32_t timeoutMs = 20000) {
  String resp;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (client.available()) resp += (char)client.read();
    if (!client.connected()) break;
    delay(5);
  }
  return resp;
}

static bool telegramSendMultipart(
  const char* apiMethod,
  const uint8_t* data, uint32_t len,
  const char* fieldName,
  const char* filename,
  const char* contentType,
  const String& captionText
) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(90);

  const char* host = "api.telegram.org";
  String url = "/bot" + String(BOT_TOKEN) + "/" + String(apiMethod);
  String boundary = "----ESP32BoundaryTG";

  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(CHAT_ID) + "\r\n";

  if (captionText.length() > 0) {
    head +=
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
      captionText + "\r\n";
  }

  head +=
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"" + String(fieldName) + "\"; filename=\"" + String(filename) + "\"\r\n"
    "Content-Type: " + String(contentType) + "\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  uint32_t totalLen = head.length() + len + tail.length();

  if (!client.connect(host, 443)) return false;

  client.print(String("POST ") + url + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(totalLen) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  client.print(head);

  const size_t chunk = 2048;
  for (size_t i = 0; i < len; i += chunk) {
    size_t n = (len - i >= chunk) ? chunk : (len - i);
    if (client.write(data + i, n) != n) { client.stop(); return false; }
    delay(0);
  }

  client.print(tail);

  String resp = readAllTLS(client, 12000);
  client.stop();
  return resp.indexOf("\"ok\":true") >= 0;
}

static bool telegramSendPhoto(const uint8_t* data, uint32_t len, const String& caption) {
  return telegramSendMultipart("sendPhoto", data, len, "photo", "photo.jpg", "image/jpeg", caption);
}

static bool telegramSendAviAsDocument(const uint8_t* data, uint32_t len, const String& caption) {
  return telegramSendMultipart("sendDocument", data, len, "document", "video_5s.avi", "video/x-msvideo", caption);
}

static bool telegramSendMessage(const String& text) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20);

  const char* host = "api.telegram.org";
  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage";
  String postData = "chat_id=" + String(CHAT_ID) + "&text=" + text;

  if (!client.connect(host, 443)) return false;

  client.print(String("POST ") + url + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Length: " + String(postData.length()) + "\r\n\r\n");
  client.print(postData);

  String resp = readAllTLS(client, 12000);
  client.stop();
  return resp.indexOf("\"ok\":true") >= 0;
}

static String telegramGetUpdates(long offset) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4);

  const char* host = "api.telegram.org";
  String url = "/bot" + String(BOT_TOKEN) + "/getUpdates?timeout=0&offset=" + String(offset);

  if (!client.connect(host, 443)) return "";

  client.print(String("GET ") + url + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Connection: close\r\n\r\n");

  String resp = readAllTLS(client, 2500);
  client.stop();

  int idx = resp.indexOf("\r\n\r\n");
  if (idx >= 0) resp = resp.substring(idx + 4);
  return resp;
}

static void handleTelegramCommand(const String& cmd);

static void telegramService() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - tg_last_poll_ms < 5000) return;
  tg_last_poll_ms = millis();

  String body = telegramGetUpdates(tg_last_update_id + 1);
  if (body.length() == 0) return;

  int p = 0;
  while (true) {
    int u = body.indexOf("\"update_id\":", p);
    if (u < 0) break;
    u += 12;
    while (u < (int)body.length() && body[u] == ' ') u++;
    int e = u;
    while (e < (int)body.length() && isDigit(body[e])) e++;
    long upd = body.substring(u, e).toInt();
    if (upd > tg_last_update_id) tg_last_update_id = upd;

    int t = body.indexOf("\"text\":\"", e);
    if (t > 0) {
      t += 8;
      int te = body.indexOf("\"", t);
      if (te > t) {
        String txt = body.substring(t, te);
        txt.replace("\\/", "/");
        txt.replace("\\n", "\n");
        txt.trim();
        handleTelegramCommand(txt);
      }
    }
    p = e;
  }
}

// ============================================================
// 4B) WEB HELPERS
// ============================================================
static String readAllHTTP(WiFiClient &client, uint32_t timeoutMs = 15000) {
  String resp;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (client.available()) resp += (char)client.read();
    if (!client.connected()) break;
    delay(2);
  }
  return resp;
}

static int httpStatusCode(const String& resp) {
  int p = resp.indexOf("HTTP/");
  if (p < 0) return -1;
  int sp = resp.indexOf(' ', p);
  if (sp < 0 || sp + 4 > (int)resp.length()) return -1;
  return resp.substring(sp + 1, sp + 4).toInt();
}

static String httpBodyOnly(const String& resp) {
  int idx = resp.indexOf("\r\n\r\n");
  if (idx < 0) return resp;
  return resp.substring(idx + 4);
}

static int jsonGetIntField(const String& body, const char* key) {
  String k = String("\"") + key + "\"";
  int p = body.indexOf(k);
  if (p < 0) return 0;
  p = body.indexOf(':', p);
  if (p < 0) return 0;
  p++;
  while (p < (int)body.length() && (body[p] == ' ' || body[p] == '\n' || body[p] == '\r' || body[p] == '\t')) p++;
  int e = p;
  while (e < (int)body.length() && isDigit(body[e])) e++;
  return body.substring(p, e).toInt();
}

// ============================================================
// 4C) PIN API (GET/UPDATE)
// ============================================================
static bool webGetPin(String &outPin) {
  WiFiClientSecure client;
  client.setInsecure(); // agar tidak perlu sertifikat
  client.setTimeout(15);


  String url = String(WEB_GET_PIN_PATH) + "?" + WEB_API_FIELD + "=" + WEB_API_KEY;
  if (!client.connect(WEB_HOST, WEB_PORT)) return false;

  client.print(String("GET ") + url + " HTTP/1.1\r\n");
  client.print(String("Host: ") + WEB_HOST + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Connection: close\r\n\r\n");

  String resp = readAllHTTP(client, 15000);
  client.stop();

  if (httpStatusCode(resp) != 200) return false;
  String body = httpBodyOnly(resp);

  int p = body.indexOf("\"pin\"");
  if (p < 0) p = body.indexOf("\"pin_brangkas\"");
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < (int)body.length() && (body[p] == ' ' || body[p] == '\"')) p++;
  int e = p;
  while (e < (int)body.length() && isDigit(body[e])) e++;

  String pin = body.substring(p, e);
  pin.trim();
  if (pin.length() != 4) return false;

  outPin = pin;
  return true;
}

static bool webUpdatePin(const String& oldPin, const String& newPin) {
  WiFiClientSecure client;
  client.setInsecure(); // agar tidak perlu sertifikat
  client.setTimeout(15);

  if (!client.connect(WEB_HOST, WEB_PORT)) return false;

  String postData =
    String(WEB_API_FIELD) + "=" + WEB_API_KEY +
    "&pin_lama=" + oldPin +
    "&pin_baru=" + newPin;

  client.print(String("POST ") + WEB_UPDATE_PIN_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + WEB_HOST + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Length: " + String(postData.length()) + "\r\n\r\n");
  client.print(postData);

  String resp = readAllHTTP(client, 15000);
  client.stop();

  if (httpStatusCode(resp) != 200) return false;
  return resp.indexOf("\"ok\":true") >= 0;
}

// ============================================================
// 5) AVI BUILD (MJPG -> AVI)
// ============================================================
struct FrameInfo { uint32_t offset; uint32_t size; };
static uint8_t* aviBuf = nullptr;
static uint32_t aviLen = 0;
static FrameInfo frames[MAX_FRAMES];
static int frameCount = 0;

static void write32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void memWrite(const void* data, uint32_t n) {
  if (!aviBuf) return;
  if (aviLen + n > MAX_AVI_BYTES) return;
  memcpy(aviBuf + aviLen, data, n);
  aviLen += n;
}
static void memWrite4(const char* s4) { memWrite(s4, 4); }

static void memPad2() {
  if (aviLen & 1) { uint8_t z = 0; memWrite(&z, 1); }
}

static uint32_t memWriteSizePlaceholder() {
  uint32_t pos = aviLen;
  uint32_t zero = 0;
  memWrite(&zero, 4);
  return pos;
}

static void patchSizeAt(uint32_t pos, uint32_t size) {
  if (!aviBuf) return;
  write32(aviBuf + pos, size);
}

static void getWH(uint32_t &w, uint32_t &h) {
  if (CAM_FRAMESIZE == FRAMESIZE_QVGA) { w = 320; h = 240; return; }
  if (CAM_FRAMESIZE == FRAMESIZE_VGA)  { w = 640; h = 480; return; }
  w = 160; h = 120;
}

static inline void flashOn()  { digitalWrite(FLASH_GPIO_NUM, HIGH); }
static inline void flashOff() { digitalWrite(FLASH_GPIO_NUM, LOW);  }

static bool buildAvi(int fps = FPS, int seconds = SECONDS) {
  frameCount = 0;
  aviLen = 0;

  if (!aviBuf) {
    aviBuf = (uint8_t*)ps_malloc(MAX_AVI_BYTES);
    if (!aviBuf) return false;
  }

  for (int i = 0; i < 2; i++) {
    camera_fb_t* t = esp_camera_fb_get();
    if (t) esp_camera_fb_return(t);
    delay(40);
  }

  const int targetFrames = min(MAX_FRAMES, fps * seconds);
  uint32_t width, height;
  getWH(width, height);

  memWrite4("RIFF");
  uint32_t riffSizePos = memWriteSizePlaceholder();
  memWrite4("AVI ");

  memWrite4("LIST");
  uint32_t hdrlSizePos = memWriteSizePlaceholder();
  memWrite4("hdrl");

  memWrite4("avih");
  uint32_t avihSize = 56;
  memWrite(&avihSize, 4);

  uint32_t usPerFrame = 1000000UL / (uint32_t)fps;
  uint32_t flags = 0x10;

  uint8_t avih[56] = {0};
  write32(avih + 0,  usPerFrame);
  write32(avih + 12, flags);
  write32(avih + 16, (uint32_t)targetFrames);
  write32(avih + 24, 1);
  write32(avih + 32, width);
  write32(avih + 36, height);
  memWrite(avih, 56);

  memWrite4("LIST");
  uint32_t strlSizePos = memWriteSizePlaceholder();
  memWrite4("strl");

  memWrite4("strh");
  uint32_t strhSize = 56;
  memWrite(&strhSize, 4);

  uint8_t strh[56] = {0};
  memcpy(strh + 0, "vids", 4);
  memcpy(strh + 4, "MJPG", 4);
  write32(strh + 20, 1);
  write32(strh + 24, (uint32_t)fps);
  write32(strh + 32, (uint32_t)targetFrames);
  memWrite(strh, 56);

  memWrite4("strf");
  uint32_t strfSize = 40;
  memWrite(&strfSize, 4);

  uint8_t strf[40] = {0};
  write32(strf + 0, 40);
  write32(strf + 4, width);
  write32(strf + 8, height);
  strf[12] = 1;
  strf[14] = 24;
  memcpy(strf + 16, "MJPG", 4);
  memWrite(strf, 40);

  patchSizeAt(strlSizePos, (aviLen - (strlSizePos + 4)));
  patchSizeAt(hdrlSizePos, (aviLen - (hdrlSizePos + 4)));

  memWrite4("LIST");
  uint32_t moviSizePos = memWriteSizePlaceholder();
  memWrite4("movi");
  uint32_t moviDataStart = aviLen;

  for (int i = 0; i < targetFrames; i++) {
    flashOn();
    delay(15);
    camera_fb_t* fb = esp_camera_fb_get();
    flashOff();
    if (!fb) break;

    memWrite4("00dc");
    uint32_t sz = fb->len;
    memWrite(&sz, 4);

    frames[frameCount].offset = aviLen;
    frames[frameCount].size   = sz;

    memWrite(fb->buf, fb->len);
    memPad2();
    frameCount++;

    esp_camera_fb_return(fb);

    delay(1000 / fps);
    if (aviLen > (MAX_AVI_BYTES - 80000)) break;
  }

  patchSizeAt(moviSizePos, (aviLen - (moviSizePos + 4)));

  memWrite4("idx1");
  uint32_t idxSizePos = memWriteSizePlaceholder();
  uint32_t idxStart = aviLen;

  for (int i = 0; i < frameCount; i++) {
    memWrite4("00dc");
    uint32_t kf = 0x10;
    memWrite(&kf, 4);
    uint32_t offset = frames[i].offset - moviDataStart;
    memWrite(&offset, 4);
    uint32_t sz = frames[i].size;
    memWrite(&sz, 4);
  }

  patchSizeAt(idxSizePos, (aviLen - idxStart));
  patchSizeAt(riffSizePos, aviLen - 8);

  return frameCount > 0;
}

// ============================================================
// 6) WEB UPLOAD (photo_only/video_only)
// ============================================================
static int webUploadPhotoOnlyReturnId(const uint8_t* photoData, uint32_t photoLen, const char* aktivitasText) {
  WiFiClientSecure client;
  client.setInsecure(); // agar tidak perlu sertifikat
  client.setTimeout(30);


  String boundary = "----ESP32BoundaryWEB_PHOTO";

  String part1 =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"" + String(WEB_API_FIELD) + "\"\r\n\r\n" +
    String(WEB_API_KEY) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"mode\"\r\n\r\n"
    "photo_only\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"aktivitas\"\r\n\r\n" +
    String(aktivitasText) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  uint32_t totalLen = part1.length() + photoLen + tail.length();

  if (!client.connect(WEB_HOST, WEB_PORT)) return 0;

  client.print(String("POST ") + WEB_API_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + WEB_HOST + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(totalLen) + "\r\n\r\n");

  client.print(part1);

  const size_t chunk = 1024;
  for (size_t i = 0; i < photoLen; i += chunk) {
    size_t n = (photoLen - i >= chunk) ? chunk : (photoLen - i);
    if (client.write(photoData + i, n) != n) { client.stop(); return 0; }
    delay(0);
  }

  client.print(tail);

  String resp = readAllHTTP(client, 20000);
  client.stop();

  int code = httpStatusCode(resp);
  String body = httpBodyOnly(resp);
  if (code != 200) return 0;

  return jsonGetIntField(body, "id");
}

static bool webUploadVideoOnly(int aksesId, const uint8_t* videoData, uint32_t videoLen) {
  WiFiClientSecure client;;
  client.setInsecure(); // agar tidak perlu sertifikat
  client.setTimeout(30);

  String boundary = "----ESP32BoundaryWEB_VIDEO";

  String part1 =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"" + String(WEB_API_FIELD) + "\"\r\n\r\n" +
    String(WEB_API_KEY) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"mode\"\r\n\r\n"
    "video_only\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"akses_id\"\r\n\r\n" +
    String(aksesId) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"video\"; filename=\"video_5s.avi\"\r\n"
    "Content-Type: video/x-msvideo\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  uint32_t totalLen = part1.length() + videoLen + tail.length();

  if (!client.connect(WEB_HOST, WEB_PORT)) return false;

  client.print(String("POST ") + WEB_API_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + WEB_HOST + "\r\n");
  client.print("User-Agent: ESP32CAM\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(totalLen) + "\r\n\r\n");

  client.print(part1);

  const size_t chunk = 1024;
  for (size_t i = 0; i < videoLen; i += chunk) {
    size_t n = (videoLen - i >= chunk) ? chunk : (videoLen - i);
    if (client.write(videoData + i, n) != n) { client.stop(); return false; }
    delay(0);
  }

  client.print(tail);

  String resp = readAllHTTP(client, 30000);
  client.stop();

  return (httpStatusCode(resp) == 200);
}

// ============================================================
// 7) CAMERA INIT
// ============================================================
static bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;

#if ESP_IDF_VERSION_MAJOR >= 4
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
#else
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
#endif

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = CAM_FRAMESIZE;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, CAM_FRAMESIZE);
  s->set_quality(s, CAM_JPEG_QUALITY);
  return true;
}

// ============================================================
// 8) EVENT HANDLER
// ============================================================
static void handleEvent(int pulses) {
  String key = eventKeyFromPulse(pulses);

  lastEventKey = key;
  lastEventMs  = millis();

  String caption = makeCaption(key);
  String captionVid = caption + "\n🎥 Video 5 detik terlampir.";

  flashOn();
  delay(35);
  camera_fb_t* fb = esp_camera_fb_get();
  flashOff();
  if (!fb) return;

  uint32_t photoLen = fb->len;
  uint8_t* photoCopy = (uint8_t*)ps_malloc(photoLen);
  if (!photoCopy) { esp_camera_fb_return(fb); return; }
  memcpy(photoCopy, fb->buf, photoLen);
  esp_camera_fb_return(fb);

  telegramSendPhoto(photoCopy, photoLen, caption);

  int aksesId = webUploadPhotoOnlyReturnId(photoCopy, photoLen, key.c_str());

  if (needVideo(key)) {
    if (buildAvi(FPS, SECONDS)) {
      telegramSendAviAsDocument(aviBuf, aviLen, captionVid);
      if (aksesId > 0) webUploadVideoOnly(aksesId, aviBuf, aviLen);
    }
  }

  free(photoCopy);
}

// ============================================================
// 8B) SERIAL COMMAND SERVICE (UNO <-> ESP32)
// ============================================================
static String serialLine = "";

static void serialService() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      String line = serialLine;
      serialLine = "";
      line.trim();
      if (line.length() == 0) return;

      if (line == "GETPIN") {
        String pinWeb;
        if (WiFi.status() == WL_CONNECTED && webGetPin(pinWeb) && is4DigitPin(pinWeb)) {
          savePinCache(pinWeb);
        }
        Serial.print("PIN=");
        Serial.println(cachedPin);
        return;
      }

      if (line.startsWith("SETPIN")) {
        int pOld = line.indexOf("old=");
        int pNew = line.indexOf("new=");
        if (pOld < 0 || pNew < 0) { Serial.println("ERR"); return; }

        String oldPin = line.substring(pOld + 4, pOld + 8);
        String newPin = line.substring(pNew + 4, pNew + 8);
        oldPin.trim(); newPin.trim();

        if (!is4DigitPin(oldPin) || !is4DigitPin(newPin)) { Serial.println("ERR"); return; }
        if (WiFi.status() != WL_CONNECTED) { Serial.println("ERR"); return; }

        if (webUpdatePin(oldPin, newPin)) {
          savePinCache(newPin);
          Serial.println("OK");
        } else {
          Serial.println("ERR");
        }
        return;
      }

      Serial.println("ERR");
      return;
    } else {
      if (serialLine.length() < 80) serialLine += c;
      else serialLine = "";
    }
  }
}

// ============================================================
// TELEGRAM COMMANDS
// ============================================================
static void doManualPhoto(const String& caption) {
  if (WiFi.status() != WL_CONNECTED) {
    telegramSendMessage("❌ WiFi belum connect, tidak bisa kirim foto.");
    return;
  }

  flashOn();
  delay(35);
  camera_fb_t* fb = esp_camera_fb_get();
  flashOff();
  if (!fb) {
    telegramSendMessage("❌ Gagal ambil foto.");
    return;
  }

  telegramSendPhoto(fb->buf, fb->len, caption);
  esp_camera_fb_return(fb);
}

static void doManualVideo(const String& caption) {
  if (WiFi.status() != WL_CONNECTED) {
    telegramSendMessage("❌ WiFi belum connect, tidak bisa kirim video.");
    return;
  }

  telegramSendMessage("⏳ Rekam video 5 detik...");
  if (buildAvi(FPS, SECONDS)) {
    telegramSendAviAsDocument(aviBuf, aviLen, caption);
    telegramSendMessage("✅ Video terkirim");
  } else {
    telegramSendMessage("❌ Gagal rekam video");
  }
}

static void handleTelegramCommand(const String& cmd) {
  if (cmd == "/status") {
    telegramSendMessage(makeStatusText());
    return;
  }
  if (cmd == "/photo") {
    telegramSendMessage("📸 Foto manual");
    doManualPhoto("📸 Foto manual\n\n📍Brankas Pintar");
    telegramSendMessage("✅ Foto terkirim");
    return;
  }
  if (cmd == "/video") {
    doManualVideo("🎥 Video manual 5 detik\n\n📍Brankas Pintar");
    return;
  }
  telegramSendMessage("Perintah dikenal:\n/status\n/photo\n/video");
}

// ============================================================
// 9) SETUP / LOOP
// ============================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(400);

  loadPinCache();

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  flashOff();

  pinMode(TRIG_PIN, INPUT_PULLUP);

  // init boot guard
  bootAtMs = millis();
  decoderArmed = false;
  idleStableStartUs = 0;
  resetDecoderState(micros(), stableReadTrig());

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 25000) delay(250);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi gagal connect (PIN CACHE MODE)");
  } else {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
      Serial.println("❌ Gagal sync waktu NTP");
    } else {
      Serial.println("✅ Waktu NTP OK");
    }

    if (!initCamera()) {
      Serial.println("❌ Camera init gagal (PIN CACHE MODE)");
    }
  }

  Serial.println("✅ ESP32-CAM READY");
}

void loop() {
  // serial protocol from UNO
  serialService();

  uint32_t nowUs = micros();

  // ===== BOOT IGNORE =====
  if (millis() - bootAtMs < BOOT_IGNORE_MS) {
    resetDecoderState(nowUs, stableReadTrig());
    delay(2);
    return;
  }

  // ===== ARM only when TRIG idle stable =====
  if (!decoderArmed) {
    int s = stableReadTrig();
    bool active = trigIsActiveLevel(s);

    if (!active) {
      if (idleStableStartUs == 0) idleStableStartUs = nowUs;
      if (nowUs - idleStableStartUs >= IDLE_STABLE_US) {
        decoderArmed = true;
        resetDecoderState(nowUs, s);
        Serial.println("✅ DECODER ARMED");
      }
    } else {
      idleStableStartUs = 0;
    }

    delay(2);
    return;
  }

  // ===== RELAY FRIENDLY DECODER =====
  int raw = stableReadTrig();

  if (raw != lastStable) {
    if (nowUs - lastChangeUs >= DEBOUNCE_US) {
      lastStable = raw;
      lastChangeUs = nowUs;

      bool isActive = trigIsActiveLevel(lastStable);

      if (isActive && !inActive) {
        inActive = true;
        activeStartUs = nowUs;

        if (pulseCount == 0) burstStartUs = nowUs;
        lastEdgeUs = nowUs;
      }

      if (!isActive && inActive) {
        inActive = false;
        uint32_t onDur = nowUs - activeStartUs;

        if (onDur >= MIN_ON_US && onDur <= MAX_ON_US) {
          pulseCount++;
          lastEdgeUs = nowUs;
        }
      }
    }
  }

  if (pulseCount > 0) {
    if ((nowUs - lastEdgeUs) > BURST_GAP_US || (nowUs - burstStartUs) > MAX_BURST_TIME_US) {
      int pulses = (int)pulseCount;

      // reset state before handle
      resetDecoderState(nowUs, stableReadTrig());

      if (isValidPulse(pulses)) {
        Serial.printf("EVENT pulses=%d => %s\n", pulses, eventKeyFromPulse(pulses).c_str());
        handleEvent(pulses);
      } else {
        Serial.printf("IGNORED noise pulses=%d\n", pulses);

        // re-arm singkat anti noise beruntun
        decoderArmed = false;
        idleStableStartUs = 0;
      }
    }
  }

  // Telegram only when idle
  if (pulseCount == 0 && (lastEdgeUs == 0 || (micros() - lastEdgeUs) > 500000UL)) {
    telegramService();
  }

  delay(2);
}
