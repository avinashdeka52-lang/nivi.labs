// STEP 1: Arduino IDE -> Tools -> Partition Scheme ->
// "Minimal SPIFFS (1.9MB APP with OTA)"
// =====================================================
#define GITHUB_USER "avinashdeka52-lang"
#define NIVI_VERSION "2.3"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Arduino_JSON.h>
#include <Preferences.h>
#include "time.h"

// ===== PINS =====
#define OLED_MOSI 6
#define OLED_CLK  4
#define OLED_DC   1
#define OLED_CS   2
#define OLED_RESET 0
#define TOUCH_PIN 3

// ===== OTA URLS =====
#define VERSION_URL "https://raw.githubusercontent.com/" GITHUB_USER "/nivi.labs/main/version%20.txt"
#define FIRMWARE_URL "https://raw.githubusercontent.com/" GITHUB_USER "/nivi.labs/main/firmware.bin"

Adafruit_SSD1306 oled(128, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// ===== STATES =====
enum State { BOOT, WAKE, FACES, MAIN_MENU, ARCADE_MENU, CLOCK, WEATHER, FOCUS, CONFIG, NOTIFY, RESET_PAGE, VERSION_PAGE };
State curState = BOOT;
bool hasWoken = false, touchEnabled = true, isBeingTouched = false;

// ===== TIMING — central tick variables =====
unsigned long nowMs = 0; // updated once per loop(), use instead of millis()
unsigned long lastWeather = 0, focusStart = 0, lastAITick = 0;
unsigned long reactionTimer = 0, lastTapTime = 0;
unsigned long lastWifiRetry = 0;
int rapidTaps = 0, menuIdx = 0, arcadeIdx = 0;
int hiScoreDino = 0, hiScoreSnake = 0, hiScoreFlappy = 0;
int mood = 0, affection = 50, boredom = 0, energy = 100, memoryMood = 50;
unsigned long lastInteraction = 0;

// ===== CREDENTIALS (loaded from Preferences) =====
String ssid, pass, city = "Guwahati";
String apiKey = "";            // <-- no longer hardcoded

String remoteMsg = "";
int tempC = 0;
String weatherDesc = "Loading...";
bool weatherLoaded = false;

// ===== ANIMATION SYSTEM =====
#define ANIM_COUNT 12
int curAnim = 0;
unsigned long animStart = 0, animDuration = 20000;
bool animReacted = false;
unsigned long animReactTime = 0;
unsigned long notifyStart = 0;

// ===== MOCHI PHYSICS =====
const float SPRING = 0.20f, DAMPING = 0.70f;
struct Eye {
  float x, y, w, h, tx, ty, tw, th, vx, vy, vw, vh;
  void update() {
    vx = (vx + (tx-x)*SPRING)*DAMPING; x += vx;
    vy = (vy + (ty-y)*SPRING)*DAMPING; y += vy;
    vw = (vw + (tw-w)*SPRING)*DAMPING; w += vw;
    vh = (vh + (th-h)*SPRING)*DAMPING; h += vh;
  }
};
Eye le = {38,32,24,30,38,32,24,30,0,0,0,0};
Eye re = {90,32,24,30,90,32,24,30,0,0,0,0};
float lookOffsetX = 0;

// ===== PARTICLES =====
struct Particle { float x,y,vy; int life; bool active; int type; };
Particle particles[5];

void spawnParticle(float x, float y, int type) {
  for(int i=0;i<5;i++) {
    if(!particles[i].active) {
      particles[i] = {x, y, random(-15,-5)/10.0f, 255, true, type};
      break;
    }
  }
}

void updateParticles() {
  for(int i=0;i<5;i++) {
    if(!particles[i].active) continue;
    particles[i].y += particles[i].vy;
    particles[i].life -= 5;
    if(particles[i].life <= 0) { particles[i].active=false; continue; }
    int px = constrain((int)particles[i].x, 0, 125);
    int py = constrain((int)particles[i].y, 0, 61);
    if(particles[i].type==0) {
      oled.fillRect(px,py,2,2,1);
      if(px>0) oled.drawPixel(px-1,py,1);
      if(px<126) oled.drawPixel(px+2,py,1);
    }
  }
}

// ===== safeDelay: yield to server while waiting =====
void safeDelay(int d) {
  unsigned long end = millis() + d;
  while(millis() < end) { server.handleClient(); yield(); }
}

// ===== SAFE DRAW HELPERS =====
void sFillRect(int x, int y, int w, int h, uint16_t c=1) {
  if(w<=0||h<=0) return;
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  w=constrain(w,0,128-x); h=constrain(h,0,64-y);
  if(w>0&&h>0) oled.fillRect(x,y,w,h,c);
}
void sFillRoundRect(int x, int y, int w, int h, int r, uint16_t c=1) {
  if(w<=0||h<=0) return;
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  w=constrain(w,0,128-x); h=constrain(h,0,64-y);
  r=constrain(r,0,min(w,h)/2);
  if(w>0&&h>0) oled.fillRoundRect(x,y,w,h,r,c);
}
void sDrawLine(int x0,int y0,int x1,int y1) {
  x0=constrain(x0,0,127); y0=constrain(y0,0,63);
  x1=constrain(x1,0,127); y1=constrain(y1,0,63);
  oled.drawLine(x0,y0,x1,y1,1);
}
void sDrawPixel(int x, int y) { if(x>=0&&x<128&&y>=0&&y<64) oled.drawPixel(x,y,1); }
void sFillCircle(int x, int y, int r, uint16_t c=1) {
  if(r<=0 || x+r<0 || x-r>=128 || y+r<0 || y-r>=64) return;
  oled.fillCircle(x,y,r,c);
}
void sVLine(int x, int y, int h) {
  if(x<0||x>=128||h<=0) return;
  int y2=constrain(y+h-1,0,63); y=constrain(y,0,63);
  if(y<=y2) oled.drawFastVLine(x,y,y2-y+1,1);
}
void sHLine(int x, int y, int w) {
  if(y<0||y>=64||w<=0) return;
  int x2=constrain(x+w-1,0,127); x=constrain(x,0,127);
  if(x<=x2) oled.drawFastHLine(x,y,x2-x+1,1);
}

// ===== BOOT LOGO =====
void showBootLogo() {
  oled.setTextColor(1);
  oled.clearDisplay();
  sFillRect(62, 30, 4, 4);
  oled.display();
  safeDelay(200);

  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(24, 20);
  oled.print("[NIVI]");
  oled.setTextSize(1);
  oled.setCursor(52, 42);
  oled.print("LABS");
  oled.display();
  safeDelay(600);

  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(24, 20);
  oled.print("|NIVI|");
  oled.setTextSize(1);
  oled.setCursor(52, 42);
  oled.print("LABS");
  oled.display();
  safeDelay(600);
}

// ===== WEB PORTAL =====
// Now includes API Key and City fields so secrets stay off source code.
void handleRoot() {
  String h;
  h.reserve(2048);
  h += "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<style>body{background:#0d0d0d;color:#0ff;font-family:'Courier New',monospace;text-align:center;padding:20px}";
  h += ".box{border:2px solid #0ff;border-radius:10px;padding:20px;margin:20px auto;max-width:400px;box-shadow:0 0 15px #00ffff40}";
  h += "input[type=text],input[type=password]{width:90%;padding:10px;margin:10px 0;background:#1a1a1a;border:1px solid #0ff;color:#fff;border-radius:5px}";
  h += "input[type=submit]{background:#0ff;color:#000;border:none;padding:10px 20px;font-weight:bold;border-radius:5px;cursor:pointer;width:90%}";
  h += ".note{font-size:11px;color:#088;margin-top:6px}</style></head><body>";
  h += "<h1>NIVI LABS v" NIVI_VERSION "</h1>";

  // --- WiFi + credentials block ---
  h += "<div class='box'><h3>WIFI + SETUP</h3><form method='POST' action='/s'>";
  h += "<input name='s' type='text' placeholder='WiFi Name'><br>";
  h += "<input name='p' type='password' placeholder='WiFi Password'><br>";
  h += "<input name='c' type='text' placeholder='Your City (e.g. Mumbai)'><br>";
  h += "<input name='k' type='text' placeholder='OpenWeather API Key'><br>";
  h += "<p class='note'>Get a free key at openweathermap.org</p>";
  h += "<input type='submit' value='SAVE & REBOOT'></form></div>";

  // --- Ghost override block ---
  h += "<div class='box'><h3>GHOST OVERRIDE</h3><form action='/m'>";
  h += "<input name='v' type='text' placeholder='Message...' maxlength='15'><br>";
  h += "<input type='submit' value='SEND'></form></div></body></html>";

  server.send(200, "text/html", h);
}

void handleSave() {
  prefs.begin("nivi", false);
  if(server.arg("s") != "") prefs.putString("s", server.arg("s"));
  if(server.arg("p") != "") prefs.putString("p", server.arg("p"));
  if(server.arg("c") != "") prefs.putString("c", server.arg("c"));
  if(server.arg("k") != "") prefs.putString("k", server.arg("k")); // API key
  prefs.end();
  server.send(200, "text/html", "<h2 style='color:#0ff;text-align:center'>Saved! Rebooting...</h2>");
  safeDelay(1500);
  ESP.restart();
}

void handleMsg() {
  remoteMsg = server.arg("v");
  notifyStart = 0;
  curState = NOTIFY;
  server.send(200, "text/html", "<h2 style='color:#0ff;text-align:center'>Sent!</h2>");
}

// ===== WEATHER — only runs when WiFi is up =====
void fetchWeather() {
  if(WiFi.status() != WL_CONNECTED) return;
  if(apiKey.length() < 8) return;          // skip silently if key not set
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.openweathermap.org/data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=metric");
  http.setTimeout(6000);
  if(http.GET() > 0) {
    String payload = http.getString();
    if(payload.length() < 2048) {
      JSONVar obj = JSON.parse(payload);
      if(JSON.typeof(obj) != "undefined" && obj.hasOwnProperty("main") && obj["main"].hasOwnProperty("temp")) {
        tempC = (int)obj["main"]["temp"];
        weatherDesc = (const char*)obj["weather"][0]["main"];
        weatherLoaded = true;
      }
    }
  }
  http.end();
}

// ===== CLOUD OTA =====
void checkCloudOTA() {
  if(WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client, VERSION_URL); http.setTimeout(6000);
  if(http.GET() != 200) { http.end(); return; }
  String latest = http.getString(); latest.trim(); http.end();
  if(latest == NIVI_VERSION) return;

  oled.clearDisplay(); oled.setCursor(20,8); oled.print("Update found!");
  oled.setCursor(20,50); oled.print("Flashing Nivi..."); oled.display();
  safeDelay(2000);
  if(httpUpdate.update(client, FIRMWARE_URL) == HTTP_UPDATE_FAILED) {
    oled.clearDisplay(); oled.setCursor(20,38); oled.print("Retry next boot"); oled.display(); safeDelay(2000);
  }
}

// ===== AI CORE =====
// Uses nowMs (set once per loop) — no extra millis() calls inside
void processAI() {
  if(nowMs < reactionTimer || isBeingTouched) return;
  struct tm ti; getLocalTime(&ti);
  bool isNight = (ti.tm_hour >= 23 || ti.tm_hour < 6);

  if(isNight) { oled.ssd1306_command(0x81); oled.ssd1306_command(0x01); }
  else        { oled.ssd1306_command(0x81); oled.ssd1306_command(0xCF); }

  static unsigned long lastGlance = 0; static unsigned long glanceInterval = 2000;
  if(nowMs - lastGlance > glanceInterval && mood == 0) {
    if(random(10) > 3) { lookOffsetX = random(-12,13); le.ty = 32+random(-3,4); re.ty = le.ty; }
    else               { lookOffsetX = 0; le.ty = 32; re.ty = 32; }
    lastGlance = nowMs; glanceInterval = random(1500,4000);
  }

  switch(mood) {
    case 0:  le.tw=24;le.th=30;re.tw=24;re.th=30; break;
    case 1:  le.tw=20;le.th=2; re.tw=20;re.th=2; le.ty=38;re.ty=38;lookOffsetX=0; break;
    case 3:  le.tw=24;le.th=24;re.tw=24;re.th=24;le.ty=28;re.ty=28; break;
    case 13: le.tw=24;le.th=10;le.ty=32;re.tw=24;re.th=30;re.ty=32;lookOffsetX=5; break;
    case 2: case 7: le.tw=24;le.th=15;le.ty=35;re.tw=24;re.th=15;re.ty=35;lookOffsetX=0; break;
    default: le.tw=24;le.th=30;re.tw=24;re.th=30; break;
  }
  le.tx = 38 + lookOffsetX; re.tx = 90 + lookOffsetX;

  if(nowMs - lastAITick > 5000) {
    boredom += 2; energy -= 1; if(affection > 0) affection--;
    if(nowMs - lastInteraction > 10000) memoryMood--;
    if(affection > 70) memoryMood++;
    boredom     = constrain(boredom,    0,100);
    energy      = constrain(energy,     0,100);
    affection   = constrain(affection,  0,100);
    memoryMood  = constrain(memoryMood,10, 90);
    lastAITick  = nowMs;

    if(isNight && energy < 50)            mood = 1;
    else if(memoryMood < 20)              mood = 2;
    else if(energy < 15)                  mood = 6;
    else if(boredom > 70)                 mood = 4;
    else if(memoryMood > 80 && random(10) > 5) mood = 3;
    else { mood = 0; if(random(100) > 90) mood = 5; }

    if(curAnim == 0 && reactionTimer != 0 && nowMs > reactionTimer &&
       (mood==0||mood==3||mood==4||mood==5)) {
      curAnim = random(1, ANIM_COUNT+1);
      animStart = nowMs; animDuration = random(15000,31000); animReacted = false;
    }
  }
}

// ===== FACE RENDERER =====
void drawFace(int m) {
  oled.clearDisplay();
  float breath = isBeingTouched ? 0 : sin(nowMs / 400.0f) * 2.0f;
  float lDrawW = le.w + (30-le.h)*0.2f, rDrawW = re.w + (30-re.h)*0.2f;
  le.update(); re.update();
  int lx = (int)le.x, ly = constrain((int)(le.y + breath), 0, 48);
  int rx = (int)re.x, ry = constrain((int)(re.y + breath), 0, 48);
  int lw = constrain((int)lDrawW,4,40), lh = constrain((int)le.h,2,40);
  int rw = constrain((int)rDrawW,4,40), rh = constrain((int)re.h,2,40);
  switch(m) {
    case 1: sHLine(lx-lw/2,ly,lw); sHLine(lx-lw/2,ly+1,lw); sHLine(rx-rw/2,ry,rw); sHLine(rx-rw/2,ry+1,rw);
            if(random(100)>95) spawnParticle(100,20,1); break;
    case 3: sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8); sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
            sFillRect(lx-lw/2-2,ly,lw+4,lh,0); sFillRect(rx-rw/2-2,ry,rw+4,rh,0); break;
    case 4: sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8); sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
            sFillRect(lx-lw/2,ly-lh/2,lw,lh/2,0); sFillRect(rx-rw/2,ry-rh/2,rw,rh/2,0); break;
    case 13: sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,lh>10?8:2); sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
             sHLine(lx-lw/2-5,ly-lh/2-4,lw+10); break;
    case 2: sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8); sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
            sDrawLine(lx-lw/2-2,ly-lh/2-2,lx+lw/2+2,ly-lh/2+4);
            sDrawLine(rx+rw/2+2,ry-rh/2-2,rx-rw/2-2,ry-rh/2+4); break;
    case 7: sFillRoundRect(lx-14,ly-14,28,26,4); sFillRoundRect(rx-14,ry-14,28,26,4);
            sFillRect(lx-8,ly-8,6,8,0); sFillRect(lx+2,ly-8,6,8,0);
            sFillRect(rx-8,ry-8,6,8,0); sFillRect(rx+2,ry-8,6,8,0);
            sDrawLine(lx-14,ly-14,lx+14,ly-6); sDrawLine(rx-14,ry-6,rx+14,ry-14);
            sHLine(46,ly+16,36); sHLine(46,ly+17,36); break;
    default: sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8); sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8); break;
  }
  updateParticles();
  oled.display();
}

