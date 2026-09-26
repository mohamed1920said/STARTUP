let ws=null;
const MAX_LOG=200;
const MAX_HISTORY=60;
const sensorHistory={};
let cloudCfgState={apiKeySet:false,caCertSet:false};
let activeView='overview', reconnectTimer=null, reconnectDelay=1000;
let refreshTimer=null, refreshRunning=false, healthOK=false, lastHealthAt=0;
let lastFrameAt=0, lastBootId=null, lastUptime=null, aiStatusLoaded=false;
let telemetrySupported=true;
const REQUEST_TIMEOUT_MS=8000;
const aiPredictions={};
const actuatorStates={};
function textAt(id,value){const el=document.getElementById(id);if(el)el.textContent=value;}
function finite(value,digits=1){return Number.isFinite(value)?value.toFixed(digits):'—';}
function validNodeId(value){return Number.isInteger(value)&&value>0&&value<=65535;}
async function fetchJSON(url,options={}){
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),REQUEST_TIMEOUT_MS);
  try{
    const response=await fetch(url,{cache:'no-store',...options,signal:controller.signal});
    if(!response.ok){const error=new Error(response.status===401?'Sign in again to continue.':'Request failed ('+response.status+').');error.status=response.status;throw error;}
    return await response.json();
  }finally{clearTimeout(timeout);}
}
function updateConnection(){
  const live=!!ws&&ws.readyState===WebSocket.OPEN;
  const reachable=healthOK&&Date.now()-lastHealthAt<45000;
  textAt('health-status',live?'Connected':reachable?'Reconnecting live data':'Gateway unavailable');
  const status=document.getElementById('health-status');
  if(status)status.style.color=live?'var(--success)':'var(--text-muted)';
  const banner=document.getElementById('connection-banner');
  if(banner)banner.hidden=live;
  textAt('connection-detail',reachable?'The gateway is reachable. Restoring live updates; saved readings remain visible.':'Unable to reach the central gateway. Check its power and Wi-Fi. Displayed readings may be out of date.');
  document.querySelectorAll('.toggle-btn').forEach(button=>{button.disabled=!reachable&&!live;});
  if(!live&&!reachable){textAt('overview-recommendation','Gateway connection lost');textAt('overview-reason','Recommendations will refresh after the central reconnects.');}
  updateAIEmptyState();
}
function scheduleReconnect(){
  if(reconnectTimer||document.hidden)return;
  reconnectTimer=setTimeout(()=>{reconnectTimer=null;connectWS();},reconnectDelay);
  reconnectDelay=Math.min(reconnectDelay*2,30000);
}

function connectWS(){
  if(document.hidden||(ws&&(ws.readyState===WebSocket.OPEN||ws.readyState===WebSocket.CONNECTING)))return;
  const p=location.protocol==='https:'?'wss:':'ws:';
  const socket=new WebSocket(p+'//'+location.host+'/ws');ws=socket;
  const openingTimeout=setTimeout(()=>{if(socket.readyState===WebSocket.CONNECTING)socket.close();},REQUEST_TIMEOUT_MS);
  socket.onopen=()=>{
    clearTimeout(openingTimeout);lastFrameAt=Date.now();reconnectDelay=1000;
    updateConnection();refreshDashboard();
  };
  socket.onclose=()=>{
    clearTimeout(openingTimeout);if(ws!==socket)return;
    ws=null;updateConnection();scheduleReconnect();
  };
  socket.onerror=()=>socket.close();
  socket.onmessage=e=>{lastFrameAt=Date.now();try{handleMessage(JSON.parse(e.data));}catch(ex){console.error('Invalid gateway message',ex);}};
}

