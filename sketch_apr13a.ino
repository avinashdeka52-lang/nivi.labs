// ===== NIVI OS v2.1 (God Tier Animation Edition) =====
//
// Animations : Aurora Wave, Wink, Scan/HUD, Lightning Storm, Sleep Story, Peek
// Boot Logo  : Nivi Labs (flask icon + clean branding)
// OTA        : GitHub cloud updates on every boot
// WiFi Fix   : reconnect only every 20s — no more lag when disconnected
//
// =====================================================
// STEP 1: Arduino IDE -> Tools -> Partition Scheme ->
//         "Minimal SPIFFS (1.9MB APP with OTA)"
//         REQUIRED or OTA will silently fail.
//
// STEP 2: Replace YOURUSERNAME below with your
//         actual GitHub username.
// =====================================================
#define GITHUB_USER  "avinashdeka52-lang"
#define NIVI_VERSION "2.1"
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
#define OLED_MOSI  6
#define OLED_CLK   4
#define OLED_DC    1
#define OLED_CS    2
#define OLED_RESET 0
#define TOUCH_PIN  3
// ===== OTA URLS =====
#define VERSION_URL  "https://raw.githubusercontent.com/avinashdeka52-lang/nivi.labs/main/version.txt"
#define FIRMWARE_URL "https://raw.githubusercontent.com/avinashdeka52-lang/nivi.labs/main/firmware.bin"
Adafruit_SSD1306 oled(128, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);
Preferences prefs;
WebServer    server(80);
DNSServer    dnsServer;
// ===== STATES =====
enum State { BOOT, WAKE, FACES, MAIN_MENU, ARCADE_MENU,
             CLOCK, WEATHER, FOCUS, CONFIG, NOTIFY, RESET_PAGE };
