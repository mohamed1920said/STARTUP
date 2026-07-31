let ws=null;
const MAX_LOG=200;
const MAX_HISTORY=60;
const sensorHistory={};
let cloudCfgState={apiKeySet:false,caCertSet:false};

function connectWS(){
  const p=location.protocol==='https:'?'wss:':'ws:';
  ws=new WebSocket(p+'//'+location.host+'/ws');
  ws.onopen=()=>{
    document.getElementById('health-status').textContent='● Connected';
    document.getElementById('health-status').style.color='var(--accent2)';
    showToast('Connected to gateway','success');
  };
  ws.onclose=()=>{
    document.getElementById('health-status').textContent='○ Disconnected';
    document.getElementById('health-status').style.color='var(--danger)';
    setTimeout(connectWS,3000);
  };
  ws.onmessage=e=>{try{handleMessage(JSON.parse(e.data))}catch(ex){console.error(ex)}};
}

function handleMessage(m){
  switch(m.type){
    case'sensor':updateSensor(m);break;
    case'weather':updateWeather(m);break;
    case'actuator':updateActuator(m);break;
    case'log':appendLog(m);break;
    case'provisioned':provisionDone(m);break;
  }
}

function showToast(msg,level='info'){
  const c=document.getElementById('toast-container');
  const t=document.createElement('div');t.className='toast '+level;
  const icons={info:'ℹ',success:'✓',error:'✗',warn:'⚠'};
  const icon=document.createElement('span');icon.className='toast-icon';icon.textContent=icons[level]||'ℹ';
  const text=document.createElement('span');text.textContent=String(msg);
  t.append(icon,text);
  c.appendChild(t);
  setTimeout(()=>{t.style.opacity='0';t.style.transition='opacity .3s';setTimeout(()=>t.remove(),300)},4000);
}

function updateClock(){
  const n=new Date();
  document.getElementById('clock').textContent=n.toLocaleTimeString();
}

function updateWeather(w){
  var f = function(v, d){ return isFinite(v) ? v.toFixed(d) : '--'; };
  var r = function(v){ return isFinite(v) ? Math.round(v) : '--'; };
  document.getElementById('w-temp').textContent  = f(w.temp, 1);
  document.getElementById('w-hum').textContent   = f(w.hum, 1);
  document.getElementById('w-pres').textContent  = f(w.pres, 1);
  document.getElementById('rain').textContent    = f(w.rain, 2);
  document.getElementById('wind').textContent    = f(w.wind * 3.6, 1);
  document.getElementById('wind-dir').textContent = (isFinite(w.dir) ? w.dir : '--');
  document.getElementById('w-lux').textContent   = r(w.lux);
  document.getElementById('w-bat').textContent   = r(Math.min((w.bat / 4200) * 100, 100));
}