function handleMessage(m){
  switch(m.type){
    case'sensor':updateSensor(m);break;
    case'weather':updateWeather(m);break;
    case'actuator':updateActuator(m);break;
    case'ai_weather':updateAIWeather(m);break;
    case'ai_prediction':updateAIPrediction(m);break;
    case'log':appendLog(m);break;
    case'provisioned':provisionDone(m);break;
    case'command_result':showToast(m.status==='queued'?'Valve command queued.':'Valve command rejected.',m.status==='queued'?'info':'error');break;
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
  var f = function(v, d){ return Number.isFinite(v) ? v.toFixed(d) : '--'; };
  var r = function(v){ return Number.isFinite(v) ? Math.round(v) : '--'; };
  document.getElementById('w-temp').textContent  = f(w.temp, 1);
  document.getElementById('w-hum').textContent   = f(w.hum, 1);
  document.getElementById('w-pres').textContent  = f(w.pres, 1);
  document.getElementById('rain').textContent    = f(w.rain, 2);
  document.getElementById('wind').textContent    = f(Number.isFinite(w.wind)?w.wind*3.6:NaN, 1);
  document.getElementById('wind-dir').textContent = (Number.isFinite(w.dir) ? w.dir : '--');
  document.getElementById('w-lux').textContent   = r(w.lux);
  document.getElementById('w-bat').textContent   = f(Number.isFinite(w.bat)&&w.bat>0?w.bat/1000:NaN,2);
  textAt('weather-updated',w.age_ms>90000?'Last observation · '+Math.floor(w.age_ms/60000)+' min ago':'Latest station observation · '+new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}));
}