State curState = BOOT;
bool hasWoken = false, touchEnabled = true, isBeingTouched = false;
unsigned long lastWeather = 0, focusStart = 0, lastAITick = 0;
unsigned long reactionTimer = 0, lastTapTime = 0;
unsigned long lastWifiRetry = 0;
int rapidTaps = 0, menuIdx = 0, arcadeIdx = 0;
int hiScoreDino = 0, hiScoreSnake = 0, hiScoreFlappy = 0;
int mood = 0;
int affection = 50, boredom = 0, energy = 100, memoryMood = 50;
unsigned long lastInteraction = 0;
String ssid, pass, remoteMsg = "", city = "Guwahati";
String apiKey = "2a96703b35e4060148dda2d95790566e";
int    tempC  = 0;
String weatherDesc = "Loading...";
// ===== ANIMATION SYSTEM =====
#define ANIM_COUNT 6
int           curAnim      = 0;
unsigned long animStart    = 0;
unsigned long animDuration = 20000;
bool          animReacted  = false;
unsigned long animReactTime = 0;
// ===== NOTIFY =====
unsigned long notifyStart = 0;
// ===== MOCHI PHYSICS =====
const float SPRING = 0.20f, DAMPING = 0.70f;
struct Eye {
  float x, y, w, h, tx, ty, tw, th, vx, vy, vw, vh;
  // BUG FIX 1: vh line was outside the function — moved inside
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
    particles[i].y  += particles[i].vy;
    particles[i].life -= 5;
    if(particles[i].life <= 0) { particles[i].active=false; continue; }
    int px = constrain((int)particles[i].x, 0, 125);
    int py = constrain((int)particles[i].y, 0, 61);
    if(particles[i].type==0) {
      oled.fillRect(px,py,2,2,1);
      if(px>0)   oled.drawPixel(px-1,py,1);
      if(px<126) oled.drawPixel(px+2,py,1);
    } else if(particles[i].type==1) {
      oled.setCursor(px,py); oled.print("z");
    }
  }
}
void safeDelay(int d) {
  for(int i=0;i<d;i++) { delay(1); server.handleClient(); yield(); }
}
// =============================================
// ========== SAFE DRAW HELPERS ================
// =============================================
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
void sDrawPixel(int x, int y) {
  if(x>=0&&x<128&&y>=0&&y<64) oled.drawPixel(x,y,1);
}
void sFillCircle(int x, int y, int r, uint16_t c=1) {
  if(r<=0) return;
  if(x+r<0||x-r>=128||y+r<0||y-r>=64) return;
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
void drawStar(int cx, int cy, int r) {
  if(r<1||cx<0||cx>=128||cy<0||cy>=64) return;
  sDrawLine(cx,cy-r,cx+r,cy); sDrawLine(cx+r,cy,cx,cy+r);
  sDrawLine(cx,cy+r,cx-r,cy); sDrawLine(cx-r,cy,cx,cy-r);
  sFillRect(cx-1,cy-1,3,3);
}
// ===== WEB PORTAL =====
void handleRoot() {
  // BUG FIX 2: All multi-line strings joined onto single lines
  String h = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<style>body{background:#0d0d0d;color:#0ff;font-family:'Courier New',monospace;text-align:center;padding:20px}";
  h += ".box{border:2px solid #0ff;border-radius:10px;padding:20px;margin:20px auto;max-width:400px;box-shadow:0 0 15px #00ffff40}";
  h += "input[type=text],input[type=password]{width:90%;padding:10px;margin:10px 0;background:#1a1a1a;border:1px solid #0ff;color:#fff;border-radius:5px}";
  h += "input[type=submit]{background:#0ff;color:#000;border:none;padding:10px 20px;font-weight:bold;border-radius:5px;cursor:pointer;width:90%}</style></head><body>";
  h += "<h1>NIVI LABS v" NIVI_VERSION "</h1>";
  h += "<div class='box'><h3>WIFI SETUP</h3><form method='POST' action='/s'>";
  h += "<input name='s' type='text' placeholder='WiFi Name'><br>";
  h += "<input name='p' type='password' placeholder='Password'><br>";
  h += "<input name='c' type='text' placeholder='Your City (e.g. Mumbai)'><br>";
  h += "<input type='submit' value='CONNECT'></form></div>";
  h += "<div class='box'><h3>GHOST OVERRIDE</h3><form action='/m'>";
  h += "<input name='v' type='text' placeholder='Message...' maxlength='15'><br>";
  h += "<input type='submit' value='SEND'></form></div></body></html>";
  server.send(200,"text/html",h);
}
void handleSave() {
  prefs.begin("nivi",false);
  prefs.putString("s",server.arg("s"));
  prefs.putString("p",server.arg("p"));
  if(server.arg("c")!="") prefs.putString("c",server.arg("c"));
  prefs.end();
  // BUG FIX 2: String joined onto single line
  server.send(200,"text/html","<h2 style='color:#0ff;text-align:center'>Saved! Rebooting...</h2>");
  safeDelay(1500); ESP.restart();
}
void handleMsg() {
  remoteMsg=server.arg("v"); notifyStart=0; curState=NOTIFY;
  // BUG FIX 2: String joined onto single line
  server.send(200,"text/html","<h2 style='color:#0ff;text-align:center'>Sent!</h2>");
}
// ===== WEATHER =====
void fetchWeather() {
  if(WiFi.status()!=WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  // BUG FIX 2: URL string joined onto single line
  http.begin(client,"https://api.openweathermap.org/data/2.5/weather?q="+city+"&appid="+apiKey+"&units=metric");
  http.setTimeout(6000);
  if(http.GET()>0) {
    String payload=http.getString();
    if(payload.length()<2048) {
      JSONVar obj=JSON.parse(payload);
      if(JSON.typeof(obj)!="undefined") {
        tempC=(int)obj["main"]["temp"];
        weatherDesc=(const char*)obj["weather"][0]["main"];
      }
    }
  }
  http.end();
}
// ===== CLOUD OTA =====
void checkCloudOTA() {
  if(WiFi.status()!=WL_CONNECTED) return;
  oled.clearDisplay();
  oled.setCursor(16,28); oled.print("Checking update...");
  oled.display();
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client,VERSION_URL);
  http.setTimeout(6000);
  int code=http.GET();
  if(code!=200) { http.end(); return; }
  String latest=http.getString(); latest.trim(); http.end();
  if(latest==NIVI_VERSION) return;
  oled.clearDisplay();
  oled.setCursor(20,8);  oled.print("Update found!");
  oled.setCursor(20,22); oled.print("Current: v" NIVI_VERSION);
  oled.setCursor(20,34); oled.print("New:     v"+latest);
  oled.setCursor(20,50); oled.print("Flashing Nivi...");
  oled.display(); safeDelay(2000);
  t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL);
  if(ret==HTTP_UPDATE_FAILED) {
    oled.clearDisplay();
    oled.setCursor(20,24); oled.print("Update failed :(");
    oled.setCursor(20,38); oled.print("Retry next boot");
    oled.display(); safeDelay(2000);
  }
}
// ===== NIVI LABS BOOT LOGO =====
void showBootLogo() {
  oled.clearDisplay(); oled.setTextColor(1);
  sFillRect(60,14,8,11,1);
  sHLine(56,13,16);
  sHLine(57,14,14);
  sDrawLine(60,25,50,35);
  sDrawLine(67,25,77,35);
  sVLine(50,35,16); sVLine(77,35,16);
  sHLine(50,51,28);
  sFillRect(51,41,26,10,1);
  sHLine(51,40,26);
  oled.drawCircle(58,37,2,1);
  oled.drawCircle(68,38,2,1);
  sDrawPixel(63,36); sDrawPixel(72,35);
  oled.display(); safeDelay(900);
  oled.clearDisplay();
  oled.drawFastVLine(6,5,54,1);  oled.drawFastHLine(6,5,10,1);  oled.drawFastHLine(6,58,10,1);
  oled.drawFastVLine(121,5,54,1); oled.drawFastHLine(111,5,10,1); oled.drawFastHLine(111,58,10,1);
  oled.setTextSize(2); oled.setCursor(40,10); oled.print("NIVI");
  sHLine(38,30,52);
  sDrawPixel(33,30); sDrawPixel(31,30);
  sDrawPixel(94,30); sDrawPixel(96,30);
  oled.setTextSize(1); oled.setCursor(50,35); oled.print("LABS");
  oled.setCursor(38,48); oled.print("OS v" NIVI_VERSION);
  oled.display(); safeDelay(1600);
}
// ===== AI CORE =====
void processAI() {
  if(millis()<reactionTimer||isBeingTouched) return;
  struct tm ti; getLocalTime(&ti);
  bool isNight=(ti.tm_hour>=23||ti.tm_hour<6);
  if(isNight) oled.ssd1306_command(0x81); else oled.ssd1306_command(0xCF);
  static unsigned long lastGlance=0;
  static unsigned long glanceInterval=2000;
  if(millis()-lastGlance>glanceInterval&&mood==0) {
    if(random(10)>3) { lookOffsetX=random(-12,13); le.ty=32+random(-3,4); re.ty=le.ty; }
    else             { lookOffsetX=0; le.ty=32; re.ty=32; }
    lastGlance=millis(); glanceInterval=random(1500,4000);
  }
  switch(mood) {
    case 0:  le.tw=24;le.th=30;re.tw=24;re.th=30; break;
    case 1:  le.tw=20;le.th=2; re.tw=20;re.th=2; le.ty=38;re.ty=38;lookOffsetX=0; break;
    case 3:  le.tw=24;le.th=24;re.tw=24;re.th=24;le.ty=28;re.ty=28; break;
    case 13: le.tw=24;le.th=10;le.ty=32;re.tw=24;re.th=30;re.ty=32;lookOffsetX=5; break;
    case 2:
    case 7:  le.tw=24;le.th=15;le.ty=35;re.tw=24;re.th=15;re.ty=35;lookOffsetX=0; break;
    default: le.tw=24;le.th=30;re.tw=24;re.th=30; break;
  }
  le.tx=38+lookOffsetX; re.tx=90+lookOffsetX;
  if(millis()-lastAITick>5000) {
    boredom+=2; energy-=1;
    if(affection>0) affection-=1;
    if(millis()-lastInteraction>10000) memoryMood-=1;
    if(affection>70) memoryMood+=1;
    boredom=constrain(boredom,0,100); energy=constrain(energy,0,100);
    affection=constrain(affection,0,100); memoryMood=constrain(memoryMood,10,90);
    lastAITick=millis();
    if(isNight&&energy<50)                   mood=1;
    else if(memoryMood<20)                   mood=2;
    else if(energy<15)                       mood=6;
    else if(boredom>70)                      mood=4;
    else if(memoryMood>80&&random(10)>5)     mood=3;
    else { mood=0; if(random(100)>90)        mood=5; }
    if(curAnim==0&&reactionTimer!=0&&millis()>reactionTimer&&
       (mood==0||mood==3||mood==4||mood==5)) {
      curAnim=random(1,ANIM_COUNT+1);
      animStart=millis(); animDuration=random(15000,31000); animReacted=false;
    }
  }
}
// ===== FACE RENDERER =====
void drawFace(int m) {
  oled.clearDisplay();
  float breath=isBeingTouched?0:sin(millis()/400.0)*2.0;
  float lDrawW=le.w+(30-le.h)*0.2; float rDrawW=re.w+(30-re.h)*0.2;
  le.update(); re.update();
  int lx=(int)le.x, ly=constrain((int)(le.y+breath),0,48);
  int rx=(int)re.x, ry=constrain((int)(re.y+breath),0,48);
  int lw=constrain((int)lDrawW,4,40), lh=constrain((int)le.h,2,40);
  int rw=constrain((int)rDrawW,4,40), rh=constrain((int)re.h,2,40);
  switch(m) {
    case 1: // Sleepy
      sHLine(lx-lw/2,ly,lw); sHLine(lx-lw/2,ly+1,lw);
      sHLine(rx-rw/2,ry,rw); sHLine(rx-rw/2,ry+1,rw);
      if(random(100)>95) spawnParticle(100,20,1);
      break;
    case 3: // Happy
      sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8);
      sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
      sFillRect(lx-lw/2-2,ly,lw+4,lh,0);
      sFillRect(rx-rw/2-2,ry,rw+4,rh,0);
      break;
    case 4: // Bored
      sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8);
      sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
      sFillRect(lx-lw/2,ly-lh/2,lw,lh/2,0);
      sFillRect(rx-rw/2,ry-rh/2,rw,rh/2,0);
      break;
    case 13: // Curious
      sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,lh>10?8:2);
      sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
      sHLine(lx-lw/2-5,ly-lh/2-4,lw+10);
      break;
    case 2: // Angry
      sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8);
      sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
      sDrawLine(lx-lw/2-2,ly-lh/2-2,lx+lw/2+2,ly-lh/2+4);
      sDrawLine(rx+rw/2+2,ry-rh/2-2,rx-rw/2-2,ry-rh/2+4);
      break;
    case 7: // SUPER HANGRY
      sFillRoundRect(lx-14,ly-14,28,26,4);
      sFillRoundRect(rx-14,ry-14,28,26,4);
      sFillRect(lx-8,ly-8,6,8,0); sFillRect(lx+2,ly-8,6,8,0);
      sFillRect(rx-8,ry-8,6,8,0); sFillRect(rx+2,ry-8,6,8,0);
      sDrawLine(lx-14,ly-14,lx+14,ly-6);
      sDrawLine(rx-14,ry-6,rx+14,ry-14);
      sHLine(46,ly+16,36); sHLine(46,ly+17,36);
      break;
    default: // Normal
      sFillRoundRect(lx-lw/2,ly-lh/2,lw,lh,8);
      sFillRoundRect(rx-rw/2,ry-rh/2,rw,rh,8);
      break;
  }
  updateParticles(); oled.display();
}
// ===== WAKE ANIMATION =====
void playWakeAnimation() {
  touchEnabled=false;
  le.th=2; re.th=2; drawFace(1); safeDelay(600);
  le.th=30; re.th=30; le.ty=20; re.ty=20;
  for(int i=0;i<15;i++) { drawFace(0); delay(15); }
  le.ty=32; re.ty=32;
  for(int i=0;i<15;i++) { drawFace(0); delay(15); }
  le.th=2; re.th=2;
  for(int i=0;i<3;i++)  { drawFace(0); delay(15); }
  le.th=30; re.th=30;
  for(int i=0;i<3;i++)  { drawFace(0); delay(15); }
  touchEnabled=true; hasWoken=true;
  curAnim=random(1,ANIM_COUNT+1);
  animStart=millis(); animDuration=random(15000,31000);
  curState=FACES;
}
// =============================================
// ========== GOD TIER ANIMATIONS ==============
// =============================================
// ===== 1. AURORA WAVE =====
void animAurora() {
  oled.clearDisplay();
  unsigned long t  = millis() - animStart;
  float spd        = (float)t / 700.0f;
  for(int x=0; x<128; x++) {
    int y = 6 + (int)(sin((float)x/13.0f + spd)       * 5
                    + sin((float)x/7.0f  + spd*1.5f)  * 3);
    sDrawPixel(x, constrain(y,   0, 63));
    sDrawPixel(x, constrain(y+1, 0, 63));
  }
  for(int x=0; x<128; x++) {
    int y = 57 + (int)(sin((float)x/11.0f + spd*1.2f) * 4
                     + sin((float)x/6.0f  + spd*0.9f) * 3);
    sDrawPixel(x, constrain(y,   0, 63));
    sDrawPixel(x, constrain(y-1, 0, 63));
  }
  for(int x=0; x<128; x++) {
    bool inLeftEye  = (x>20 && x<58);
    bool inRightEye = (x>70 && x<110);
    if(inLeftEye || inRightEye) continue;
    int y = 32 + (int)(sin((float)x/9.0f + spd*1.8f) * 4);
    sDrawPixel(x, constrain(y, 0, 63));
  }
  float pulse  = sin(spd * 0.65f);
  int   eyeH   = constrain(26+(int)(pulse*5), 14, 36);
  int   eyeY   = constrain(32 - eyeH/2,       12, 30);
  int   lx=38, rx=90;
  sFillRoundRect(lx-12, eyeY, 24, eyeH, 7);
  sFillRoundRect(rx-12, eyeY, 24, eyeH, 7);
  int glintX = (int)(sin(spd*1.1f) * 6) + 4;
  sFillRect(constrain(lx+glintX,   lx-11, lx+9),  eyeY+3, 4, 4, 0);
  sFillRect(constrain(rx+glintX,   rx-11, rx+9),  eyeY+3, 4, 4, 0);
  if(animReacted && millis()-animReactTime < 900) {
    float rt  = (float)(millis()-animReactTime);
    float fst = rt/70.0f;
    for(int x=0; x<128; x++) {
      bool inL = (x>20&&x<58); bool inR = (x>70&&x<110);
      if(inL||inR) continue;
      int y1 = 20+(int)(sin((float)x/4.5f+fst)*11);
      int y2 = 44+(int)(sin((float)x/3.5f+fst*1.4f)*9);
      sDrawPixel(x, constrain(y1,0,63));
      sDrawPixel(x, constrain(y2,0,63));
    }
  }
  oled.display();
}
// ===== 2. WINK =====
void animWink() {
  oled.clearDisplay();
  unsigned long t = millis()-animStart;
  int breath = (int)(sin(t/500.0)*2);
  sFillRoundRect(78,16+breath,24,30,8);
  sFillRect(84,20+breath,5,5,0);
  sHLine(26,30+breath,24); sHLine(26,31+breath,24);
  sHLine(27,32+breath,22); sHLine(29,33+breath,18);
  sDrawPixel(27,29+breath); sDrawPixel(49,29+breath);
  if((t/400UL)%3<2) drawStar(14,16,6);
  sDrawLine(46,54,56,58); sDrawLine(56,58,72,58); sDrawLine(72,58,82,54);
  if(animReacted && millis()-animReactTime<700) {
    sFillRect(78,16+breath,24,30,0);
    sHLine(78,30+breath,24); sHLine(78,31+breath,24);
    sHLine(79,32+breath,22); sHLine(81,33+breath,18);
    drawStar(100,14,6);
  }
  oled.display();
}
// ===== 3. SCAN / HUD =====
void animScan() {
  oled.clearDisplay();
  unsigned long t = millis()-animStart;
  sFillRoundRect(26,17,24,30,8); sFillRoundRect(78,17,24,30,8);
  sHLine(0,0,12);   sVLine(0,0,10);
  sHLine(116,0,12); sVLine(127,0,10);
  sHLine(0,63,12);  sVLine(0,54,10);
  sHLine(116,63,12);sVLine(127,54,10);
  int scanY=(t/20UL)%64;
  sHLine(0,scanY,128);
  if(scanY>0) sHLine(0,scanY-1,96);
  if(scanY>1) sHLine(0,scanY-2,64);
  if(scanY>2) sHLine(0,scanY-3,32);
  oled.setCursor(2,12);
  oled.print((t/700UL)%2==0 ? "SYS:OK" : "NIVI  ");
  if(animReacted && millis()-animReactTime<1500) {
    sFillRect(4,50,120,12,1);
    oled.setTextColor(0); oled.setCursor(6,53);
    oled.print(">> ACCESS GRANTED <<");
    oled.setTextColor(1);
  }
  oled.display();
}
// ===== 4. LIGHTNING STORM =====
void animLightning() {
  unsigned long t     = millis()-animStart;
  unsigned long cycle = t % 4500UL;
  bool flash1     = (cycle>=4000UL && cycle<4120UL);
  bool flash2     = (cycle>=4250UL && cycle<4340UL);
  bool isFlash    = flash1||flash2;
  bool afterFlash = (cycle>=4120UL && cycle<4500UL);
  if(isFlash) {
    oled.fillRect(0,0,128,64,1);
    oled.display();
    return;
  }
  oled.clearDisplay();
  const int   rainX[]   = {4, 12, 23, 34, 46, 58, 68, 80, 91, 103, 114, 123};
  const int   rainSpd[] = {55, 72, 48, 65, 78, 52, 69, 85, 58, 74,  62, 50 };
  const int   rainOff[] = {0,  14,  7, 21,  3, 17, 10, 25,  5, 18,  8, 13 };
  const int   rainLen[] = {5,  4,   6,  4,  5,  6,  4,  5,  6,  4,  5,  6  };
  for(int i=0;i<12;i++) {
    int ry = (int)((float)t/rainSpd[i]+rainOff[i]) % 72 - 8;
    int startY = max(ry, 16);
    int len    = constrain(rainLen[i], 0, 63-startY);
    if(len>0 && startY<63) sVLine(rainX[i], startY, len);
  }
  sFillCircle(52, 11, 8);
  sFillCircle(62,  7, 10);
  sFillCircle(73,  7, 10);
  sFillCircle(83, 11, 8);
  sFillRect(52, 11, 33, 9);
  sFillRect(44, 0, 42, 8, 0);
  int lx=38, rx=90;
  if(afterFlash) {
    sFillRoundRect(lx-14,24,28,30,8);
    sFillRoundRect(rx-14,24,28,30,8);
    sDrawLine(lx-12,20,lx-3,18);
    sDrawLine(rx+3, 18,rx+12,20);
    oled.drawCircle(64,58,5,1); sFillCircle(64,58,3);
  } else {
    sFillRoundRect(lx-11,30,22,22,7);
    sFillRoundRect(rx-11,30,22,22,7);
  }
  if(animReacted && millis()-animReactTime<150) {
    oled.fillRect(0,0,128,64,1);
    oled.display(); return;
  }
  oled.display();
}
// ===== 5. SLEEP STORY =====
void animSleep() {
  oled.clearDisplay();
  unsigned long t = millis()-animStart;
  float c  = fmod((float)t/9000.0f, 1.0f);
  int   lx = 38, rx = 90;
  if(c < 0.18f) {
    float p    = c/0.18f;
    int   eyeH = constrain((int)(30-p*20), 4, 30);
    int   eyeY = 17;
    sFillRoundRect(lx-12,eyeY,24,eyeH,6);
    sFillRoundRect(rx-12,eyeY,24,eyeH,6);
    int mask = constrain((int)(p*eyeH*0.75f), 0, eyeH-2);
    if(mask>0) {
      sFillRect(lx-14, eyeY, 28, mask, 0);
      sFillRect(rx-14, eyeY, 28, mask, 0);
    }
    sHLine(lx-12, eyeY, 24); sHLine(rx-12, eyeY, 24);
  }
  else if(c < 0.72f) {
    float sp = (c-0.18f)/0.54f;
    int   bob= (int)(sin((float)t/900.0f)*1.5f);
    sHLine(lx-12,28+bob,24); sHLine(lx-12,29+bob,22); sHLine(lx-11,30+bob,20);
    sHLine(rx-12,28+bob,24); sHLine(rx-12,29+bob,22); sHLine(rx-11,30+bob,20);
    sDrawLine(50,48+bob,56,51+bob);
    sDrawLine(56,51+bob,64,52+bob);
    sDrawLine(64,52+bob,72,51+bob);
    sDrawLine(72,51+bob,78,48+bob);
    if(sp>0.04f) {
      float z1p = fmod(sp*2.2f, 1.0f);
      int   z1y = constrain(55-(int)(z1p*52), 2, 55);
      oled.setTextSize(1); oled.setCursor(96, z1y); oled.print("z");
    }
    if(sp>0.22f) {
      float z2p = fmod((sp-0.18f)*2.0f, 1.0f);
      int   z2y = constrain(55-(int)(z2p*52), 2, 50);
      oled.setTextSize(2); oled.setCursor(104, z2y); oled.print("z"); oled.setTextSize(1);
    }
    if(sp>0.42f) {
      float z3p = fmod((sp-0.38f)*2.0f, 1.0f);
      int   z3y = constrain(55-(int)(z3p*52), 2, 46);
      oled.setTextSize(2); oled.setCursor(114, z3y); oled.print("Z"); oled.setTextSize(1);
    }
  }
  else if(c < 0.82f) {
    float p    = (c-0.72f)/0.10f;
    int   eyeH = constrain((int)(p*36), 2, 36);
    int   eyeY = constrain(32-(int)(p*16), 14, 30);
    sFillRoundRect(lx-13, eyeY, 26, eyeH, 8);
    sFillRoundRect(rx-13, eyeY, 26, eyeH, 8);
  }
  else {
    int shk = (c<0.87f && (int)(t/75)%2==0) ? 2 : 0;
    sFillRoundRect(lx-13+shk, 16, 26, 34, 8);
    sFillRoundRect(rx-13-shk, 16, 26, 34, 8);
    sHLine(lx-10,11,20); sHLine(rx-10,11,20);
    oled.drawCircle(64,55,5,1); sFillCircle(64,55,3);
  }
  if(animReacted && millis()-animReactTime<1200) {
    oled.clearDisplay();
    sFillRoundRect(lx-13,16,26,34,8);
    sFillRoundRect(rx-13,16,26,34,8);
    sHLine(lx-10,11,20); sHLine(rx-10,11,20);
    oled.drawCircle(64,55,5,1); sFillCircle(64,55,3);
    oled.display(); return;
  }
  oled.display();
}
// ===== 6. PEEK =====
void animPeek() {
  oled.clearDisplay();
  unsigned long t = millis()-animStart;
  float cycle = fmod((float)t/5000.0f, 1.0f);
  int peekY;
  if(cycle<0.25f)      peekY=64-(int)(cycle/0.25f*44);
  else if(cycle<0.55f) peekY=20;
  else if(cycle<0.75f) peekY=20+(int)((cycle-0.55f)/0.2f*44);
  else                 peekY=64;
  peekY=constrain(peekY,18,64);
  if(peekY<64) sHLine(0,peekY,128);
  int lx=38, rx=90;
  int eyeBot=peekY-3;
  int eyeH=constrain(eyeBot-10,0,26);
  if(eyeH>3) {
    int eTop=eyeBot-eyeH;
    sFillRoundRect(lx-12,eTop,24,eyeH,constrain(eyeH/3,1,8));
    sFillRoundRect(rx-12,eTop,24,eyeH,constrain(eyeH/3,1,8));
    if(eyeH>14) {
      int br=map(eyeH,14,26,0,6);
      sHLine(lx-10,eTop-4-br,20); sHLine(lx-10,eTop-3-br,20);
      sHLine(rx-10,eTop-4-br,20); sHLine(rx-10,eTop-3-br,20);
    }
  }
  if(cycle>0.22f&&cycle<0.58f&&peekY<38) {
    sFillRoundRect(lx-18,peekY-2,14,8,3);
    sVLine(lx-16,peekY+6,4); sVLine(lx-13,peekY+6,5); sVLine(lx-10,peekY+6,4);
    sFillRoundRect(rx+4,peekY-2,14,8,3);
    sVLine(rx+6,peekY+6,4); sVLine(rx+9,peekY+6,5); sVLine(rx+12,peekY+6,4);
  }
  if(animReacted && millis()-animReactTime<1500) {
    oled.clearDisplay();
    sFillRoundRect(lx-13,12,26,32,8); sFillRoundRect(rx-13,12,26,32,8);
    sHLine(lx-10,6,20); sHLine(lx-10,7,20);
    sHLine(rx-10,6,20); sHLine(rx-10,7,20);
    oled.drawCircle(64,54,6,1); sFillCircle(64,54,4);
  }
  oled.display();
}
// ===== ANIMATION CONTROLLER =====
void pickNextAnim() {
  int prev=curAnim;
  while(curAnim==prev) curAnim=random(1,ANIM_COUNT+1);
  animStart=millis(); animDuration=random(15000,31000); animReacted=false;
}
void runAnim() {
  if(millis()-animStart>animDuration) pickNextAnim();
  switch(curAnim) {
    case 1: animAurora();    break;
    case 2: animWink();      break;
    case 3: animScan();      break;
    case 4: animLightning(); break;
    case 5: animSleep();     break;
    case 6: animPeek();      break;
    default: drawFace(mood); break;
  }
}
// ===== ARCADE =====
void playDino() {
  oled.clearDisplay(); oled.setCursor(45,30); oled.print("READY?"); oled.display(); safeDelay(1000);
  float y=40,dy=0,cx=128; int score=0; unsigned long lastTap=0;
  while(1) {
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    if(digitalRead(TOUCH_PIN)){delay(10);if(digitalRead(TOUCH_PIN)){
      while(digitalRead(TOUCH_PIN))yield();
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis();lastInteraction=millis(); if(y>=40)dy=-8.5;
    }}
    y+=dy;dy+=0.8;if(y>40)y=40;
    cx-=5.0;if(cx<-10){cx=128;score++;}
    if(cx<25&&cx>10&&y>30){
      if(score>hiScoreDino){prefs.begin("nivi",false);prefs.putInt("hdino",score);prefs.end();hiScoreDino=score;}
      oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
      oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreDino);
      oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
    }
    oled.clearDisplay();oled.drawFastHLine(0,60,128,1);
    sFillRect(10,(int)y,15,20);sFillRect((int)cx,45,8,15);
    oled.setCursor(0,0);oled.print(score);oled.display();safeDelay(15);
  }
}
void playSnake() {
  oled.clearDisplay();oled.setCursor(45,30);oled.print("READY?");oled.display();safeDelay(1000);
  int sx[40],sy[40];int len=3,dir=0,score=0;
  for(int i=0;i<len;i++){sx[i]=16-i;sy[i]=8;}
  int fx=random(1,31),fy=random(1,15);
  unsigned long lastMove=millis(),lastTap=0;
  while(1){
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    if(digitalRead(TOUCH_PIN)){delay(10);if(digitalRead(TOUCH_PIN)){
      while(digitalRead(TOUCH_PIN))yield();
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis();lastInteraction=millis();dir=(dir+1)%4;
    }}
    if(millis()-lastMove>280){
      for(int i=len-1;i>0;i--){sx[i]=sx[i-1];sy[i]=sy[i-1];}
      if(dir==0)sx[0]++;else if(dir==1)sy[0]++;else if(dir==2)sx[0]--;
      // BUG FIX 4: sy[0]-- was broken across lines as "sy[0]-\n;"
      else sy[0]--;
      bool dead=(sx[0]<0||sx[0]>31||sy[0]<0||sy[0]>15);
      for(int i=1;i<len;i++)if(sx[0]==sx[i]&&sy[0]==sy[i])dead=true;
      if(dead){
        if(score>hiScoreSnake){prefs.begin("nivi",false);prefs.putInt("hsnake",score);prefs.end();hiScoreSnake=score;}
        oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
        oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreSnake);
        oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
      }
      if(sx[0]==fx&&sy[0]==fy){if(len<40)len++;score++;fx=random(1,31);fy=random(1,15);}
      lastMove=millis();
    }
    oled.clearDisplay();
    sFillRect(fx*4,fy*4,4,4);
    for(int i=0;i<len;i++)sFillRect(sx[i]*4,sy[i]*4,4,4);
    oled.display();
  }
}
void playFlappy() {
  oled.clearDisplay();oled.setCursor(45,30);oled.print("READY?");oled.display();safeDelay(1000);
  float y=32,dy=0;int px=128,gapY=random(10,30),score=0;unsigned long lastTap=0;
  while(1){
    server.handleClient();
    if(ESP.getFreeHeap()<15000){curState=FACES;break;}
    if(millis()-lastInteraction>300000){curState=MAIN_MENU;break;}
    if(digitalRead(TOUCH_PIN)){delay(10);if(digitalRead(TOUCH_PIN)){
      while(digitalRead(TOUCH_PIN))yield();
      if(millis()-lastTap<200){curState=MAIN_MENU;break;}
      lastTap=millis();lastInteraction=millis();dy=-3.5;
    }}
    y+=dy;dy+=0.4;px-=3;
    if(px<-15){px=128;gapY=random(10,30);score++;}
    // BUG FIX 5: closing brace was misplaced — game-over block now properly enclosed
    if(y<0||y>60||(px<26&&px>5&&(y<gapY||y>gapY+32))){
      if(score>hiScoreFlappy){prefs.begin("nivi",false);prefs.putInt("hflappy",score);prefs.end();hiScoreFlappy=score;}
      oled.clearDisplay();oled.setCursor(35,20);oled.print("GAME OVER");
      oled.setCursor(30,35);oled.print("HI: ");oled.print(hiScoreFlappy);
      oled.display();safeDelay(2000);curState=ARCADE_MENU;break;
    }
    oled.clearDisplay();
    sFillRect(20,(int)y,6,6);
    sFillRect(px,0,15,gapY);
    sFillRect(px,gapY+32,15,constrain(64-(gapY+32),0,64));
    oled.setCursor(0,0);oled.print(score);oled.display();safeDelay(30);
  }
}
// ===== SETUP =====
void setup() {
  setCpuFrequencyMhz(160);
  pinMode(TOUCH_PIN,INPUT_PULLDOWN);
  SPI.begin(OLED_CLK,-1,OLED_MOSI,OLED_CS);
  if(!oled.begin(SSD1306_SWITCHCAPVCC,0x3C)){for(;;);}
  oled.setTextColor(1);
  showBootLogo();
  prefs.begin("nivi",true);
  ssid          = prefs.getString("s",    "");
  pass          = prefs.getString("p",    "");
  city          = prefs.getString("c",    "Guwahati");
  hiScoreDino   = prefs.getInt("hdino",   0);
  hiScoreSnake  = prefs.getInt("hsnake",  0);
  hiScoreFlappy = prefs.getInt("hflappy", 0);
  memoryMood    = prefs.getInt("memMood", 50);
  prefs.end();
  if(ssid=="") {
    curState=CONFIG;
    WiFi.softAP("Nivi-Setup");
    dnsServer.start(53,"*",WiFi.softAPIP());
  } else {
    WiFi.begin(ssid.c_str(),pass.c_str());
    // BUG FIX 2: string joined onto single line
    oled.clearDisplay();oled.setCursor(10,30);oled.print("Nivi is waking...");oled.display();
    unsigned long st=millis();
    while(WiFi.status()!=WL_CONNECTED&&millis()-st<7000) safeDelay(200);
    if(WiFi.status()==WL_CONNECTED) {
      configTime(19800,0,"in.pool.ntp.org","time.google.com");
      struct tm ti; int retry=0;
      while(!getLocalTime(&ti)&&retry<4){safeDelay(500);retry++;}
      checkCloudOTA();
    }
    safeDelay(200); curState=WAKE;
  }
  server.on("/",handleRoot);
  server.on("/s",handleSave);
  server.on("/m",handleMsg);
  server.begin();
}
// ===== LOOP =====
void loop() {
  if(curState==CONFIG) dnsServer.processNextRequest();
  server.handleClient();
  if(ssid!=""&&WiFi.status()!=WL_CONNECTED) {
    if(millis()-lastWifiRetry>20000) {
      WiFi.reconnect();
      lastWifiRetry=millis();
    }
  }
  if(curState!=CONFIG&&WiFi.status()==WL_CONNECTED&&millis()-lastWeather>600000) {
    fetchWeather(); lastWeather=millis();
  }
  static unsigned long lastSave=0;
  if(millis()-lastSave>180000) {
    prefs.begin("nivi",false);prefs.putInt("memMood",memoryMood);prefs.end();
    lastSave=millis();
  }
  static unsigned long lastBlink=0;
  static bool isBlinking=false;
  if(curState==FACES&&curAnim==0&&!isBeingTouched&&
     (mood==0||mood==5||mood==4||mood==6)) {
    if(isBlinking&&millis()-lastBlink>120){le.th=30;re.th=30;isBlinking=false;}
    if(!isBlinking&&millis()-lastBlink>random(2500,6000)){le.th=2;re.th=2;isBlinking=true;lastBlink=millis();}
  }
  // ===== TOUCH INPUT =====
  if(touchEnabled&&digitalRead(TOUCH_PIN)) {
    delay(10);
    if(!digitalRead(TOUCH_PIN)) return;
    unsigned long st=millis();
    isBeingTouched=true; lastInteraction=millis();
    if(curState==FACES&&curAnim==0){le.ty=40;re.ty=40;le.th=15;re.th=15;lookOffsetX=0;}
    bool longPetTriggered=false;
    while(digitalRead(TOUCH_PIN)) {
      server.handleClient();
      if(curState==FACES) {
        if(curAnim!=0) runAnim(); else drawFace(mood);
        delay(15);
        if(millis()-st>800&&!longPetTriggered) {
          curAnim=1; animStart=millis(); animDuration=3500; animReacted=false;
          affection=constrain(affection+15,0,100);
          boredom=0; energy=constrain(energy+10,0,100);
          memoryMood=constrain(memoryMood+5,10,90);
          mood=3;
          for(int i=0;i<3;i++) spawnParticle(random(30,90),random(10,30),0);
          longPetTriggered=true;
        }
      } else if(curState==RESET_PAGE&&millis()-st>5000) {
        prefs.begin("nivi",false);prefs.clear();prefs.end();
        // BUG FIX 2: string joined onto single line
        oled.clearDisplay();oled.setCursor(20,30);oled.print("RESET DONE");oled.display();
        safeDelay(1500);ESP.restart();
      }
      yield();
    }
    isBeingTouched=false; lastTapTime=millis();
    unsigned long dur=millis()-st;
    int clickType=1;
    if(dur>600) {
      clickType=3;
    } else {
      unsigned long wt=millis(); bool dbl=false;
      while(millis()-wt<250){
        if(digitalRead(TOUCH_PIN)){delay(10);if(digitalRead(TOUCH_PIN)){dbl=true;while(digitalRead(TOUCH_PIN))yield();break;}}
        yield();
      }
      clickType=dbl?2:1;
    }
    if(curState==FACES) {
      if(clickType==2) curState=MAIN_MENU;
      else if(clickType==1) {
        animReacted=true; animReactTime=millis();
        rapidTaps++;
        if(rapidTaps>=5)      {mood=7;reactionTimer=millis()+4000;rapidTaps=0;curAnim=0;}
        // BUG FIX 3: missing minus sign — was "constrain(affection\n10,...)" now "affection-10"
        else if(rapidTaps>=3) {mood=2;affection=constrain(affection-10,0,100);reactionTimer=millis()+3000;curAnim=0;}
        else                  {mood=13;reactionTimer=millis()+1500;}
      }
      else if(clickType==3&&!longPetTriggered) { if(curAnim!=0) pickNextAnim(); }
    }
    else if(curState==MAIN_MENU) {
      if(clickType==1) menuIdx=(menuIdx+1)%6;
      else if(clickType==2) {
        if(menuIdx==0)      curState=FACES;
        else if(menuIdx==1) curState=CLOCK;
        else if(menuIdx==2){curState=WEATHER;fetchWeather();}
        else if(menuIdx==3) curState=ARCADE_MENU;
        else if(menuIdx==4){curState=FOCUS;focusStart=millis();}
        else if(menuIdx==5) curState=RESET_PAGE;
      }
    }
    else if(curState==ARCADE_MENU) {
      if(clickType==1) arcadeIdx=(arcadeIdx+1)%4;
      else if(clickType==3) {
        if(arcadeIdx==0)      playDino();
        else if(arcadeIdx==1) playSnake();
        else if(arcadeIdx==2) playFlappy();
        else if(arcadeIdx==3) curState=MAIN_MENU;
      }
      else if(clickType==2) curState=MAIN_MENU;
    }
    else if(curState==CLOCK||curState==WEATHER||curState==FOCUS||curState==RESET_PAGE) {
      if(clickType==2||clickType==3) curState=MAIN_MENU;
    }
  } else {
    isBeingTouched=false;
    if(millis()-lastTapTime>1500&&rapidTaps>0&&mood!=2&&mood!=7) rapidTaps=0;
  }
  // ===== DISPLAY ROUTER =====
  switch(curState) {
    case WAKE: if(!hasWoken) playWakeAnimation(); break;
    case FACES:
      processAI();
      if(curAnim!=0) runAnim(); else drawFace(mood);
      safeDelay(15); break;
    case MAIN_MENU: {
      oled.clearDisplay();
      const char* m[]={"Bio-Face","Time","Weather","Arcade","Focus","Reset"};
      for(int i=0;i<6;i++){
        int y=6+i*10;
        if(i==menuIdx){sFillRect(0,y-1,128,10);oled.setTextColor(0);}
        oled.setCursor(5,y);oled.print(m[i]);oled.setTextColor(1);
      }
      oled.display(); break;
    }
    case ARCADE_MENU: {
      oled.clearDisplay();
      const char* g[]={"Dino","Snake","Flappy","Back"};
      for(int i=0;i<4;i++){
        int y=12+i*13;
        if(i==arcadeIdx){sFillRect(0,y-1,128,11);oled.setTextColor(0);}
        oled.setCursor(10,y);oled.print(g[i]);oled.setTextColor(1);
      }
      oled.display(); break;
    }
    case CLOCK: {
      struct tm ti; getLocalTime(&ti);
      oled.clearDisplay();
      int hr=ti.tm_hour%12; if(hr==0) hr=12;
      oled.setTextSize(2); oled.setCursor(8,20);
      oled.printf("%02d:%02d:%02d",hr,ti.tm_min,ti.tm_sec);
      oled.setTextSize(1);
      char ds[20]; strftime(ds,sizeof(ds),"%d-%b-%Y",&ti);
      oled.setCursor(31,42);oled.print(ds);
      oled.display(); break;
    }
    case WEATHER: {
      oled.clearDisplay(); oled.setTextSize(3);
      oled.setCursor(10,20); oled.print(tempC);
      oled.drawCircle(48,22,4,1); oled.drawCircle(48,22,3,1);
      oled.drawFastVLine(64,15,34,1);
      String w=weatherDesc; w.toLowerCase();
      if(w.indexOf("cloud")>=0){
        sFillCircle(85,25,5);sFillCircle(95,22,7);sFillCircle(105,25,5);sFillRect(85,25,20,6);
      } else if(w.indexOf("rain")>=0){
        oled.drawCircle(95,20,6,1);
        int dy2=30+(millis()%1000)/100;
        sDrawLine(90,constrain(dy2,0,60),88,constrain(dy2+3,0,63));
        sDrawLine(100,constrain(dy2,0,60),98,constrain(dy2+3,0,63));
      } else {
        sFillCircle(95,24,6);
        if(millis()%1000>500){sHLine(85,24,4);sHLine(103,24,4);sVLine(95,14,4);sVLine(95,32,4);}
      }
      oled.setTextSize(1); oled.setCursor(68,40);
      oled.print(weatherDesc.substring(0,min((unsigned int)weatherDesc.length(),(unsigned int)9)));
      oled.display(); break;
    }
    case FOCUS: {
      long elapsed=(millis()-focusStart)/1000;
      long rem=(25*60)-elapsed;
      if(rem<=0){
        affection=constrain(affection+10,0,100); energy=constrain(energy+10,0,100);
        mood=3;le.ty=32;re.ty=32;le.th=24;re.th=24;
        reactionTimer=millis()+3000; curState=FACES; break;
      }
      oled.clearDisplay(); oled.setTextSize(2); oled.setCursor(30,20);
      oled.printf("%02d:%02d",(int)(rem/60),(int)(rem%60));
      oled.drawRoundRect(14,45,100,8,3,1);
      int pw=constrain((int)map(elapsed,0,25*60,0,96),0,96);
      sFillRoundRect(16,47,pw,4,2);
      oled.setTextSize(1); oled.display(); break;
    }
    case CONFIG:
      // BUG FIX 2: string joined onto single line
      oled.clearDisplay();oled.setCursor(10,25);oled.print("WIFI: Nivi Setup");oled.display(); break;
    case NOTIFY: {
      if(notifyStart==0) notifyStart=millis();
      oled.clearDisplay();oled.drawRect(0,0,128,64,1);
      oled.setCursor(5,10);oled.print(remoteMsg);oled.display();
      if(millis()-notifyStart>4000){notifyStart=0;curState=FACES;}
      break;
    }
    case RESET_PAGE:
      // BUG FIX 2: string joined onto single line
      oled.clearDisplay();oled.setCursor(10,30);oled.print("HOLD 5S TO WIPE");oled.display(); break;
    default: break;
  }
  if(ESP.getFreeHeap()<12000) ESP.restart();
}
