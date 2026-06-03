#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include "RTClib.h"
#include "LittleFS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ── Config ────────────────────────────────────────────
#define I2C_SDA      21
#define I2C_SCL      22
#define LOG_FILE     "/sensor_log.csv"
#define LOG_INTERVAL 30000
#define DEVICE_NAME  "PalletSensor"
#define WIFI_PASS    "pallet123"

// ── Globals ───────────────────────────────────────────
Adafruit_BME280 bme;
RTC_DS3231 rtc;
WebServer server(80);
bool wifiMode = false;
unsigned long lastLogTime = 0;
int readingNumber = 0;

// ── Dashboard HTML ─────────────────────────────────────
String buildDashboard() {
  String csv = "";
  File f = LittleFS.open(LOG_FILE, "r");
  if (f) { while (f.available()) csv += (char)f.read(); f.close(); }
  csv.replace("`", "\\`");

  String h = "<!DOCTYPE html><html><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>PalletSensor</title><style>";
  h += "*{box-sizing:border-box;margin:0;padding:0}";
  h += "body{font-family:-apple-system,sans-serif;background:#121212;padding:16px;color:#e0e0e0}";
  h += "h1{font-size:18px;font-weight:600;margin-bottom:16px;color:#ffffff}";
  h += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}";
  h += ".chart-grid{display:grid;grid-template-columns:repeat(auto-fit, minmax(280px, 1fr));gap:10px;margin-bottom:12px}";
  h += ".card{background:#1e1e1e;border-radius:10px;padding:14px;box-shadow:0 1px 4px rgba(0,0,0,.2)}";
  h += ".val{font-size:28px;font-weight:700;color:#4da3ff}";
  h += ".lbl{font-size:11px;color:#888;margin-top:4px}";
  h += ".chart-card{background:#1e1e1e;border-radius:10px;padding:16px;box-shadow:0 1px 4px rgba(0,0,0,.2)}";
  h += ".ctitle{font-size:10px;font-weight:600;letter-spacing:.8px;color:#888;margin-bottom:10px}";
  h += ".breach{color:#ff453a!important}";
  h += ".btn{display:block;padding:14px;margin-bottom:10px;color:#fff;font-weight:600;";
  h += "border-radius:10px;text-align:center;text-decoration:none;font-size:15px;cursor:pointer;border:none;width:100%}";
  h += ".blue{background:#0a84ff}.red{background:#ff453a}.purple{background:#bf5af2}";
  h += ".note{font-size:11px;color:#888;text-align:center;margin-top:4px;line-height:1.6}";
  h += "</style></head><body>";

  h += "<h1>📦 PalletSensor</h1>";

  h += "<div class='grid'>";
  h += "<div class='card'><div class='val' id='vAvgTemp'>—</div><div class='lbl'>Avg Temp °C</div></div>";
  h += "<div class='card'><div class='val' id='vMaxTemp'>—</div><div class='lbl'>Max Temp °C</div></div>";
  h += "<div class='card'><div class='val' id='vAvgHum'>—</div><div class='lbl'>Avg Humidity %</div></div>";
  h += "<div class='card'><div class='val' id='vAvgPres'>—</div><div class='lbl'>Avg Pressure hPa</div></div>";
  h += "</div>";

  h += "<div class='chart-grid'>";
  h += "<div class='chart-card'><div class='ctitle'>TEMPERATURE °C</div>";
  h += "<svg id='chart1' viewBox='0 0 320 120' xmlns='http://www.w3.org/2000/svg' style='width:100%'></svg></div>";
  h += "<div class='chart-card'><div class='ctitle'>HUMIDITY %</div>";
  h += "<svg id='chart2' viewBox='0 0 320 120' xmlns='http://www.w3.org/2000/svg' style='width:100%'></svg></div>";
  h += "</div>";

  h += "<div id='breaches-container' class='card' style='margin-bottom:12px; display:block; border-left: 4px solid #ff453a;'>";
  h += "<h3 style='font-size:14px; margin-bottom:8px; color:#ff453a;'>⚠ Breach Alerts</h3>";
  h += "<ul id='breach-list' style='list-style:none; font-size:12px; color:#ccc;'></ul>";
  h += "</div>";

  h += "<div class='card' style='margin-bottom:12px;display:flex;justify-content:space-between;align-items:center'>";
  h += "<span style='font-size:13px;color:#888'>Total readings</span>";
  h += "<span class='val' id='vCount' style='font-size:20px;color:#e0e0e0'>—</span>";
  h += "</div>";

  // Action buttons
  h += "<button id='syncBtn' onclick='syncToCloud()' class='btn purple'>☁️ Sync Data to Cloud</button>";
  h += "<a href='/log.csv' class='btn blue'>⬇ Download Local CSV</a>";
  h += "<a href='/confirm-wipe' class='btn red'>⚠ Wipe Local Log</a>";
  h += "<p class='note'>Ensure cellular data is active before syncing.</p>";

  h += "<script>const raw=`" + csv + "`;";
  h += R"(