const aiZoneCards={};
function aiNumber(value,digits){return Number.isFinite(value)?Number(value).toFixed(digits):'--'}
function setAIMode(shadow,version){
  const el=document.getElementById('ai-mode');if(!el)return;
  el.textContent=shadow?'Advisory only':'Automatic control enabled';
  el.title=version?'Model version '+version:'';
  el.className='ai-mode '+(shadow?'shadow':'control');
}
function updateAIWeather(w){
  const valid=w.valid!==false&&w.forecast_valid!==false;
  const temp=w.temperature_c_24h!==undefined?w.temperature_c_24h:w.forecast_temp_c;
  const rain=w.rain_mm_24h!==undefined?w.rain_mm_24h:w.forecast_rain_mm;
  const probability=w.rain_probability;
  const et0=w.et0_mm_24h!==undefined?w.et0_mm_24h:w.forecast_et0_mm;
  document.getElementById('ai-f-temp').textContent=aiNumber(valid?temp:NaN,1);
  document.getElementById('ai-f-rain').textContent=aiNumber(valid?rain:NaN,1);
  document.getElementById('ai-f-rain-p').textContent=valid&&Number.isFinite(probability)?Math.round(probability*100):'—';
  document.getElementById('ai-f-et0').textContent=aiNumber(valid?et0:NaN,1);
  setAIMode(w.shadow_mode!==false,w.model_version);
}
function updateAIPrediction(p){
  const id=Number(p.sensor_id);if(!validNodeId(id))return;
  aiPredictions[id]={...p,receivedAt:Date.now()-(Number.isFinite(p.sensor_age_s)?p.sensor_age_s*1000:p.age_ms||0)};
  const holder=document.getElementById('ai-zone-cards');
  const placeholder=document.getElementById('ai-placeholder');if(placeholder)placeholder.remove();
  let card=aiZoneCards[id];
  if(!card){
    card=document.createElement('article');card.className='ai-zone-card';
    card.innerHTML='<div class="ai-zone-header"><strong>Zone sensor 0x'+id.toString(16).padStart(4,'0')+'</strong><span id="ai-decision-'+id+'" class="ai-decision">--</span></div>'
      +'<div class="ai-answer-grid">'
      +'<div><label>Valve runtime</label><strong id="ai-runtime-'+id+'">--</strong></div>'
      +'<div><label>Water depth</label><strong id="ai-water-'+id+'">--</strong></div>'
      +'<div><label>Schedule</label><strong id="ai-schedule-'+id+'">--</strong></div>'
      +'<div><label>Drying rate</label><strong id="ai-drying-'+id+'">--</strong></div>'
      +'<div><label>Watering result</label><strong id="ai-effect-'+id+'">--</strong></div>'
      +'<div><label>Equipment health</label><strong id="ai-fault-'+id+'">--</strong></div>'
      +'</div><p class="ai-reason" id="ai-reason-'+id+'"></p>';
    holder.appendChild(card);aiZoneCards[id]=card;
  }
  const decision=document.getElementById('ai-decision-'+id);
  if(!p.valid||p.stale){decision.textContent='Needs attention';decision.className='ai-decision warn'}
  else if(p.fault&&p.fault!=='normal'){decision.textContent='Check equipment';decision.className='ai-decision warn'}
  else if(p.readiness==='needs_weather'){decision.textContent=p.irrigation_needed?'Soil is dry · weather needed':'Soil moisture adequate';decision.className='ai-decision wait'}
  else if(p.irrigation_needed&&!p.runtime_min){decision.textContent='Set up field';decision.className='ai-decision wait'}
  else if(p.irrigation_now){decision.textContent='Irrigation recommended';decision.className='ai-decision irrigate'}
  else if(p.irrigation_needed){decision.textContent='Wait '+(p.wait_hours||0)+' h';decision.className='ai-decision wait'}
  else{decision.textContent='No irrigation needed';decision.className='ai-decision ok'}
  const usable=p.valid&&!p.stale;
  const runtime=usable&&p.runtime_min>0?p.runtime_min+' min × '+p.cycles+' cycle'+(p.cycles===1?'':'s'):'Not available';
  document.getElementById('ai-runtime-'+id).textContent=runtime;
  document.getElementById('ai-water-'+id).textContent=usable?aiNumber(p.water_mm,1)+' mm':'—';
  document.getElementById('ai-schedule-'+id).textContent=!usable?'Unavailable':p.readiness==='needs_weather'?'Waiting for weather':!p.irrigation_needed?'No watering planned':p.fault&&p.fault!=='normal'?'Paused · check equipment':!p.runtime_min?'Add area and flow':p.wait_for_rain?'Wait for rain':p.slow_drying?'Wait · slow drying':p.wait_hours>0?'Review in '+p.wait_hours+' h':'Ready for review';
  document.getElementById('ai-drying-'+id).textContent=Number.isFinite(p.drying_rate)?aiNumber(p.drying_rate,1)+' points/day'+(p.drying_rate_measured?' · measured':' · estimated'):'More readings needed';
  const effect=String(p.watering_effect||'not_available').replaceAll('_',' ');
  document.getElementById('ai-effect-'+id).textContent=effect+(p.watering_effect==='moisture_increased'&&Number.isFinite(p.moisture_increase_pct)?' (+'+p.moisture_increase_pct.toFixed(1)+' points)':'');
  const fault=document.getElementById('ai-fault-'+id);const faultName=p.fault==='normal'?'No fault detected in available data':String(p.fault||'unavailable').replaceAll('_',' ');
  fault.textContent=usable?faultName:'Unavailable';
  fault.className=p.fault&&p.fault!=='normal'?'ai-fault danger':'ai-fault ok';
  document.getElementById('ai-reason-'+id).textContent=String(p.reason||'')+(p.fault_checks_limited?' Equipment checks are limited by the connected sensors.':'');
  refreshAISummary();
}
function refreshAISummary(){
  const entries=Object.values(aiPredictions);if(!entries.length)return;
  const score=p=>!p.valid?5:p.fault&&p.fault!=='normal'?4:p.irrigation_now?3:p.irrigation_needed?2:1;
  entries.sort((a,b)=>score(b)-score(a));
  const first=entries[0];const decision=document.getElementById('ai-decision-'+first.sensor_id);
  textAt('overview-recommendation',decision?decision.textContent:'Review irrigation');
  textAt('overview-reason',(entries.length>1?entries.length+' zones · ':'')+String(first.reason||'Open Irrigation to review your field.'));
}
async function fetchAIStatus(){
  try{
    const status=await fetchJSON('/api/ai/status');aiStatusLoaded=true;
    setAIMode(status.shadow_mode!==false,status.model_version);
    if(status.weather)updateAIWeather(Object.assign({shadow_mode:status.shadow_mode,model_version:status.model_version},status.weather));
    if(Array.isArray(status.zones)){
      const ids=new Set(status.zones.map(p=>Number(p.sensor_id)));
      Object.keys(aiZoneCards).forEach(id=>{if(!ids.has(Number(id))){aiZoneCards[id].remove();delete aiZoneCards[id];delete aiPredictions[id];}});
      status.zones.forEach(updateAIPrediction);
    }
    updateAIEmptyState(status.reason);
  }catch(ex){
    if(Object.keys(aiZoneCards).length){textAt('overview-recommendation','Recommendation update unavailable');textAt('overview-reason','Previous advice is still displayed. Reconnect before acting on it.');}
    else updateAIEmptyState('The recommendation service could not be reached. Retrying automatically.');
  }
}
function updateAIEmptyState(reason){
  if(Object.keys(aiZoneCards).length)return;
  const holder=document.getElementById('ai-zone-cards');if(!holder)return;
  let placeholder=document.getElementById('ai-placeholder');
  if(!placeholder){placeholder=document.createElement('div');placeholder.id='ai-placeholder';placeholder.className='empty-state';holder.appendChild(placeholder);}
  const connected=healthOK||(ws&&ws.readyState===WebSocket.OPEN);
  const hasSensor=cachedNodes.some(n=>n.type===1);
  const title=!connected?'Connect to your gateway':!hasSensor&&aiStatusLoaded?'Add a soil sensor':'No soil reading yet';
  const detail=reason||(!connected?'Check central power and Wi-Fi. We will reconnect automatically.':!hasSensor&&aiStatusLoaded?'Register your LoRa sensor in Settings, then power it on.': 'Keep the sensor powered and within LoRa range. Confirm its node ID and key match the gateway. Recommendations appear after a valid soil reading.');
  placeholder.replaceChildren();
  const heading=document.createElement('strong');heading.textContent=title;
  const body=document.createElement('p');body.textContent=detail;placeholder.append(heading,body);
  textAt('overview-recommendation',title);textAt('overview-reason',detail);
}