void playWakeAnimation() {
  touchEnabled = false;
  le.th = 2; re.th = 2; drawFace(1); safeDelay(600);
  le.th = 30; re.th = 30; le.ty = 20; re.ty = 20;
  for(int i=0;i<15;i++){drawFace(0);delay(15);} le.ty=32; re.ty=32;
  for(int i=0;i<15;i++){drawFace(0);delay(15);} le.th=2; re.th=2;
  for(int i=0;i<3;i++){drawFace(0);delay(15);}  le.th=30; re.th=30;
  for(int i=0;i<3;i++){drawFace(0);delay(15);}
  touchEnabled = true; hasWoken = true; curState = FACES;
}

// ===== ANIMATIONS =====
void anim1_HappyBounce() { oled.clearDisplay(); unsigned long t=nowMs-animStart; float bounce=abs(sin((float)t/300.0f))*8; int lx=38,rx=90,ly=32-(int)bounce,ry=32-(int)bounce; sFillRoundRect(lx-12,ly-14,24,20,7); sFillRoundRect(rx-12,ry-14,24,20,7); sFillRect(lx-14,ly-14,28,8,0); sFillRect(rx-14,ry-14,28,8,0); if((t/200)%4<2){sDrawPixel(lx-16,ly-16);sDrawPixel(rx+16,ry-16);} oled.display(); }
void anim2_ExcitedPulse() { oled.clearDisplay(); unsigned long t=nowMs-animStart; float pulse=sin((float)t/400.0f)*0.5f+0.5f; int size=(int)(10+pulse*12),lx=38,rx=90; sFillCircle(lx,32,size,1); sFillCircle(rx,32,size,1); sFillCircle(lx,32,size/3,0); sFillCircle(rx,32,size/3,0); oled.display(); }
void anim3_ConfidentStare() { oled.clearDisplay(); unsigned long t=nowMs-animStart; int lx=38,rx=90; sFillRoundRect(lx-14,28,28,8,4); sFillRoundRect(rx-14,28,28,8,4); float shimmer=sin((float)t/300.0f); if(shimmer>0){sFillRect(lx-10,28,20,8,0);sFillRect(rx-10,28,20,8,0);sHLine(lx-8,28,16);sHLine(rx-8,28,16);} oled.display(); }
void anim4_PlayfulWink() { oled.clearDisplay(); unsigned long cycle=(nowMs-animStart)%1200; int lx=38,rx=90; if(cycle<400){sFillRoundRect(lx-12,20,24,24,8);sFillRoundRect(rx-12,20,24,24,8);}else if(cycle<600){sHLine(lx-12,30,24);sHLine(lx-12,31,24);sFillRoundRect(rx-12,20,24,24,8);}else if(cycle<800){sFillRoundRect(lx-12,20,24,24,8);sFillRoundRect(rx-12,20,24,24,8);}else{sFillRoundRect(lx-12,20,24,24,8);sHLine(rx-12,30,24);sHLine(rx-12,31,24);} oled.display(); }
void anim5_CuriousGaze() { oled.clearDisplay(); float gaze=sin((float)(nowMs-animStart)/600.0f)*8; int lx=38+(int)gaze,rx=90+(int)gaze,ly=32-abs((int)gaze/2); sFillRoundRect(lx-13,ly-16,26,28,8); sFillRoundRect(rx-13,ly-16,26,28,8); sFillCircle(lx-5,ly-4,4,0); sFillCircle(rx-5,ly-4,4,0); oled.display(); }
void anim6_DreamyFloat() { oled.clearDisplay(); unsigned long t=nowMs-animStart; float bobY=sin((float)t/1000.0f)*4; float sizeVar=sin((float)t/800.0f)*4+24; int lx=38,rx=90,ly=32+(int)bobY,sz=(int)sizeVar; sFillRoundRect(lx-sz/2,ly-sz/2,sz,sz,sz/3); sFillRoundRect(rx-sz/2,ly-sz/2,sz,sz,sz/3); if((t/300)%2<1){sDrawPixel(lx-16,ly-8);sDrawPixel(rx+16,ly-8);} oled.display(); }
void anim7_DeterminedFocus() { oled.clearDisplay(); unsigned long t=nowMs-animStart; int lx=38,rx=90; sFillRoundRect(lx-14,30,28,4,2); sFillRoundRect(rx-14,30,28,4,2); sHLine(lx-16,24,32); sHLine(rx-16,24,32); float intensity=sin((float)t/400.0f)*0.3f+0.7f; if(intensity>0.8){sFillRect(lx-6,28,12,8,0);sFillRect(rx-6,28,12,8,0);} oled.display(); }
void anim8_SurprisedGasp() { oled.clearDisplay(); float expand=sin((float)(nowMs-animStart)/500.0f)*0.3f+0.7f; int size=(int)(24+expand*10),lx=38,rx=90; sFillCircle(lx,32,size/2,1); sFillCircle(rx,32,size/2,1); sFillCircle(lx,32,size/6,0); sFillCircle(rx,32,size/6,0); sHLine(lx-18,18,36); sHLine(rx-18,18,36); oled.display(); }
void anim9_LovingGlow() { oled.clearDisplay(); unsigned long t=nowMs-animStart; int lx=38,rx=90; float glow=sin((float)t/800.0f)*4+8; sFillRoundRect(lx-14,22,28,20,8); sFillRoundRect(rx-14,22,28,20,8); for(int i=0;i<(int)glow;i++){sDrawPixel(lx-18,20-i);sDrawPixel(rx+18,20-i);} sFillRect(lx-14,32,28,10,0); sFillRect(rx-14,32,28,10,0); oled.display(); }
void anim10_NervousFlutter() { oled.clearDisplay(); unsigned long flutter=(nowMs-animStart)%300; int lx=38,rx=90,ly=(flutter<75)?28:(flutter<150)?36:(flutter<225)?28:36; sFillRoundRect(lx-12,ly,24,16,6); sFillRoundRect(rx-12,ly,24,16,6); if(flutter<100){sDrawPixel(lx-8,ly+2);sDrawPixel(rx-8,ly+2);} oled.display(); }
void anim11_ProudDisplay() { oled.clearDisplay(); int lx=38,rx=90; float proud=sin((float)(nowMs-animStart)/600.0f); sFillRoundRect(lx-14,24,28,22,8); sFillRoundRect(rx-14,24,28,22,8); sHLine(lx-16,20,32); sHLine(rx-16,20,32); if(proud>0){sFillRect(lx-8,26,16,4,0);sFillRect(rx-8,26,16,4,0);} oled.display(); }
void anim12_SelfPlayingGame() {
  oled.clearDisplay(); unsigned long gameTime=(nowMs-animStart)%8000; int lx=38,rx=90,ly=12;
  sFillRoundRect(lx-10,ly,20,12,5); sFillRoundRect(rx-10,ly,20,12,5);
  sHLine(20,30,88); sHLine(20,40,88); sVLine(20,30,10); sVLine(108,30,10);
  if(gameTime<2000){int pos=(int)((gameTime/2000.0f)*60);sFillRect(30+pos,33,6,4);sFillCircle(64,37,2);}
  else if(gameTime<4000){int pos=(int)(((gameTime-2000)/2000.0f)*60);sHLine(30+pos,33,6);sVLine(64,32,6);}
  else if(gameTime<6000){int bounce=(int)(sin((gameTime-4000)/500.0f)*3);sFillCircle(64,35+bounce,2);}
  else{sDrawPixel(45,34);sDrawPixel(55,34);sDrawPixel(75,34);sDrawPixel(85,34);sDrawPixel(50,36);sDrawPixel(78,36);}
  oled.setCursor(30,45); oled.print("SCORE:"); oled.print((gameTime/2000)%4); oled.display();
}