function drawChart(id, vals, color) {
  const svg = document.getElementById(id);
  if (!svg || vals.length < 2) return;
  const W=320,H=120,pl=32,pr=8,pt=8,pb=18;
  const cW=W-pl-pr, cH=H-pt-pb;
  const mn=Math.min(...vals), mx=Math.max(...vals);
  const rng=mx-mn||0.1;
  const xs=i=>pl+(i/(vals.length-1))*cW;
  const ys=v=>pt+cH-((v-mn)/rng)*cH;

  [mn, mn+rng*0.5, mx].forEach((v,i)=>{
    const y = pt + cH*(1-i*0.5);
    const gl = document.createElementNS('http://www.w3.org/2000/svg','line');
    gl.setAttribute('x1',pl); gl.setAttribute('x2',W-pr);
    gl.setAttribute('y1',y); gl.setAttribute('y2',y);
    gl.setAttribute('stroke','#333333'); gl.setAttribute('stroke-width','1');
    svg.appendChild(gl);
    const gt = document.createElementNS('http://www.w3.org/2000/svg','text');
    gt.setAttribute('x',pl-4); gt.setAttribute('y',y+3);
    gt.setAttribute('text-anchor','end');
    gt.setAttribute('font-size','8'); gt.setAttribute('fill','#888');
    gt.textContent = v.toFixed(1);
    svg.appendChild(gt);
  });

  const areaD = `M${xs(0)},${ys(vals[0])}` +
    vals.map((v,i)=>`L${xs(i).toFixed(1)},${ys(v).toFixed(1)}`).join('') +
    `L${xs(vals.length-1)},${pt+cH} L${xs(0)},${pt+cH}Z`;
  const area = document.createElementNS('http://www.w3.org/2000/svg','path');
  area.setAttribute('d',areaD);
  area.setAttribute('fill',color); area.setAttribute('fill-opacity','0.15');
  svg.appendChild(area);

  const lineD = vals.map((v,i)=>(i?'L':'M')+xs(i).toFixed(1)+','+ys(v).toFixed(1)).join('');
  const line = document.createElementNS('http://www.w3.org/2000/svg','path');
  line.setAttribute('d',lineD); line.setAttribute('fill','none');
  line.setAttribute('stroke',color); line.setAttribute('stroke-width','2');
  line.setAttribute('stroke-linejoin','round'); line.setAttribute('stroke-linecap','round');
  svg.appendChild(line);

  if (vals.length <= 48) {
    vals.forEach((v,i)=>{
      const c = document.createElementNS('http://www.w3.org/2000/svg','circle');
      c.setAttribute('cx',xs(i)); c.setAttribute('cy',ys(v));
      c.setAttribute('r','2.5'); c.setAttribute('fill',color);
      svg.appendChild(c);
    });
  }

  const xlabels = [{i:0, anchor:'start'}, {i:vals.length-1, anchor:'end'}];
  xlabels.forEach(({i,anchor})=>{
    if(timestamps[i]){
      const t=document.createElementNS('http://www.w3.org/2000/svg','text');
      t.setAttribute('x',xs(i)); t.setAttribute('y',H-2);
      t.setAttribute('text-anchor',anchor);
      t.setAttribute('font-size','7'); t.setAttribute('fill','#888');
      const ts=timestamps[i].toString();
      t.textContent=ts.length>=16?ts.slice(11,16):ts;
      svg.appendChild(t);
    }
  });
}