const sensorCards={},sensorLastSeen={};
function updateSensor(s){
  if(!validNodeId(s.id))return;
  const c=document.getElementById('sensor-cards');
  const ph=document.getElementById('sensor-placeholder');
  if(ph)ph.remove();
  sensorLastSeen[s.id]=Date.now()-(s.age_ms||0);
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
      +'<div class="sensor-footer"><span>Raw: <strong id="sraw-'+s.id+'">--</strong></span><span>RSSI: <strong id="sr-'+s.id+'">--</strong></span><span>Seq: <strong id="sq-'+s.id+'">--</strong></span></div>';
    c.appendChild(card);sensorCards[s.id]=card;
    sensorHistory[s.id]=[];
  }
  const m=finite(s.moisture);
  document.getElementById('mb-'+s.id).style.width=(Number.isFinite(s.moisture)?Math.max(0,Math.min(s.moisture,100)):0)+'%';
  document.getElementById('mv-'+s.id).textContent=m+(Number.isFinite(s.moisture)?'%':'');
  const t=finite(s.temp);
  const tv=document.getElementById('tv-'+s.id);
  tv.textContent=t+(Number.isFinite(s.temp)?'°C':'');
  const b=s.batt;
  const bpct=Number.isFinite(b)&&b>0?Math.max(0,Math.min((b-3.0)/1.2*100,100)):0;
  document.getElementById('bb-'+s.id).style.width=bpct+'%';
  document.getElementById('bb-'+s.id).style.background=b<3.3?'var(--danger)':b<3.6?'var(--warning)':'var(--success)';
  document.getElementById('bv-'+s.id).textContent=Number.isFinite(b)&&b>0?b.toFixed(2)+' V':'Unavailable';
  document.getElementById('bb-'+s.id).parentElement.hidden=true;
  if(s.rssi!==undefined)document.getElementById('sr-'+s.id).textContent=s.rssi+' dBm';
  document.getElementById('sraw-'+s.id).textContent=s.moisture_raw===undefined?'--':s.moisture_raw;
  document.getElementById('sq-'+s.id).textContent=s.seq;
  // Update history + chart
  const previousSeq=card.dataset.seq;
  card.dataset.seq=String(s.seq);
  if(Number.isFinite(s.moisture)&&previousSeq!==String(s.seq))sensorHistory[s.id].push(s.moisture);
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
  if(!validNodeId(a.id))return;
  sensorLastSeen[a.id]=Date.now()-(a.age_ms||0);
  actuatorStates[a.id]=a;
  const c=document.getElementById('actuator-cards');
  const ph=c.querySelector('.empty-state');if(ph)ph.remove();
  let card=actuatorCards[a.id];
  if(!card){
    card=document.createElement('div');card.className='actuator-card';
    card.innerHTML='<div class="actuator-header"><span class="sensor-name">Actuator 0x'+a.id.toString(16).padStart(4,'0')+'</span><span class="valve-state" id="ast-'+a.id+'">--</span></div>'
      +'<div class="actuator-controls">'
      +'<button class="toggle-btn" id="abtn-'+a.id+'">Toggle</button>'
      +'<span class="actuator-batt" id="abatt-'+a.id+'">--</span>'
      +'</div><div class="sensor-footer"><span id="afb-'+a.id+'">Feedback: --</span><span id="ahyd-'+a.id+'">Hydraulics: --</span></div>'
      +'<div class="actuator-auto">'
      +'<label class="auto-toggle"><input type="checkbox" id="auto-cb-'+a.id+'" onchange="setAutoCfg('+a.id+')"> Auto (verified feedback required)</label>'
      +'<div class="auto-params" id="auto-prm-'+a.id+'" style="display:none">'
      +'<label>Threshold: <span id="athr-val-'+a.id+'">50</span>%<br><input type="range" min="5" max="95" value="50" id="athr-'+a.id+'" oninput="document.getElementById(\'athr-val-'+a.id+'\').textContent=this.value" onchange="setAutoCfg('+a.id+')"></label>'
      +'<label>Sensor ID: <input type="text" class="auto-sensor" id="asid-'+a.id+'" value="0x0001" onchange="setAutoCfg('+a.id+')"></label>'
      +'</div></div>';
    c.appendChild(card);actuatorCards[a.id]=card;
    document.getElementById('abtn-'+a.id).addEventListener('click',()=>toggleAct(a.id));
  }
  const stEl=document.getElementById('ast-'+a.id);
  const commanded=a.commanded!==undefined?a.commanded:a.valve;
  stEl.textContent=(commanded?'Open':'Closed')+(a.feedback_valid?' · confirmed':' · unverified');
  stEl.className='valve-state '+(a.valve?'on':'off');
  document.getElementById('abatt-'+a.id).textContent=Number.isFinite(a.batt)&&a.batt>0?a.batt.toFixed(2)+' V':'Battery unavailable';
  document.getElementById('afb-'+a.id).textContent=a.feedback_valid?'Feedback: verified':'Feedback: unavailable';
  const hyd=[];
  if(Number.isFinite(a.flow_lpm)&&a.flow_lpm>=0)hyd.push(a.flow_lpm.toFixed(2)+' L/min');
  if(Number.isFinite(a.pressure_bar)&&a.pressure_bar>=0)hyd.push(a.pressure_bar.toFixed(2)+' bar');
  if(Number.isFinite(a.tank_pct)&&a.tank_pct>=0)hyd.push(a.tank_pct.toFixed(0)+'% tank');
  document.getElementById('ahyd-'+a.id).textContent='Hydraulics: '+(hyd.length?hyd.join(' · '):'not connected');
  const btn=document.getElementById('abtn-'+a.id);
  btn.textContent=commanded?'Close valve':'Open valve';
  btn.className='toggle-btn '+(a.valve?'on':'off');
  const n=cachedNodes.find(x=>x.id===a.id);
  if(n){
    const autoControl=document.getElementById('auto-cb-'+a.id);
    autoControl.disabled=!a.feedback_valid;
    autoControl.checked=n.autoMode&&a.feedback_valid;
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
  if(ws&&ws.readyState===WebSocket.OPEN){ws.send(JSON.stringify({action:'set_actuator_config',node_id:id,auto_mode:auto,threshold:thr,sensor_id:sid}));showToast('Configuration requested. Check the device list for the saved setting.');}
  else showToast('Gateway disconnected. Configuration was not sent.','error');
}
async function toggleAct(id){
  const btn = document.getElementById('abtn-'+id);
  const state=actuatorStates[id];if(!state)return;
  const isOpen=state.commanded!==undefined?state.commanded:state.valve;
  if(!isOpen&&!confirm('Open this irrigation valve? The actuator timeout will limit the run.'))return;
  btn.disabled=true;
  try{await fetchJSON('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({node_id:id,value:!isOpen})});showToast('Valve command queued. Waiting for the actuator response.');}
  catch(ex){showToast('Command failed: '+ex.message,'error');}
  finally{btn.disabled=false;}
}
function provisionDone(m){
  const el=document.getElementById('provision-status');
  el.textContent=m.status==='ok'?'Node 0x'+m.id.toString(16).padStart(4,'0')+' ('+m.node_type+') registered!':'Failed: '+m.status;
  el.style.color=m.status==='ok'?'var(--success)':'var(--danger)';
  if(m.status==='ok')showToast('Node 0x'+m.id.toString(16).padStart(4,'0')+' provisioned as '+m.node_type,'success');
  fetchNodes();
}
async function fetchNodes(){
  try{cachedNodes=await fetchJSON('/api/nodes');renderNodes(cachedNodes);
    updateAIEmptyState();
  }catch(ex){console.error(ex)}
}
function nodeStatus(id){
  const last=sensorLastSeen[id];if(!last)return'○ Offline';
  const ago=(Date.now()-last)/1000;
  const node=cachedNodes.find(n=>n.id===id);const limit=node&&node.type===1?180:75;
  return ago<limit?'● Online':'○ Offline';
}
function updateNodeStatuses(){
  Object.keys(sensorCards).forEach(id=>textAt('sst-'+id,nodeStatus(Number(id))));
  Object.keys(actuatorCards).forEach(id=>{
    const button=document.getElementById('abtn-'+id);
    if(button)button.disabled=nodeStatus(Number(id)).includes('Offline');
  });
  const t=document.getElementById('nodes-tbody');
  if(!t)return;const rows=t.children;
  for(let i=0;i<rows.length;i++){
    const id=parseInt(rows[i].dataset.nid,16);if(!id)continue;
    const st=rows[i].querySelector('.node-status');
    if(st){
      const last=sensorLastSeen[id];
      if(!last){st.textContent='○ Offline';st.className='node-offline';continue}
      const ago=(Date.now()-last)/1000;
      const node=cachedNodes.find(n=>n.id===id);const limit=node&&node.type===1?180:75;
      st.textContent=ago<limit?'● Online':'○ Offline';
      st.className=ago<limit?'node-online':'node-offline';
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
    if(!validNodeId(n.id))return;
    r.innerHTML='<td>0x'+n.id.toString(16).padStart(4,'0')+'</td>'
      +'<td><span class="node-status" id="ns-'+n.id+'">'+st+'</span></td>'
      +'<td><span class="tag '+(n.type===2?'tag-actuator':'tag-sensor')+'">'+['','sensor','actuator'][n.type]+'</span></td>'
      +'<td class="node-alias"></td>'
      +'<td>'+n.lastSeq+'</td>'
      +'<td>'+(n.type===2?(n.autoMode?'<span style="color:var(--success)">ON</span>':'OFF'):'-')+'</td>'
      +'<td>'+(n.type===2?n.threshold:'-')+'</td>'
      +'<td><button class="remove-btn" aria-label="Remove node '+n.id.toString(16).padStart(4,'0')+'" onclick="removeNode('+n.id+')">Remove</button></td>';
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
  const weakKey=/^0{32}$/i.test(psk)||/^f{32}$/i.test(psk);
  if(!id||id>=0xFFFF||isNaN(id)||!/^[0-9a-fA-F]{32}$/.test(psk)||weakKey){showToast('Invalid node ID or PSK; use a unique random 32-character hexadecimal key','error');return;}
  if(!ws||ws.readyState!==WebSocket.OPEN){showToast('Reconnect to the gateway before registering a node.','error');return;}
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({action:'add_node',node_id:id,psk:psk,node_type:type,alias:alias}));
  document.getElementById('provision-status').textContent='Sending provisioning request for node 0x'+id.toString(16).padStart(4,'0')+'...';
  showToast('Provisioning node 0x'+id.toString(16).padStart(4,'0')+'...','info');
});
const otaForm=document.getElementById('ota-form');
if(otaForm)otaForm.addEventListener('submit',async e=>{
  e.preventDefault();const file=e.target.querySelector('input[type=file]').files[0];
  if(!file)return;const fd=new FormData();fd.append('firmware',file);
  document.getElementById('ota-progress').textContent='Uploading...';
  showToast('OTA firmware upload started','info');
  try{const r=await fetch('/api/ota/upload',{method:'POST',body:fd});const j=await r.json();if(!r.ok)throw new Error(j.msg||'Upload rejected');document.getElementById('ota-progress').textContent=j.msg||'Update triggered';showToast('Firmware verified; gateway restarting.','success')}catch(ex){document.getElementById('ota-progress').textContent='Error: '+ex.message;showToast('OTA error: '+ex.message,'error')}
});

async function fetchCloudCfg(){
  try{
    const cfg=await fetchJSON('/api/cloud-config');
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
      document.getElementById('cloud-status').style.color='var(--success)';
      showToast('Cloud configuration saved','success');
    } else {
      document.getElementById('cloud-status').textContent='Failed to save';
      document.getElementById('cloud-status').style.color='var(--danger)';
    }
  }catch(ex){
    document.getElementById('cloud-status').textContent='Error saving config';
  }
});

