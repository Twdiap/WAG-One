/*
 * WAG ONE - Waveshare ESP32-C3-LCD-0.71 (GC9D01, 160x160)
 * v1.0.0
 *
 * Behavior:
 * 1. On boot -> show "WAG One" centered on screen
 * 2. Try to join the TANK2 hotspot (2 min timeout)
 * 3. If connected -> show IP on screen, start webserver
 *    -> from phone: upload several GIFs, pick rotation,
 *       storage bar, "Clear all" button, "Turn off Wi-Fi" button
 * 4. If not connected after 2 min (or after "Turn off Wi-Fi" click) ->
 *    kill Wi-Fi and loop-play the GIFs stored in flash
 * 5. Physical BOOT button (during GIF playback) -> restarts
 *    Wi-Fi / webserver so GIFs can be edited again
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// ───────────────────────────── CONFIG ─────────────────────────────
#define VERSION           "v1.0.0"
#define BTN_BOOT          9
#define WIFI_SSID         "REPLACE_SSID"
#define WIFI_PASS         "REPLACE_PSSWD"
#define WIFI_TIMEOUT_MS   120000UL     // give up after 2 min
#define MAX_GIFS          20
#define FLASH_TOTAL       (4UL * 1024 * 1024)
#define FLASH_RESERVED    (300UL * 1024)   // leave room for FS overhead / firmware bits
#define FLASH_USABLE      (FLASH_TOTAL - FLASH_RESERVED)
#define GIF_DURATION_MS   4000UL       // how long each gif stays on screen

String gifPath(int i) { return "/gif/" + String(i) + ".gif"; }

// ───────────────────────────── TFT + LVGL ─────────────────────────────
TFT_eSPI tft = TFT_eSPI(160, 160);
static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvgl_buf[160 * 20];      // partial buffer, 20 lines is plenty for this screen
static lv_obj_t* gif_obj    = nullptr;
static lv_obj_t* label_obj  = nullptr;
static int       gif_cur    = -1;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  tft.pushColors(&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// LVGL needs its own tiny FS driver to read gifs straight from LittleFS
static void* lfs_open(lv_fs_drv_t*, const char* p, lv_fs_mode_t m) {
  File* f = new File(LittleFS.open(p, m == LV_FS_MODE_WR ? "w" : "r"));
  if (!f || !(*f)) { delete f; return nullptr; }
  return f;
}
static lv_fs_res_t lfs_close(lv_fs_drv_t*, void* fp) { ((File*)fp)->close(); delete (File*)fp; return LV_FS_RES_OK; }
static lv_fs_res_t lfs_read(lv_fs_drv_t*, void* fp, void* buf, uint32_t btr, uint32_t* br) { *br = ((File*)fp)->read((uint8_t*)buf, btr); return LV_FS_RES_OK; }
static lv_fs_res_t lfs_seek(lv_fs_drv_t*, void* fp, uint32_t pos, lv_fs_whence_t w) { SeekMode sm = w==LV_FS_SEEK_CUR?SeekCur:w==LV_FS_SEEK_END?SeekEnd:SeekSet; ((File*)fp)->seek(pos, sm); return LV_FS_RES_OK; }
static lv_fs_res_t lfs_tell(lv_fs_drv_t*, void* fp, uint32_t* pos) { *pos = ((File*)fp)->position(); return LV_FS_RES_OK; }

void initLVGLFS() {
  static lv_fs_drv_t drv; lv_fs_drv_init(&drv);
  drv.letter = 'L'; drv.open_cb = lfs_open; drv.close_cb = lfs_close; drv.read_cb = lfs_read; drv.seek_cb = lfs_seek; drv.tell_cb = lfs_tell;
  lv_fs_drv_register(&drv);
}
void initLVGL() {
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvgl_buf, NULL, 160 * 20);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd);
  dd.hor_res = 160; dd.ver_res = 160; dd.flush_cb = my_disp_flush; dd.draw_buf = &draw_buf;
  lv_disp_drv_register(&dd);
  initLVGLFS();
}

void clearScreenBg() {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

void showLabel(const char* txt) {
  // wipe whatever's on screen (gif or old label) before drawing text
  if (gif_obj) { lv_obj_del(gif_obj); gif_obj = nullptr; gif_cur = -1; }
  if (label_obj) { lv_obj_del(label_obj); label_obj = nullptr; }
  clearScreenBg();
  label_obj = lv_label_create(lv_scr_act());
  lv_label_set_long_mode(label_obj, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_obj, 140);
  lv_obj_set_style_text_align(label_obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label_obj, lv_color_hex(0x00FFFF), 0);
  lv_label_set_text(label_obj, txt);
  lv_obj_align(label_obj, LV_ALIGN_CENTER, 0, 0);
  lv_timer_handler();   // force an immediate redraw, don't wait for the next loop
}

// ───────────────────────────── Preferences / rotation ─────────────────────────────
Preferences prefs;
uint8_t rotationMode = 0; // 0 = normal, 1 = 90deg

void loadPrefs() {
  prefs.begin("wagone", false);
  rotationMode = prefs.getUChar("rot", 0);
  prefs.end();
}
void savePrefs() {
  prefs.begin("wagone", false);
  prefs.putUChar("rot", rotationMode);
  prefs.end();
}
void applyRotation() {
  // screen is a square 160x160, so setRotation() alone is enough, no buffer resize needed
  tft.setRotation(rotationMode == 1 ? 1 : 0);
}

// ───────────────────────────── GIF handling ─────────────────────────────
int gifList[MAX_GIFS];
int gifCount = 0;
int playIdx = 0;
uint32_t slotStart = 0;

size_t usedFlash() {
  size_t total = 0;
  File dir = LittleFS.open("/gif");
  if (!dir || !dir.isDirectory()) return 0;
  File f = dir.openNextFile();
  while (f) {
    total += f.size();
    f = dir.openNextFile();
  }
  return total;
}
size_t freeFlash() {
  size_t u = usedFlash();
  return u < FLASH_USABLE ? FLASH_USABLE - u : 0;
}

void refreshGifList() {
  gifCount = 0;
  for (int i = 0; i < MAX_GIFS; i++) {
    if (LittleFS.exists(gifPath(i))) {
      gifList[gifCount++] = i;
    }
  }
}
int nextFreeSlot() {
  for (int i = 0; i < MAX_GIFS; i++) {
    if (!LittleFS.exists(gifPath(i))) return i;
  }
  return -1;
}

void playGifAt(int idx) {
  if (idx < 0 || idx >= gifCount) return;
  int slot = gifList[idx];
  if (gif_cur == slot) return;   // already showing this one, nothing to do
  if (label_obj) { lv_obj_del(label_obj); label_obj = nullptr; }
  if (gif_obj) { lv_obj_del(gif_obj); gif_obj = nullptr; gif_cur = -1; lv_timer_handler(); delay(10); }
  clearScreenBg();
  String path = "L:" + gifPath(slot);
  gif_obj = lv_gif_create(lv_scr_act());
  lv_obj_align(gif_obj, LV_ALIGN_CENTER, 0, 0);
  lv_gif_set_src(gif_obj, path.c_str());
  gif_cur = slot;
  Serial.printf("[play] slot %d\n", slot);
}

void startPlayback() {
  refreshGifList();
  applyRotation();
  playIdx = 0;
  slotStart = millis();
  if (gifCount > 0) playGifAt(0);
  else showLabel("No GIF\nPress BOOT to\nconfigure");
}

void tickPlayback() {
  lv_timer_handler();
  if (gifCount <= 1) return;   // nothing to rotate between
  if (millis() - slotStart >= GIF_DURATION_MS) {
    playIdx = (playIdx + 1) % gifCount;
    slotStart = millis();
    playGifAt(playIdx);
  }
}

// ───────────────────────────── Global state ─────────────────────────────
enum AppMode { MODE_BOOT, MODE_CONNECTING, MODE_PORTAL, MODE_PLAY };
AppMode mode = MODE_BOOT;
unsigned long wifiAttemptStart = 0;
bool wifiOffRequested = false;

// ───────────────────────────── Upload state ─────────────────────────────
struct UpState {
  int  slot = -1;
  File file;
  bool ok = false;
  size_t bytes = 0;
  void reset() { if (file) file.close(); slot = -1; ok = false; bytes = 0; }
} up;

// ───────────────────────────── HTML ─────────────────────────────
const char HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>WAG ONE</title>
<style>
:root{--c:#0ff;--bg:#060606;--s:#0e0e0e;--t:#e8e8e8;--m:#555;--ok:#0fa;--err:#f55;--warn:#fa0}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--t);font-family:-apple-system,sans-serif;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 60px}
.title{font-size:1.6rem;font-weight:800;letter-spacing:.15em;color:var(--c);margin-bottom:2px}
.ver{font-size:.6rem;color:var(--m);margin-bottom:20px}
.card{width:100%;max-width:420px;background:var(--s);border:1px solid #181818;border-radius:12px;padding:16px;margin-bottom:12px}
.ct{font-size:.62rem;text-transform:uppercase;letter-spacing:.12em;color:var(--m);font-weight:600;margin-bottom:10px}
.storbg{background:#111;border-radius:99px;height:8px;overflow:hidden}
.storf{background:var(--c);height:100%;width:0%;border-radius:99px;transition:width .3s}
.storf.warn{background:var(--warn)}.storf.full{background:var(--err)}
.stortxt{font-size:.62rem;color:var(--m);margin-top:6px;text-align:right}
.rot{display:flex;gap:8px}
.rotbtn{flex:1;padding:12px;border-radius:8px;border:1.5px solid #242424;background:none;color:var(--t);font-size:.8rem;cursor:pointer}
.rotbtn.active{border-color:var(--c);color:var(--c);background:#001818}
.uparea{border:2px dashed #242424;border-radius:10px;padding:22px;text-align:center;cursor:pointer}
.uparea:hover{border-color:var(--c)}
.uparea input{display:none}
.uplbl{font-size:.8rem;color:var(--m)}
.filelist{margin-top:10px;display:flex;flex-direction:column;gap:5px}
.fitem{display:flex;justify-content:space-between;align-items:center;background:#111;padding:6px 10px;border-radius:6px;font-size:.68rem}
.fname{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:220px}
.fstatus{flex-shrink:0;margin-left:8px}
.fstatus.ok{color:var(--ok)}.fstatus.err{color:var(--err)}.fstatus.ing{color:var(--warn)}
.btn{width:100%;border:none;border-radius:9px;padding:13px;font-size:.85rem;font-weight:700;cursor:pointer;margin-top:8px}
.bsend{background:#001818;color:var(--c);border:1.5px solid #003333}
.bclear{background:#180000;color:var(--err);border:1.5px solid #330000}
.boff{background:#001800;color:var(--ok);border:1.5px solid #003300}
.btn:disabled{opacity:.3}
</style></head><body>
<div class='title'>WAG ONE</div>
<div class='ver'>%VERSION%</div>

<div class='card'>
  <div class='ct'>GIF flash storage</div>
  <div class='storbg'><div class='storf' id='sf'></div></div>
  <div class='stortxt' id='sv'></div>
</div>

<div class='card'>
  <div class='ct'>Screen orientation</div>
  <div class='rot'>
    <button class='rotbtn' id='r0' onclick='setRot(0)'>Normal</button>
    <button class='rotbtn' id='r1' onclick='setRot(1)'>Rotate 90°</button>
  </div>
</div>

<div class='card'>
  <div class='ct'>Add GIFs</div>
  <label class='uparea' id='uplabel'>
    <div class='uplbl'>Tap to pick one or more GIFs</div>
    <input type='file' id='finput' accept='.gif' multiple onchange='filesChosen(this.files)'>
  </label>
  <div class='filelist' id='flist'></div>
  <button class='btn bsend' id='bsend' onclick='sendAll()' disabled>Upload</button>
</div>

<div class='card' style='display:flex;flex-direction:column;gap:0'>
  <button class='btn bclear' onclick='clearAll()'>Delete all GIFs</button>
  <button class='btn boff' onclick='wifiOff()'>Turn off Wi-Fi and play GIFs</button>
</div>

<script>
var TOTB = %TOTB%;
var chosenFiles = [];

function fmt(b){return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(2)+' MB';}

function refresh(){
  fetch('/list').then(r=>r.json()).then(function(d){
    var p = Math.min(100, d.used/TOTB*100);
    var sf = document.getElementById('sf');
    sf.style.width = p+'%';
    sf.className = 'storf' + (p>90?' full':p>70?' warn':'');
    document.getElementById('sv').textContent = fmt(d.used)+' / '+fmt(TOTB)+' - '+d.count+' gif(s)';
    document.getElementById('r0').className = 'rotbtn' + (d.rot==0?' active':'');
    document.getElementById('r1').className = 'rotbtn' + (d.rot==1?' active':'');
  }).catch(()=>{});
}
refresh();

function setRot(v){
  fetch('/setrot',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'v='+v})
    .then(()=>refresh());
}

function filesChosen(files){
  chosenFiles = [];
  var flist = document.getElementById('flist');
  flist.innerHTML = '';
  for (var i=0; i<files.length; i++){
    var f = files[i];
    if (!f.name.toLowerCase().endsWith('.gif')) continue;
    chosenFiles.push(f);
    var row = document.createElement('div');
    row.className = 'fitem';
    row.id = 'fi'+i;
    row.innerHTML = '<span class="fname">'+f.name+'</span><span class="fstatus" id="fs'+i+'">'+fmt(f.size)+'</span>';
    flist.appendChild(row);
  }
  document.getElementById('bsend').disabled = chosenFiles.length === 0;
}

function sendOne(i){
  return new Promise(function(resolve){
    var f = chosenFiles[i];
    var st = document.getElementById('fs'+i);
    if (st){ st.textContent = 'Uploading...'; st.className = 'fstatus ing'; }
    var fd = new FormData();
    fd.append('gif', f);
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload');
    xhr.onload = function(){
      if (xhr.status === 200){
        if (st){ st.textContent = 'OK'; st.className = 'fstatus ok'; }
      } else {
        if (st){ st.textContent = 'Error'; st.className = 'fstatus err'; }
      }
      resolve();
    };
    xhr.onerror = function(){
      if (st){ st.textContent = 'Network error'; st.className = 'fstatus err'; }
      resolve();
    };
    xhr.send(fd);
  });
}

async function sendAll(){
  document.getElementById('bsend').disabled = true;
  for (var i=0; i<chosenFiles.length; i++){
    await sendOne(i);
  }
  setTimeout(function(){
    chosenFiles = [];
    document.getElementById('finput').value = '';
    refresh();
  }, 500);
}

function clearAll(){
  if (!confirm('Delete ALL GIFs?')) return;
  fetch('/clearall',{method:'POST'}).then(()=>{
    document.getElementById('flist').innerHTML = '';
    chosenFiles = [];
    refresh();
  });
}

function wifiOff(){
  if (!confirm('Turn off Wi-Fi and start playback?')) return;
  fetch('/wifioff',{method:'POST'}).then(()=>{
    document.body.innerHTML = '<div style="padding:40px;text-align:center;color:#0ff;font-size:1rem">Wi-Fi off.<br>Playing GIFs...</div>';
  });
}
</script>
</body></html>
)HTMLPAGE";

WebServer server(80);

String htmlWithVars() {
  String s = FPSTR(HTML);
  s.replace("%VERSION%", VERSION);
  s.replace("%TOTB%", String(FLASH_USABLE));
  return s;
}

// ───────────────────────────── HTTP handlers ─────────────────────────────
void hRoot() { server.send(200, "text/html", htmlWithVars()); }

void hList() {
  refreshGifList();
  String j = "{\"used\":" + String(usedFlash());
  j += ",\"free\":" + String(freeFlash());
  j += ",\"count\":" + String(gifCount);
  j += ",\"rot\":" + String(rotationMode);
  j += "}";
  server.send(200, "application/json", j);
}

void hSetRot() {
  if (server.hasArg("v")) {
    rotationMode = server.arg("v").toInt() == 1 ? 1 : 0;
    savePrefs();
  }
  server.send(200, "text/plain", "OK");
}

void hClearAll() {
  for (int i = 0; i < MAX_GIFS; i++) {
    if (LittleFS.exists(gifPath(i))) LittleFS.remove(gifPath(i));
  }
  server.send(200, "text/plain", "OK");
}

void hWifiOff() {
  server.send(200, "text/plain", "OK");
  wifiOffRequested = true;   // actual shutdown happens in loop(), not here
}

void hUploadDone() {
  if (up.ok) server.send(200, "text/plain", "OK");
  else server.send(500, "text/plain", "Upload error");
  up.reset();
}

void hUploadBody() {
  HTTPUpload& u = server.upload();

  if (u.status == UPLOAD_FILE_START) {
    up.reset();
    String fn = u.filename;
    fn.toLowerCase();
    if (!fn.endsWith(".gif")) { Serial.println("[up] not a gif"); return; }

    int slot = nextFreeSlot();
    if (slot < 0) { Serial.println("[up] no free slot"); return; }
    if (freeFlash() == 0) { Serial.println("[up] flash full"); return; }

    up.slot = slot;
    up.file = LittleFS.open(gifPath(slot), "w");
    if (!up.file) { up.slot = -1; return; }
    Serial.printf("[up] start slot%d\n", slot);

  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (up.slot < 0 || !up.file) return;
    up.bytes += u.currentSize;
    if (up.bytes > freeFlash() + u.currentSize) {
      // ran out of space mid-upload, bail and clean up the partial file
      up.file.close();
      LittleFS.remove(gifPath(up.slot));
      Serial.println("[up] out of space");
      up.slot = -1;
      return;
    }
    size_t wr = up.file.write(u.buf, u.currentSize);
    if (wr != u.currentSize) {
      up.file.close();
      LittleFS.remove(gifPath(up.slot));
      Serial.println("[up] write error");
      up.slot = -1;
    }

  } else if (u.status == UPLOAD_FILE_END) {
    if (up.file) up.file.close();
    if (up.slot >= 0 && up.bytes > 0) {
      up.ok = true;
      Serial.printf("[up] done slot%d %u B\n", up.slot, (unsigned)up.bytes);
    } else {
      up.ok = false;
    }

  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (up.file) up.file.close();
    if (up.slot >= 0) LittleFS.remove(gifPath(up.slot));
    up.ok = false;
    Serial.println("[up] aborted");
  }
}

// ───────────────────────────── Wi-Fi / portal ─────────────────────────────
void startServerRoutes() {
  server.on("/", HTTP_GET, hRoot);
  server.on("/list", HTTP_GET, hList);
  server.on("/setrot", HTTP_POST, hSetRot);
  server.on("/clearall", HTTP_POST, hClearAll);
  server.on("/wifioff", HTTP_POST, hWifiOff);
  server.on("/upload", HTTP_POST, hUploadDone, hUploadBody);
  server.begin();
}

void stopWifiFully() {
  // belt and suspenders: also kill BT/radio to save power once we're done with Wi-Fi
  server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  Serial.println("[wifi] off");
}

void beginWifiAttempt() {
  mode = MODE_CONNECTING;
  wifiOffRequested = false;
  showLabel("WAG One\n\nConnecting to\nTANK2...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiAttemptStart = millis();
  Serial.printf("[wifi] attempting to join %s\n", WIFI_SSID);
}

// ───────────────────────────── BOOT button ─────────────────────────────
bool btnLastState = HIGH;
unsigned long btnLastChange = 0;

bool btnPressed() {
  // quick and dirty debounce, good enough for a single button
  bool cur = digitalRead(BTN_BOOT);
  if (cur != btnLastState) {
    btnLastChange = millis();
    btnLastState = cur;
  }
  if (cur == LOW && millis() - btnLastChange > 40 && millis() - btnLastChange < 200) {
    return true;
  }
  return false;
}

// ───────────────────────────── Setup / loop ─────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(150);
  pinMode(BTN_BOOT, INPUT_PULLUP);

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS ERR");
    while (1) delay(1000);
  }
  if (!LittleFS.exists("/gif")) LittleFS.mkdir("/gif");

  loadPrefs();
  initLVGL();
  clearScreenBg();

  beginWifiAttempt();
}

void loop() {
  lv_timer_handler();

  if (mode == MODE_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      mode = MODE_PORTAL;
      String ip = WiFi.localIP().toString();
      String msg = "WAG One\n\nConnected!\n\nOpen:\nhttp://" + ip + "/";
      showLabel(msg.c_str());
      startServerRoutes();
      Serial.printf("[wifi] connected, IP=%s\n", ip.c_str());
    } else if (millis() - wifiAttemptStart >= WIFI_TIMEOUT_MS) {
      Serial.println("[wifi] timeout, giving up");
      stopWifiFully();
      mode = MODE_PLAY;
      startPlayback();
    }
  }
  else if (mode == MODE_PORTAL) {
    server.handleClient();
    if (wifiOffRequested) {
      stopWifiFully();
      mode = MODE_PLAY;
      startPlayback();
    }
  }
  else if (mode == MODE_PLAY) {
    tickPlayback();
    if (btnPressed()) {
      Serial.println("[btn] BOOT pressed -> restarting Wi-Fi");
      beginWifiAttempt();
    }
  }

  delay(4);
}