async function syncToCloud() {
  const btn = document.getElementById('syncBtn');
  
  localStorage.setItem('palletData', raw);
  
  btn.textContent = '✅ Queued! Turn off Wi-Fi to Upload';
  btn.style.background = '#e5a00d'; 
  
  checkInternetAndUpload();
}

async function checkInternetAndUpload() {
  const btn = document.getElementById('syncBtn');
  const storedData = localStorage.getItem('palletData');

  if (!storedData) return; 

  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 3000);

    const response = await fetch('https://pallet-backend-w971.onrender.com/upload', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: storedData,
      signal: controller.signal
    });

    clearTimeout(timeoutId);

    if (response.ok) {
      btn.textContent = '☁️ Synced to Cloud Successfully!';
      btn.style.background = '#32d74b'; 
      localStorage.removeItem('palletData'); 
    } else {
      throw new Error('Server error');
    }
  } catch (err) {
    setTimeout(checkInternetAndUpload, 3000);
  }
}

if(localStorage.getItem('palletData')) {
  document.getElementById('syncBtn').textContent = '⏳ Resuming Upload...';
  checkInternetAndUpload();
}

const lines = raw.trim().split('\n').filter(l=>l.trim());
const timestamps = [], temps = [], hums = [], press = [];

if (lines.length > 1) {
  lines.slice(1).forEach(l=>{
    const r = l.split(',');
    if (r.length >= 5) {
      timestamps.push(r[1]);
      const t = parseFloat(r[2]);
      const h = parseFloat(r[3]);
      const p = parseFloat(r[4]);
      if (!isNaN(t)) temps.push(t);
      if (!isNaN(h)) hums.push(h);
      if (!isNaN(p)) press.push(p);
    }
  });
}

if (temps.length) {
  const avgT = temps.reduce((a,b)=>a+b,0)/temps.length;
  const mxT  = Math.max(...temps);
  const avgH = hums.reduce((a,b)=>a+b,0)/hums.length;
  const avgP = press.reduce((a,b)=>a+b,0)/press.length;

  document.getElementById('vAvgTemp').textContent = avgT.toFixed(1);
  document.getElementById('vMaxTemp').textContent = mxT.toFixed(1);
  document.getElementById('vAvgHum').textContent  = avgH.toFixed(1);
  document.getElementById('vAvgPres').textContent = avgP.toFixed(1);
  document.getElementById('vCount').textContent   = temps.length;

  drawChart('chart1', temps, '#4da3ff');
  drawChart('chart2', hums,  '#32d74b');

  const breachList = document.getElementById('breach-list');
  let hasBreach = false;
  
  const TEMP_DEV_LIMIT = 3.0;
  const HUM_DEV_LIMIT  = 15.0;
  const PRES_DEV_LIMIT = 10.0;

  for(let i=0; i<temps.length; i++) {
    let flags = [];
    if (Math.abs(temps[i] - avgT) > TEMP_DEV_LIMIT) flags.push(`Temp (${temps[i].toFixed(1)}°C)`);
    if (Math.abs(hums[i] - avgH) > HUM_DEV_LIMIT) flags.push(`Hum (${hums[i].toFixed(1)}%)`);
    if (Math.abs(press[i] - avgP) > PRES_DEV_LIMIT) flags.push(`Pres (${press[i].toFixed(1)}hPa)`);
    
    if(flags.length > 0) {
      hasBreach = true;
      const li = document.createElement('li');
      li.style.marginBottom = '6px';
      li.innerHTML = `<span style='color:#fff;font-weight:bold;'>${timestamps[i].slice(11,16)}</span>: Spikes detected in ${flags.join(', ')}`;
      breachList.appendChild(li);
    }
  }

  if(!hasBreach) {
    const li = document.createElement('li');
    li.innerHTML = `<span style='color:#32d74b;font-weight:bold;'>✓ None</span>: All readings are within normal parameters.`;
    breachList.appendChild(li);
    document.getElementById('breaches-container').style.borderLeft = '4px solid #32d74b';
    document.querySelector('#breaches-container h3').style.color = '#32d74b';
  }
}
)";
  h += "</script></body></html>";
  return h;
}