void pickNextAnim() {
  int prev = curAnim, tries = 0;
  while(curAnim == prev && tries++ < 20) curAnim = random(1, ANIM_COUNT+1);
  animStart = nowMs; animDuration = random(15000,31000); animReacted = false;
}

void runAnim() {
  if(nowMs - animStart > animDuration) pickNextAnim();
  switch(curAnim) {
    case 1:  anim1_HappyBounce();    break; case 2:  anim2_ExcitedPulse();   break;
    case 3:  anim3_ConfidentStare(); break; case 4:  anim4_PlayfulWink();    break;
    case 5:  anim5_CuriousGaze();    break; case 6:  anim6_DreamyFloat();    break;
    case 7:  anim7_DeterminedFocus();break; case 8:  anim8_SurprisedGasp();  break;
    case 9:  anim9_LovingGlow();     break; case 10: anim10_NervousFlutter();break;
    case 11: anim11_ProudDisplay();  break; case 12: anim12_SelfPlayingGame();break;
    default: drawFace(mood); break;
  }
}

// ===== ARCADE GAMES =====
void playDino() {
  oled.clearDisplay(); oled.setCursor(45,30); oled.print("READY?"); oled.display(); safeDelay(1000);
  float y=40,dy=0,cx=128; int score=0;
  unsigned long lastTap=0; bool wasTouched=false;
  while(1) {
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    bool isTouched=digitalRead(TOUCH_PIN);
    if(isTouched&&!wasTouched){
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis(); lastInteraction=millis(); if(y>=40) dy=-8.5;
    }
    wasTouched=isTouched;
    y+=dy; dy+=0.8; if(y>40) y=40;
    cx-=5.0; if(cx<-10){cx=128;score++;}
    if(cx<25&&cx>10&&y>30){
      if(score>hiScoreDino){prefs.begin("nivi",false);prefs.putInt("hdino",score);prefs.end();hiScoreDino=score;}
      oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
      oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreDino);oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
    }
    oled.clearDisplay(); oled.drawFastHLine(0,60,128,1);
    sFillRect(10,(int)y,15,20); sFillRect((int)cx,45,8,15);
    oled.setCursor(0,0); oled.print(score); oled.display(); safeDelay(15);
  }
}