let fieldConfigs=[];
async function fetchDatasetStatus(){
  try{
    const s=await fetchJSON('/api/dataset/status');
    document.getElementById('dataset-status').textContent='Current records: '+s.records+' · Dropped: '+s.dropped+' · Current: '+(s.bytes/1024).toFixed(1)+' KB · Previous: '+((s.archive_bytes||0)/1024).toFixed(1)+' KB';
  }catch(ex){document.getElementById('dataset-status').textContent='Dataset status unavailable'}
}
async function fetchFieldConfigs(){
  try{fieldConfigs=await fetchJSON('/api/field-config');loadFieldConfig()}catch(ex){console.error(ex)}
}
function loadFieldConfig(){
  const id=parseInt(document.getElementById('fc-node').value,16);
  const c=fieldConfigs.find(x=>x.node_id===id);if(!c)return;
  document.getElementById('fc-dry').value=c.dry_raw;document.getElementById('fc-wet').value=c.wet_raw;
  document.getElementById('fc-crop').value=c.crop;document.getElementById('fc-stage').value=c.growth_stage;
  document.getElementById('fc-soil').value=c.soil_type;document.getElementById('fc-area').value=c.zone_area_m2;
  document.getElementById('fc-emitter').value=c.emitter_flow_lph;
}
document.getElementById('fc-node').addEventListener('change',loadFieldConfig);
document.getElementById('field-config-form').addEventListener('submit',async e=>{
  e.preventDefault();
  const body={node_id:parseInt(document.getElementById('fc-node').value,16),dry_raw:+document.getElementById('fc-dry').value,
    wet_raw:+document.getElementById('fc-wet').value,crop:document.getElementById('fc-crop').value.trim(),growth_stage:document.getElementById('fc-stage').value.trim(),
    soil_type:document.getElementById('fc-soil').value.trim(),zone_area_m2:+document.getElementById('fc-area').value,emitter_flow_lph:+document.getElementById('fc-emitter').value};
  const r=await fetch('/api/field-config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  document.getElementById('field-config-status').textContent=r.ok?'Saved':'Invalid profile';
  if(r.ok){showToast('Field profile saved','success');fetchFieldConfigs()}
});
document.getElementById('dataset-label-form').addEventListener('submit',async e=>{
  e.preventDefault();
  const body={label:document.getElementById('dataset-label').value,notes:document.getElementById('dataset-notes').value.trim()};
  const r=await fetch('/api/dataset/label',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  document.getElementById('dataset-label-status').textContent=r.ok?'Marker saved':'Failed';
  if(r.ok){showToast('Dataset event marker saved','success');document.getElementById('dataset-notes').value='';fetchDatasetStatus()}
});
document.getElementById('dataset-download').addEventListener('click',()=>{window.location='/api/dataset/export'});
document.getElementById('dataset-download-archive').addEventListener('click',()=>{window.location='/api/dataset/export?archive=1'});
document.getElementById('dataset-clear').addEventListener('click',async()=>{
  if(!confirm('Delete the complete local training dataset? Download it first.'))return;
  const r=await fetch('/api/dataset',{method:'DELETE'});
  if(r.ok){showToast('Dataset cleared','warn');fetchDatasetStatus()}
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
  try{
    const h=await fetchJSON('/api/health');healthOK=true;lastHealthAt=Date.now();
    if(h.preview&&!document.getElementById('preview-notice')){
      const notice=document.createElement('div');notice.id='preview-notice';notice.className='preview-notice';notice.textContent='Design preview · Demo data · No hardware connected';document.body.prepend(notice);
    }
    const restarted=(lastBootId!==null&&h.boot_id!==undefined&&h.boot_id!==lastBootId)||(lastUptime!==null&&h.uptime<lastUptime);
    if(restarted){resetTelemetry();showToast('Gateway restarted. Waiting for fresh observations.','warn');}
    if(h.boot_id!==undefined)lastBootId=h.boot_id;
    lastUptime=h.uptime;
    textAt('gw-uptime',h.uptime);textAt('gw-heap',Number.isFinite(h.free_heap)?(h.free_heap/1024).toFixed(0):'—');textAt('gw-rssi',h.rssi);
    const diagnostics=[];
    if(h.reset_reason!==undefined)diagnostics.push('Last reset: '+h.reset_reason);
    if(h.min_free_heap!==undefined)diagnostics.push('Minimum free memory: '+Math.round(h.min_free_heap/1024)+' KB');
    if(h.lora_ready!==undefined)diagnostics.push('LoRa: '+(h.lora_ready?'ready':'unavailable'));
    textAt('gateway-diagnostics',diagnostics.join(' · '));
    updateConnection();return true;
  }catch(ex){healthOK=false;updateConnection();return false;}
}
function resetTelemetry(){
  [sensorCards,actuatorCards,aiZoneCards].forEach(cards=>Object.keys(cards).forEach(id=>{cards[id].remove();delete cards[id];}));
  [sensorLastSeen,sensorHistory,actuatorStates,aiPredictions].forEach(values=>Object.keys(values).forEach(key=>delete values[key]));
  updateWeather({});updateAIWeather({valid:false});
  textAt('weather-updated','Waiting for a fresh station observation');
  for(const [holderId,message] of [['sensor-cards','Waiting for a fresh soil reading.'],['actuator-cards','Waiting for an actuator heartbeat.']]){
    const div=document.createElement('div');div.className='empty-state';div.textContent=message;
    if(holderId==='sensor-cards')div.id='sensor-placeholder';document.getElementById(holderId).replaceChildren(div);
  }
  updateAIEmptyState();
}
async function fetchTelemetry(){
  if(!telemetrySupported)return;
  try{
    const snapshot=await fetchJSON('/api/telemetry');
    if(snapshot.weather)updateWeather(snapshot.weather);
    if(Array.isArray(snapshot.sensors))snapshot.sensors.forEach(updateSensor);
    if(Array.isArray(snapshot.actuators))snapshot.actuators.forEach(updateActuator);
  }catch(ex){if(ex.status===404)telemetrySupported=false;}
}
async function refreshDashboard(){
  if(refreshRunning||document.hidden)return;
  refreshRunning=true;clearTimeout(refreshTimer);
  const button=document.getElementById('refresh-btn');if(button)button.disabled=true;
  try{
    // One request at a time keeps load and allocations bounded on the gateway.
    if(await pollHealth()){
      await fetchNodes();await fetchTelemetry();await fetchAIStatus();
      if(activeView==='settings')await fetchDatasetStatus();
    }
    if(ws&&ws.readyState===WebSocket.OPEN&&Date.now()-lastFrameAt>75000)ws.close();
    updateNodeStatuses();
  }finally{
    refreshRunning=false;if(button)button.disabled=false;
    if(!document.hidden)refreshTimer=setTimeout(refreshDashboard,15000);
  }
}
function selectView(view,updateHash=true){
  if(!['overview','irrigation','devices','settings'].includes(view))view='overview';
  activeView=view;
  document.querySelectorAll('[data-view]').forEach(section=>{section.hidden=section.dataset.view!==view;});
  document.querySelectorAll('[data-nav]').forEach(link=>{
    const active=link.dataset.nav===view;link.classList.toggle('active',active);
    if(active)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current');
  });
  textAt('view-title',{overview:'Overview',irrigation:'Irrigation',devices:'Devices',settings:'Settings'}[view]);
  if(updateHash)history.replaceState(null,'','#'+view);
  window.scrollTo({top:0,behavior:'instant'});
  if(view==='settings')loadSettings();
}
let settingsLoading=false,settingsLoaded=false;
async function loadSettings(){
  if(settingsLoading||settingsLoaded)return;settingsLoading=true;
  try{await fetchCloudCfg();await fetchFieldConfigs();await fetchDatasetStatus();settingsLoaded=true;}finally{settingsLoading=false;}
}
document.addEventListener('DOMContentLoaded',()=>{
  document.querySelectorAll('[data-nav]').forEach(link=>link.addEventListener('click',e=>{e.preventDefault();selectView(link.dataset.nav);}));
  selectView(location.hash.slice(1),false);
  window.addEventListener('hashchange',()=>selectView(location.hash.slice(1),false));
  const refresh=document.getElementById('refresh-btn');if(refresh)refresh.addEventListener('click',refreshDashboard);
  connectWS();
  refreshDashboard();
  setInterval(updateNodeStatuses,10000);
  setInterval(updateClock,1000);
  updateClock();
  document.addEventListener('visibilitychange',()=>{
    if(document.hidden){clearTimeout(refreshTimer);clearTimeout(reconnectTimer);reconnectTimer=null;if(ws)ws.close();}
    else{reconnectDelay=1000;connectWS();refreshDashboard();}
  });
  window.addEventListener('online',()=>{connectWS();refreshDashboard();});
});