void startWiFiServer() {
  WiFi.softAP(DEVICE_NAME, WIFI_PASS);
  MDNS.begin("palletsensor");

  server.on("/", []() {
    server.send(200, "text/html", buildDashboard());
  });

  server.on("/log.csv", []() {
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file) { server.send(404, "text/plain", "Not found"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=pallet_log.csv");
    server.streamFile(file, "text/csv");
    file.close();
  });

  server.on("/confirm-wipe", []() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:-apple-system,sans-serif;text-align:center;padding:30px;background:#121212;color:#e0e0e0}";
    html += ".btn{display:block;width:90%;max-width:320px;margin:12px auto;padding:14px;color:#fff;";
    html += "font-weight:600;border-radius:10px;text-decoration:none;font-size:15px}";
    html += ".red{background:#ff453a}.grey{background:#3a3a3c}</style></head><body>";
    html += "<h2 style='color:#fff'>Wipe all logs?</h2>";
    html += "<p style='color:#888;margin:16px 0'>This permanently erases all data from flash memory.</p>";
    html += "<a href='/wipe' class='btn red'>Yes, erase everything</a>";
    html += "<a href='/' class='btn grey'>Cancel</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/wipe", []() {
    File f = LittleFS.open(LOG_FILE, "w");
    if (f) {
      f.println("reading,timestamp,temperature_C,humidity_pct,pressure_hPa");
      f.close();
      readingNumber = 0;
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  wifiMode = true;
}

void logReading() {
  DateTime now = rtc.now();
  char ts[20];
  sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());

  float temp  = bme.readTemperature();
  float hum   = bme.readHumidity();
  float press = bme.readPressure() / 100.0F;
  readingNumber++;

  // --- Added Print Statement for Live Logging ---
  Serial.printf("LOG #%d | %s | Temp: %.2fC | Hum: %.2f%% | Pres: %.2fhPa\n", 
                readingNumber, ts, temp, hum, press);

  File file = LittleFS.open(LOG_FILE, "a");
  if (file) {
    file.printf("%d,%s,%.2f,%.2f,%.2f\n", readingNumber, ts, temp, hum, press);
    file.close();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // --- Added Print Statements for Boot Diagnostics ---
  Serial.println("\n--- Booting PalletSensor ---");

  Wire.begin(I2C_SDA, I2C_SCL);
  
  if (!bme.begin(0x76)) {
    Serial.println("Warning: BME280 not found! Check wiring.");
  } else {
    Serial.println("BME280 initialized successfully.");
  }

  if (!rtc.begin()) {
    Serial.println("Warning: RTC not found! Check wiring.");
  } else {
    Serial.println("RTC initialized successfully.");
  }
  
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  LittleFS.begin(true);
  if (!LittleFS.exists(LOG_FILE)) {
    File file = LittleFS.open(LOG_FILE, "w");
    if (file) {
      file.println("reading,timestamp,temperature_C,humidity_pct,pressure_hPa");
      file.close();
      Serial.println("Created new log file in LittleFS.");
    }
  } else {
    Serial.println("Existing log file found in LittleFS.");
  }

  startWiFiServer();
  Serial.println("Wi-Fi Started! Logging every 30 seconds...");
}

void loop() {
  unsigned long now = millis();
  if (now - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = now;
    logReading();
  }
  if (wifiMode) {
    server.handleClient();
  }
}