void playSnake() {
  oled.clearDisplay();oled.setCursor(45,30);oled.print("READY?");oled.display();safeDelay(1000);
  int sx[40],sy[40]; int len=3,dir=0,score=0;
  for(int i=0;i<len;i++){sx[i]=16-i;sy[i]=8;}
  int fx=random(1,31),fy=random(1,15);
  unsigned long lastMove=millis(),lastTap=0; bool wasTouched=false;
  while(1){
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    bool isTouched=digitalRead(TOUCH_PIN);
    if(isTouched&&!wasTouched){
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis();lastInteraction=millis();dir=(dir+1)%4;
    }
    wasTouched=isTouched;
    if(millis()-lastMove>280){
      for(int i=len-1;i>0;i--){sx[i]=sx[i-1];sy[i]=sy[i-1];}
      if(dir==0)sx[0]++;else if(dir==1)sy[0]++;else if(dir==2)sx[0]--;else sy[0]--;
      bool dead=(sx[0]<0||sx[0]>31||sy[0]<0||sy[0]>15);
      for(int i=1;i<len;i++) if(sx[0]==sx[i]&&sy[0]==sy[i]) dead=true;
      if(dead){
        if(score>hiScoreSnake){prefs.begin("nivi",false);prefs.putInt("hsnake",score);prefs.end();hiScoreSnake=score;}
        oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
        oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreSnake);oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
      }
      if(sx[0]==fx&&sy[0]==fy){if(len<40)len++;score++;fx=random(1,31);fy=random(1,15);}
      lastMove=millis();
    }
    oled.clearDisplay(); sFillRect(fx*4,fy*4,4,4);
    for(int i=0;i<len;i++) sFillRect(sx[i]*4,sy[i]*4,4,4);
    oled.display();
  }
}