const sensorCards={},sensorLastSeen={};
function updateSensor(s){
  const c=document.getElementById('sensor-cards');
  const ph=document.getElementById('sensor-placeholder');
  if(ph)ph.remove();
  sensorLastSeen[s.id]=Date.now();
  let card=sensorCards[s.id];
  if(!card){
    card=document.createElement('div');card.className='sensor-card';
    card.innerHTML='<div class="sensor-header"><span class="sensor-name">Node 0x'+s.id.toString(16).padStart(4,'0')+'</span><span class="sensor-status" id="sst-'+s.id+'">●</span></div>'
      +'<div class="sensor-metrics">'
      +'<div class="metric"><label>Moisture</label><div class="bar-track"><div class="bar-fill moist-bar" id="mb-'+s.id+'"></div></div><span class="metric-val" id="mv-'+s.id+'">--</span></div>'
      +'<div class="metric"><label>Temperature</label><span class="metric-val temp-val" id="tv-'+s.id+'">--</span></div>'
      +'<div class="metric"><label>Battery</label><div class="bar-track"><div class="bar-fill batt-bar" id="bb-'+s.id+'"></div></div><span class="metric-val" id="bv-'+s.id+'">--</span></div>'
      +'</div>'
      +'<div class="sensor-chart"><canvas id="chart-'+s.id+'" width="300" height="50"></canvas></div>'
      +'<div class="sensor-footer"><span>RSSI: <strong id="sr-'+s.id+'">--</strong></span><span>Seq: <strong id="sq-'+s.id+'">--</strong></span></div>';
    c.appendChild(card);sensorCards[s.id]=card;
    sensorHistory[s.id]=[];
  }
  const m=s.moisture.toFixed(1);
  document.getElementById('mb-'+s.id).style.width=Math.min(m,100)+'%';
  document.getElementById('mv-'+s.id).textContent=m+'%';
  const t=s.temp.toFixed(1);
  const tv=document.getElementById('tv-'+s.id);
  tv.textContent=t+'°C';
  tv.style.color=t<0?'var(--accent)':t>30?'var(--danger)':'var(--text)';
  const b=s.batt;
  const bpct=Math.min((b/4.2)*100,100);
  document.getElementById('bb-'+s.id).style.width=bpct+'%';
  document.getElementById('bb-'+s.id).style.background=b<3.3?'var(--danger)':b<3.6?'var(--warning)':'var(--accent2)';
  document.getElementById('bv-'+s.id).textContent=bpct.toFixed(0)+'%';
  if(s.rssi!==undefined)document.getElementById('sr-'+s.id).textContent=s.rssi+' dBm';
  document.getElementById('sq-'+s.id).textContent=s.seq;
  // Update history + chart
  sensorHistory[s.id].push(s.moisture);
  if(sensorHistory[s.id].length>MAX_HISTORY)sensorHistory[s.id].shift();
  drawChart(s.id,sensorHistory[s.id]);
}
function drawChart(id,data){
  const canvas=document.getElementById('chart-'+id);
  if(!canvas||data.length<2)return;
  const ctx=canvas.getContext('2d');
  const w=canvas.width,h=canvas.height;
  ctx.clearRect(0,0,w,h);
  const min=Math.min(...data),max=Math.max(...data);
  const range=Math.max(max-min,1);
  const pad=4;
  ctx.strokeStyle='#58a6ff';ctx.lineWidth=1.5;
  ctx.beginPath();
  data.forEach((v,i)=>{
    const x=(i/(data.length-1))*(w-pad*2)+pad;
    const y=h-pad-((v-min)/range)*(h-pad*2);
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.stroke();
  // fill gradient
  const grad=ctx.createLinearGradient(0,0,0,h);
  grad.addColorStop(0,'rgba(88,166,255,.2)');
  grad.addColorStop(1,'rgba(88,166,255,0)');
  ctx.lineTo(w-pad,h-pad);ctx.lineTo(pad,h-pad);ctx.closePath();
  ctx.fillStyle=grad;ctx.fill();
}

const actuatorCards={};
let cachedNodes=[];
function updateActuator(a){
  sensorLastSeen[a.id]=Date.now();
  const c=document.getElementById('actuator-cards');
  let card=actuatorCards[a.id];
  if(!card){
    card=document.createElement('div');card.className='actuator-card';
    card.innerHTML='<div class="actuator-header"><span class="sensor-name">Actuator 0x'+a.id.toString(16).padStart(4,'0')+'</span><span class="valve-state" id="ast-'+a.id+'">--</span></div>'
      +'<div class="actuator-controls">'
      +'<button class="toggle-btn" id="abtn-'+a.id+'">Toggle</button>'
      +'<span class="actuator-batt" id="abatt-'+a.id+'">--</span>'
      +'</div>'
      +'<div class="actuator-auto">'
      +'<label class="auto-toggle"><input type="checkbox" id="auto-cb-'+a.id+'" onchange="setAutoCfg('+a.id+')"> Auto</label>'
      +'<div class="auto-params" id="auto-prm-'+a.id+'" style="display:none">'
      +'<label>Threshold: <span id="athr-val-'+a.id+'">50</span>%<br><input type="range" min="0" max="100" value="50" id="athr-'+a.id+'" oninput="document.getElementById(\'athr-val-'+a.id+'\').textContent=this.value;setAutoCfg('+a.id+')"></label>'
      +'<label>Sensor ID: <input type="text" class="auto-sensor" id="asid-'+a.id+'" value="0x0001" onchange="setAutoCfg('+a.id+')"></label>'
      +'</div></div>';
    c.appendChild(card);actuatorCards[a.id]=card;
    document.getElementById('abtn-'+a.id).addEventListener('click',()=>toggleAct(a.id));
  }
  const stEl=document.getElementById('ast-'+a.id);
  stEl.textContent=a.valve?'● OPEN':'○ CLOSED';
  stEl.className='valve-state '+(a.valve?'on':'off');
  var abpct=Math.min((a.batt/4.2)*100,100);
  document.getElementById('abatt-'+a.id).textContent=abpct.toFixed(0)+'%';
  const btn=document.getElementById('abtn-'+a.id);
  btn.textContent=a.valve?'Turn OFF':'Turn ON';
  btn.className='toggle-btn '+(a.valve?'on':'off');
  const n=cachedNodes.find(x=>x.id===a.id);
  if(n){
    document.getElementById('auto-cb-'+a.id).checked=n.autoMode;
    document.getElementById('auto-prm-'+a.id).style.display=n.autoMode?'block':'none';
    document.getElementById('athr-'+a.id).value=n.threshold;
    document.getElementById('athr-val-'+a.id).textContent=n.threshold;
    document.getElementById('asid-'+a.id).value='0x'+n.sensorId.toString(16).padStart(4,'0');
  }
}
function setAutoCfg(id){
  const auto=document.getElementById('auto-cb-'+id).checked;
  document.getElementById('auto-prm-'+id).style.display=auto?'block':'none';
  const thr=parseInt(document.getElementById('athr-'+id).value);
  const sid=parseInt(document.getElementById('asid-'+id).value,16)||0;
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({action:'set_actuator_config',node_id:id,auto_mode:auto,threshold:thr,sensor_id:sid}));
}
function toggleAct(id){
  const btn = document.getElementById('abtn-'+id);
  const isOpen = btn && btn.textContent === 'Turn OFF';
  fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({node_id:id,value:!isOpen})});
  showToast('Toggle command sent to actuator 0x'+id.toString(16).padStart(4,'0'),'info');
}
function provisionDone(m){
  const el=document.getElementById('provision-status');
  el.textContent=m.status==='ok'?'Node 0x'+m.id.toString(16).padStart(4,'0')+' ('+m.node_type+') registered!':'Failed: '+m.status;
  el.style.color=m.status==='ok'?'var(--accent2)':'var(--danger)';
  if(m.status==='ok')showToast('Node 0x'+m.id.toString(16).padStart(4,'0')+' provisioned as '+m.node_type,'success');
  fetchNodes();
}
async function fetchNodes(){
  try{const r=await fetch('/api/nodes');cachedNodes=await r.json();renderNodes(cachedNodes);
    cachedNodes.forEach(n=>{if(n.type===2&&!actuatorCards[n.id])updateActuator({id:n.id,valve:false,batt:0})});
  }catch(ex){console.error(ex)}
}
function nodeStatus(id){
  const last=sensorLastSeen[id];if(!last)return'○ Offline';
  const ago=(Date.now()-last)/1000;
  return ago<20?'● Online':'○ Offline';
}
function updateNodeStatuses(){
  const t=document.getElementById('nodes-tbody');
  if(!t)return;const rows=t.children;
  for(let i=0;i<rows.length;i++){
    const id=parseInt(rows[i].dataset.nid,16);if(!id)continue;
    const st=rows[i].querySelector('.node-status');
    if(st){
      const last=sensorLastSeen[id];
      if(!last){st.textContent='○ Offline';st.className='node-offline';continue}
      const ago=(Date.now()-last)/1000;
      st.textContent=ago<20?'● Online':'○ Offline';
      st.className=ago<20?'node-online':'node-offline';
    }
  }
}
function removeNode(id){
  if(!confirm('Remove node 0x'+id.toString(16).padStart(4,'0')+'?'))return;
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({action:'remove_node',node_id:id}));
  showToast('Removing node 0x'+id.toString(16).padStart(4,'0'),'warn');
}
function renderNodes(nodes){
  const t=document.getElementById('nodes-tbody');t.innerHTML='';
  nodes.forEach(n=>{
    const r=document.createElement('tr');r.dataset.nid='0x'+n.id.toString(16).padStart(4,'0');
    const st=nodeStatus(n.id);
    r.innerHTML='<td>0x'+n.id.toString(16).padStart(4,'0')+'</td>'
      +'<td><span class="node-status" id="ns-'+n.id+'">'+st+'</span></td>'
      +'<td><span class="tag '+(n.type===2?'tag-actuator':'tag-sensor')+'">'+['','sensor','actuator'][n.type]+'</span></td>'
      +'<td class="node-alias"></td>'
      +'<td>'+n.lastSeq+'</td>'
      +'<td>'+(n.type===2?(n.autoMode?'<span style="color:var(--accent2)">ON</span>':'OFF'):'-')+'</td>'
      +'<td>'+(n.type===2?n.threshold:'-')+'</td>'
      +'<td><button class="remove-btn" onclick="removeNode('+n.id+')">✕</button></td>';
    t.appendChild(r);
    r.querySelector('.node-alias').textContent=String(n.alias||'');
    const el=document.getElementById('ns-'+n.id);
    if(el)el.className=st.indexOf('Online')>=0?'node-online':'node-offline';
  });
}
document.getElementById('provision-form').addEventListener('submit',e=>{
  e.preventDefault();
  const id=parseInt(document.getElementById('p-id').value,16);
  const psk=document.getElementById('p-psk').value.trim();
  const type=document.getElementById('p-type').value;
  const alias=document.getElementById('p-alias').value.trim()||'unnamed';
  if(!id||id>0xFFFF||isNaN(id)||!/^[0-9a-fA-F]{32}$/.test(psk)){showToast('Invalid node ID or PSK (need 32 hexadecimal characters)','error');return;}
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({action:'add_node',node_id:id,psk:psk,node_type:type,alias:alias}));
  document.getElementById('provision-status').textContent='Sending provisioning request for node 0x'+id.toString(16).padStart(4,'0')+'...';
  showToast('Provisioning node 0x'+id.toString(16).padStart(4,'0')+'...','info');
});
document.getElementById('ota-form').addEventListener('submit',async e=>{
  e.preventDefault();const file=e.target.querySelector('input[type=file]').files[0];
  if(!file)return;const fd=new FormData();fd.append('firmware',file);
  document.getElementById('ota-progress').textContent='Uploading...';
  showToast('OTA firmware upload started','info');
  try{const r=await fetch('/api/ota/upload',{method:'POST',body:fd});const j=await r.json();document.getElementById('ota-progress').textContent=j.msg||'Update triggered';showToast('OTA update triggered','success')}catch(ex){document.getElementById('ota-progress').textContent='Error: '+ex.message;showToast('OTA error: '+ex.message,'error')}
});

