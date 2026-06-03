from flask import Flask, request, render_template_string, send_file
from flask_cors import CORS
import urllib.parse
import os

app = Flask(__name__)
CORS(app)

# The file where uploaded data is stored
DATA_FILE = 'pallet_data.csv'

# This HTML is an exact copy of your ESP32 dashboard, 
# adapted to receive the CSV data directly from Flask.
HTML_TEMPLATE = """
<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Cloud PalletSensor</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#121212;padding:16px;color:#e0e0e0}
h1{font-size:18px;font-weight:600;margin-bottom:16px;color:#ffffff}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}
.chart-grid{display:grid;grid-template-columns:repeat(auto-fit, minmax(280px, 1fr));gap:10px;margin-bottom:12px}
.card{background:#1e1e1e;border-radius:10px;padding:14px;box-shadow:0 1px 4px rgba(0,0,0,.2)}
.val{font-size:28px;font-weight:700;color:#4da3ff}
.lbl{font-size:11px;color:#888;margin-top:4px}
.chart-card{background:#1e1e1e;border-radius:10px;padding:16px;box-shadow:0 1px 4px rgba(0,0,0,.2)}
.ctitle{font-size:10px;font-weight:600;letter-spacing:.8px;color:#888;margin-bottom:10px}
.breach{color:#ff453a!important}
.btn{display:block;padding:14px;margin-bottom:10px;color:#fff;font-weight:600;
border-radius:10px;text-align:center;text-decoration:none;font-size:15px;cursor:pointer;border:none;width:100%}
.blue{background:#0a84ff}
</style></head><body>

<h1>☁️ Cloud Pallet Dashboard</h1>

<div class='grid'>
<div class='card'><div class='val' id='vAvgTemp'>—</div><div class='lbl'>Avg Temp °C</div></div>
<div class='card'><div class='val' id='vMaxTemp'>—</div><div class='lbl'>Max Temp °C</div></div>
<div class='card'><div class='val' id='vAvgHum'>—</div><div class='lbl'>Avg Humidity %</div></div>
<div class='card'><div class='val' id='vAvgPres'>—</div><div class='lbl'>Avg Pressure hPa</div></div>
</div>

<div class='chart-grid'>
<div class='chart-card'><div class='ctitle'>TEMPERATURE °C</div>
<svg id='chart1' viewBox='0 0 320 120' xmlns='http://www.w3.org/2000/svg' style='width:100%'></svg></div>
<div class='chart-card'><div class='ctitle'>HUMIDITY %</div>
<svg id='chart2' viewBox='0 0 320 120' xmlns='http://www.w3.org/2000/svg' style='width:100%'></svg></div>
</div>

<div id='breaches-container' class='card' style='margin-bottom:12px; display:block; border-left: 4px solid #ff453a;'>
<h3 style='font-size:14px; margin-bottom:8px; color:#ff453a;'>⚠ Breach Alerts</h3>
<ul id='breach-list' style='list-style:none; font-size:12px; color:#ccc;'></ul>
</div>

<div class='card' style='margin-bottom:12px;display:flex;justify-content:space-between;align-items:center'>
<span style='font-size:13px;color:#888'>Total readings</span>
<span class='val' id='vCount' style='font-size:20px;color:#e0e0e0'>—</span>
</div>

<a href='/download' class='btn blue'>⬇ Download Full Cloud CSV</a>

<script>
// Flask injects the exact CSV string here seamlessly
const raw = `{{ csv_data | safe }}`;

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

const lines = raw.trim().split('\\n').filter(l=>l.trim());
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
} else {
  // If no data yet
  document.getElementById('breaches-container').innerHTML = "<p style='color:#888'>No data synced yet. Push from the pallet sensor!</p>";
  document.getElementById('vCount').textContent = "0";
}
</script></body></html>
"""

@app.route('/', methods=['GET'])
def index():
    csv_data = ""
    # Read the saved CSV file to pass it to the frontend charts
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r') as f:
            csv_data = f.read()
            
    # Escape any backticks just in case, to prevent JS template literals from breaking
    csv_data = csv_data.replace("`", "\\`")
            
    return render_template_string(HTML_TEMPLATE, csv_data=csv_data)

@app.route('/upload', methods=['POST'])
def upload_data():
    raw_body = request.data.decode('utf-8')
    
    # Handle url-encoded or plain text payloads
    if "reading%2Ctimestamp" in raw_body or "reading," in raw_body:
        csv_data = urllib.parse.unquote(raw_body)
    else:
        csv_data = raw_body

    if csv_data:
        with open(DATA_FILE, 'w') as f:
            f.write(csv_data)
        return "Upload successful", 200
        
    return "No data received", 400

@app.route('/download', methods=['GET'])
def download():
    if os.path.exists(DATA_FILE):
        return send_file(DATA_FILE, as_attachment=True, download_name='cloud_pallet_log.csv')
    return "No data available yet.", 404

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5001)