void playFlappy() {
  oled.clearDisplay();oled.setCursor(45,30);oled.print("READY?");oled.display();safeDelay(1000);
  float y=32,dy=0; int px=128,gapY=random(10,30),score=0;
  unsigned long lastTap=0; bool wasTouched=false;
  while(1){
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    bool isTouched=digitalRead(TOUCH_PIN);
    if(isTouched&&!wasTouched){
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis();lastInteraction=millis();dy=-3.5;
    }
    wasTouched=isTouched;
    y+=dy; dy+=0.4; px-=3;
    if(px<-15){px=128;gapY=random(10,30);score++;}
    if(y<0||y>60||(px<26&&px>5&&(y<gapY||y>gapY+32))){
      if(score>hiScoreFlappy){prefs.begin("nivi",false);prefs.putInt("hflappy",score);prefs.end();hiScoreFlappy=score;}
      oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
      oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreFlappy);oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
    }
    oled.clearDisplay(); sFillRect(20,(int)y,6,6); sFillRect(px,0,15,gapY);
    sFillRect(px,gapY+32,15,constrain(64-(gapY+32),0,64));
    oled.setCursor(0,0); oled.print(score); oled.display(); safeDelay(30);
  }
}

// ===== SETUP =====
void setup() {
  setCpuFrequencyMhz(160);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  showBootLogo();

  prefs.begin("nivi", true);
  ssid        = prefs.getString("s",  "");
  pass        = prefs.getString("p",  "");
  city        = prefs.getString("c",  "Guwahati");
  apiKey      = prefs.getString("k",  "");          // load API key from storage
  hiScoreDino  = prefs.getInt("hdino",  0);
  hiScoreSnake = prefs.getInt("hsnake", 0);
  hiScoreFlappy= prefs.getInt("hflappy",0);
  memoryMood   = prefs.getInt("memMood",50);
  prefs.end();

  if(ssid == "") {
    curState = CONFIG;
    WiFi.softAP("Nivi-Setup");
    dnsServer.start(53, "*", WiFi.softAPIP());
  } else {
    // ===== WiFi connect — non-blocking display while waiting =====
    WiFi.begin(ssid.c_str(), pass.c_str());
    oled.clearDisplay(); oled.setCursor(10,30); oled.print("Nivi is waking..."); oled.display();
    unsigned long st = millis();
    while(WiFi.status() != WL_CONNECTED && millis()-st < 7000) safeDelay(200);

    if(WiFi.status() == WL_CONNECTED) {
      oled.clearDisplay();
      oled.setCursor(10,20); oled.print("Connected!");
      oled.setCursor(10,40); oled.print("IP: "); oled.print(WiFi.localIP());
      oled.display();
      safeDelay(3000);
      configTime(19800, 0, "in.pool.ntp.org", "time.google.com");
      struct tm ti; int retry = 0;
      while(!getLocalTime(&ti) && retry < 4) { safeDelay(500); retry++; }
      checkCloudOTA();
    }
    // No IP display if offline — fall straight to WAKE, no blocking
    safeDelay(200);
    curState = WAKE;
  }

  server.on("/",  handleRoot);
  server.on("/s", handleSave);
  server.on("/m", handleMsg);
  server.begin();
}