async function fetchCloudCfg(){
  try{
    const r=await fetch('/api/cloud-config');
    const cfg=await r.json();
    cloudCfgState=cfg;
    if(cfg.url) document.getElementById('c-url').value = cfg.url;
    document.getElementById('c-apikey').value='';
    document.getElementById('c-apikey').placeholder=cfg.apiKeySet?'API key saved — leave blank to keep it':'Enter API key';
    document.getElementById('c-cacert').value='';
    document.getElementById('c-cacert').placeholder=cfg.caCertSet?'CA certificate saved — leave blank to keep it':'Paste trusted root CA certificate (PEM)';
  }catch(ex){console.error('Failed to fetch cloud config', ex)}
}

document.getElementById('cloud-form').addEventListener('submit', async e=>{
  e.preventDefault();
  const url=document.getElementById('c-url').value.trim();
  const apiKey=document.getElementById('c-apikey').value.trim();
  const caCert=document.getElementById('c-cacert').value.trim();
  document.getElementById('cloud-status').textContent='Saving...';
  if(url&&!url.startsWith('https://')){
    document.getElementById('cloud-status').textContent='HTTPS URL required';
    document.getElementById('cloud-status').style.color='var(--danger)';
    return;
  }
  if(!apiKey&&!cloudCfgState.apiKeySet){
    document.getElementById('cloud-status').textContent='API key is required';return;
  }
  if(!caCert&&!cloudCfgState.caCertSet){
    document.getElementById('cloud-status').textContent='Trusted CA certificate is required';return;
  }
  try{
    const r=await fetch('/api/cloud-config',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({url,apiKey,caCert})
    });
    if(r.ok) {
      document.getElementById('cloud-status').textContent='Saved successfully!';
      document.getElementById('cloud-status').style.color='var(--accent2)';
      showToast('Cloud configuration saved','success');
    } else {
      document.getElementById('cloud-status').textContent='Failed to save';
      document.getElementById('cloud-status').style.color='var(--danger)';
    }
  }catch(ex){
    document.getElementById('cloud-status').textContent='Error saving config';
  }
});
async function restartGw(){
  if(!confirm('Restart the gateway?'))return;
  try{showToast('Gateway restarting...','warn');await fetch('/api/restart',{method:'POST'});}catch(ex){}
}
function appendLog(l){
  const el=document.getElementById('log-output');
  const line='['+new Date().toLocaleTimeString()+'] ['+l.level+'] '+l.msg;
  el.appendChild(document.createTextNode(line+'\n'));
  while(el.childNodes.length>MAX_LOG)el.removeChild(el.firstChild);
  el.scrollTop=el.scrollHeight;
}
async function pollHealth(){
  try{const r=await fetch('/api/health');const h=await r.json();
    document.getElementById('health-status').textContent='● Uptime: '+h.uptime+'s · Free: '+(h.free_heap/1024).toFixed(0)+'KB · RSSI: '+h.rssi+'dBm';
    document.getElementById('gw-uptime').textContent=h.uptime+'s';
    document.getElementById('gw-heap').textContent=(h.free_heap/1024).toFixed(0)+'KB';
    document.getElementById('gw-rssi').textContent=h.rssi+'dBm';
  }catch(ex){}
}
document.addEventListener('DOMContentLoaded',()=>{
  connectWS();
  setInterval(pollHealth,30000);
  setInterval(fetchNodes,15000);
  setInterval(updateNodeStatuses,10000);
  setInterval(updateClock,1000);
  fetchNodes();updateClock();fetchCloudCfg();
});