// ===== MAIN LOOP =====
void loop() {
  nowMs = millis(); // single timestamp for the whole frame

  if(curState == CONFIG) dnsServer.processNextRequest();
  server.handleClient();

  // WiFi reconnect — only when credentials exist, non-blocking
  if(ssid != "" && WiFi.status() != WL_CONNECTED) {
    if(nowMs - lastWifiRetry > 45000) { WiFi.reconnect(); lastWifiRetry = nowMs; }
  }

  // Weather fetch — only when WiFi is up; skip entirely if no key or no WiFi
  if(curState != CONFIG && WiFi.status() == WL_CONNECTED && apiKey.length() >= 8
     && nowMs - lastWeather > 300000) {
    fetchWeather(); lastWeather = nowMs;
  }

  // Periodic save
  static unsigned long lastSave = 0;
  if(nowMs - lastSave > 180000) {
    prefs.begin("nivi", false); prefs.putInt("memMood", memoryMood); prefs.end();
    lastSave = nowMs;
  }

  // ===== BLINK — only in idle face state =====
  static unsigned long lastBlink = 0; static bool isBlinking = false;
  if(curState == FACES && curAnim == 0 && !isBeingTouched &&
     (mood==0||mood==5||mood==4||mood==6)) {
    if( isBlinking && nowMs - lastBlink > 120) { le.th=30; re.th=30; isBlinking=false; }
    if(!isBlinking && nowMs - lastBlink > (unsigned long)random(2500,6000)) {
      le.th=2; re.th=2; isBlinking=true; lastBlink=nowMs;
    }
  }

  // ===== TOUCH MENU LOGIC =====
  if(touchEnabled && digitalRead(TOUCH_PIN)) {
    delay(10);
    if(!digitalRead(TOUCH_PIN)) return;
    unsigned long st = millis();
    isBeingTouched = true; lastInteraction = nowMs;

    if(curState == FACES && curAnim == 0) { le.ty=40; re.ty=40; le.th=15; re.th=15; lookOffsetX=0; }

    bool longPetTriggered = false;
    while(digitalRead(TOUCH_PIN)) {
      server.handleClient();
      if(curState == FACES) {
        if(curAnim != 0) runAnim(); else drawFace(mood);
        delay(15);
        if(millis()-st > 800 && !longPetTriggered) {
          curAnim=1; animStart=millis(); animDuration=3500; animReacted=false;
          affection=constrain(affection+15,0,100); boredom=0;
          energy=constrain(energy+10,0,100); memoryMood=constrain(memoryMood+5,10,90);
          mood=3;
          for(int i=0;i<3;i++) spawnParticle(random(30,90),random(10,30),0);
          longPetTriggered=true;
        }
      } else if(curState == RESET_PAGE && millis()-st > 5000) {
        prefs.begin("nivi",false); prefs.clear(); prefs.end();
        oled.clearDisplay(); oled.setCursor(20,30); oled.print("RESET DONE"); oled.display();
        safeDelay(1500); ESP.restart();
      }
      yield();
    }
    isBeingTouched = false; lastTapTime = nowMs;
    unsigned long dur = millis()-st; int clickType = 1;

    if(dur > 600) {
      clickType = 3;
    } else {
      unsigned long wt=millis(); bool dbl=false;
      while(millis()-wt < 250) {
        if(digitalRead(TOUCH_PIN)) {
          delay(10);
          if(digitalRead(TOUCH_PIN)) { dbl=true; while(digitalRead(TOUCH_PIN)) yield(); break; }
        }
        yield();
      }
      clickType = dbl ? 2 : 1;
    }

    if(curState == FACES) {
      if(clickType == 2) curState = MAIN_MENU;
      else if(clickType == 1) {
        animReacted=true; animReactTime=nowMs; rapidTaps++;
        if(rapidTaps>=5)      { mood=7; reactionTimer=nowMs+4000; rapidTaps=0; curAnim=0; }
        else if(rapidTaps>=3) { mood=2; affection=constrain(affection-10,0,100); reactionTimer=nowMs+3000; curAnim=0; }
        else                  { mood=13; reactionTimer=nowMs+1500; }
      }
      else if(clickType==3&&!longPetTriggered) { if(curAnim!=0) pickNextAnim(); }
    }
    else if(curState == MAIN_MENU) {
      if(clickType == 1) menuIdx = (menuIdx+1) % 7;   // 7 items now
      else if(clickType == 2) {
        if(menuIdx==0) curState=FACES;
        else if(menuIdx==1) curState=CLOCK;
        else if(menuIdx==2) { curState=WEATHER; if(!weatherLoaded) fetchWeather(); }
        else if(menuIdx==3) curState=ARCADE_MENU;
        else if(menuIdx==4) { curState=FOCUS; focusStart=millis(); }
        else if(menuIdx==5) curState=VERSION_PAGE;   // NEW: version screen
        else if(menuIdx==6) curState=RESET_PAGE;
      }
    }
    else if(curState == ARCADE_MENU) {
      if(clickType==1) arcadeIdx=(arcadeIdx+1)%4;
      else if(clickType==3) {
        if(arcadeIdx==0) playDino(); else if(arcadeIdx==1) playSnake();
        else if(arcadeIdx==2) playFlappy(); else if(arcadeIdx==3) curState=MAIN_MENU;
      } else if(clickType==2) curState=MAIN_MENU;
    }
    else if(curState==CLOCK||curState==WEATHER||curState==FOCUS||
            curState==RESET_PAGE||curState==VERSION_PAGE) {
      if(clickType==2||clickType==3) curState=MAIN_MENU;
    }
  } else {
    isBeingTouched = false;
    if(nowMs - lastTapTime > 1500 && rapidTaps > 0 && mood != 2 && mood != 7) rapidTaps = 0;
  }

  // ===== RENDER ROUTER =====
  switch(curState) {
    case WAKE: if(!hasWoken) playWakeAnimation(); break;

    case FACES:
      processAI();
      if(curAnim != 0) runAnim(); else drawFace(mood);
      // NO safeDelay here — loop runs as fast as possible for smooth animation
      break;

    case MAIN_MENU: {
      oled.clearDisplay();
      const char* m[] = {"Bio-Face","Time","Weather","Arcade","Focus","Version","Reset"};
      for(int i=0;i<7;i++) {
        int y=2+i*9;
        if(i==menuIdx) { sFillRect(0,y-1,128,9); oled.setTextColor(0); }
        oled.setCursor(5,y); oled.print(m[i]); oled.setTextColor(1);
      }
      oled.display(); break;
    }

    case ARCADE_MENU: {
      oled.clearDisplay();
      const char* g[] = {"Dino","Snake","Flappy","Back"};
      for(int i=0;i<4;i++) {
        int y=12+i*13;
        if(i==arcadeIdx) { sFillRect(0,y-1,128,11); oled.setTextColor(0); }
        oled.setCursor(10,y); oled.print(g[i]); oled.setTextColor(1);
      }
      oled.display(); break;
    }

    case CLOCK: {
      struct tm ti; getLocalTime(&ti);
      oled.clearDisplay();
      int hr = ti.tm_hour % 12; if(hr==0) hr=12;
      oled.setTextSize(2); oled.setCursor(8,20);
      oled.printf("%02d:%02d:%02d", hr, ti.tm_min, ti.tm_sec);
      oled.setTextSize(1); char ds[20]; strftime(ds,sizeof(ds),"%d-%b-%Y",&ti);
      oled.setCursor(31,42); oled.print(ds);
      oled.display(); break;
    }

    case WEATHER: {
      oled.clearDisplay(); oled.setTextSize(3); oled.setCursor(10,20); oled.print(tempC);
      oled.drawCircle(48,22,4,1); oled.drawCircle(48,22,3,1);
      oled.drawFastVLine(64,15,34,1);
      String w = weatherDesc; w.toLowerCase();
      if(w.indexOf("cloud") >= 0) {
        sFillCircle(85,25,5); sFillCircle(95,22,7); sFillCircle(105,25,5); sFillRect(85,25,20,6);
      } else if(w.indexOf("rain") >= 0) {
        oled.drawCircle(95,20,6,1);
        int dy2=30+(nowMs%1000)/100;
        sDrawLine(90,constrain(dy2,0,60),88,constrain(dy2+3,0,63));
        sDrawLine(100,constrain(dy2,0,60),98,constrain(dy2+3,0,63));
      } else {
        sFillCircle(95,24,6);
        if(nowMs%1000 > 500) { sHLine(85,24,4); sHLine(103,24,4); sVLine(95,14,4); sVLine(95,32,4); }
      }
      oled.setTextSize(1); oled.setCursor(68,40);
      oled.print(weatherDesc.substring(0, min((int)weatherDesc.length(), 9)));
      // Show "No API key" if key missing
      if(apiKey.length() < 8) { oled.setCursor(5,50); oled.print("Set API key in portal"); }
      oled.display(); break;
    }

    case FOCUS: {
      long elapsed = (millis()-focusStart)/1000; long rem = (25*60)-elapsed;
      if(rem <= 0) {
        affection=constrain(affection+10,0,100); energy=constrain(energy+10,0,100); mood=3;
        le.ty=32; re.ty=32; le.th=24; re.th=24; reactionTimer=nowMs+3000; curState=FACES; break;
      }
      oled.clearDisplay(); oled.setTextSize(2); oled.setCursor(30,20);
      oled.printf("%02d:%02d", (int)(rem/60), (int)(rem%60));
      oled.drawRoundRect(14,45,100,8,3,1);
      int pw = constrain((int)map(elapsed,0,25*60,0,96),0,96);
      sFillRoundRect(16,47,pw,4,2);
      oled.setTextSize(1); oled.display(); break;
    }

    case CONFIG:
      oled.clearDisplay(); oled.setCursor(10,25); oled.print("WIFI: NiviSetup"); oled.display(); break;

    case NOTIFY: {
      if(notifyStart==0) notifyStart=nowMs;
      oled.clearDisplay(); oled.drawRect(0,0,128,64,1);
      oled.setCursor(5,10); oled.print(remoteMsg); oled.display();
      if(nowMs - notifyStart > 4000) { notifyStart=0; curState=FACES; } break;
    }

    // ===== VERSION PAGE (new) =====
    case VERSION_PAGE: {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setCursor(28, 8);  oled.print("NIVI LABS");
      oled.setCursor(20, 20); oled.print("Ver: " NIVI_VERSION);
      oled.setCursor(5,  32); oled.print("City: "); oled.print(city);
      oled.setCursor(5,  42); oled.print("API: ");
      oled.print(apiKey.length() >= 8 ? "Set" : "Not set");
      oled.setCursor(5,  54); oled.print("Tap x2 to go back");
      oled.display(); break;
    }

    case RESET_PAGE:
      oled.clearDisplay(); oled.setCursor(10,30); oled.print("HOLD 5S TO WIPE"); oled.display(); break;

    default: break;
  }

  // Safety restart on low heap
  if(ESP.getFreeHeap() < 12000) ESP.restart();
}
