let ws=null;
const MAX_LOG=200;
function connectWS(){
  const p=location.protocol==='https:'?'wss:':'ws:';
  ws=new WebSocket(p+'//'+location.host+'/ws');
  ws.onopen=()=>{document.getElementById('health-status').textContent='● Connected';document.getElementById('health-status').style.color='#4ade80'};
  ws.onclose=()=>{document.getElementById('health-status').textContent='○ Disconnected';document.getElementById('health-status').style.color='#f87171';setTimeout(connectWS,3000)};
  ws.onmessage=e=>{try{handleMessage(JSON.parse(e.data))}catch(ex){console.error(ex)}};
}
function handleMessage(m){
  switch(m.type){
    case'sensor':updateSensor(m);break;
    case'weather':updateWeather(m);break;
    case'actuator':updateActuator(m);break;
    case'log':appendLog(m);break;
  }
}
function updateWeather(w){
  document.getElementById('rain').textContent=w.rain.toFixed(2);
  document.getElementById('wind').textContent=w.wind.toFixed(1);
  document.getElementById('wind-dir').textContent=w.dir;
}
const sensorRows={};
function updateSensor(s){
  const t=document.getElementById('sensor-tbody');
  let r=sensorRows[s.id];
  if(!r){r=document.createElement('tr');r.innerHTML='<td>'+s.id+'</td><td class="m">--</td><td class="t">--</td><td class="b">--</td><td class="q">--</td><td class="r">--</td>';t.appendChild(r);sensorRows[s.id]=r}
  r.cells[1].textContent=s.moisture.toFixed(1)+'%';
  r.cells[2].textContent=s.temp.toFixed(2)+'°C';
  r.cells[3].textContent=s.batt.toFixed(2)+'V';
  r.cells[4].textContent=s.seq;
  if(s.rssi!==undefined)r.cells[5].textContent=s.rssi+' dBm';
}
const actuatorCards={};
function updateActuator(a){
  const c=document.getElementById('actuator-cards');
  let card=actuatorCards[a.id];
  if(!card){
    card=document.createElement('div');card.className='actuator-card';
    card.innerHTML='<h3>Actuator '+a.id+'</h3><div class="state" id="ast-'+a.id+'">--</div><button class="toggle-btn" id="abtn-'+a.id+'" data-id="'+a.id+'">Toggle</button><div style="margin-top:.5rem;font-size:.8rem;color:#6a7486">Batt: <span id="abatt-'+a.id+'">--</span></div>';
    c.appendChild(card);actuatorCards[a.id]=card;
    document.getElementById('abtn-'+a.id).addEventListener('click',()=>toggleAct(a.id));
  }
  document.getElementById('ast-'+a.id).textContent=a.valve?'● OPEN':'○ CLOSED';
  document.getElementById('ast-'+a.id).style.color=a.valve?'#4ade80':'#f87171';
  document.getElementById('abatt-'+a.id).textContent=a.batt.toFixed(2)+'V';
  const btn=document.getElementById('abtn-'+a.id);
  btn.textContent=a.valve?'Turn OFF':'Turn ON';
  btn.className='toggle-btn '+(a.valve?'on':'off');
}
function toggleAct(id){
  fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({node_id:id,value:true})});
}
document.getElementById('ota-form').addEventListener('submit',async e=>{
  e.preventDefault();const file=e.target.querySelector('input[type=file]').files[0];
  if(!file)return;const fd=new FormData();fd.append('firmware',file);
  document.getElementById('ota-progress').textContent='Uploading...';
  try{const r=await fetch('/api/ota/upload',{method:'POST',body:fd});const j=await r.json();document.getElementById('ota-progress').textContent=j.msg||'Update triggered'}catch(ex){document.getElementById('ota-progress').textContent='Error: '+ex.message}
});
function appendLog(l){
  const el=document.getElementById('log-output');
  const line='['+new Date().toLocaleTimeString()+'] ['+l.level+'] '+l.msg;
  el.appendChild(document.createTextNode(line+'\n'));
  if(el.children.length>MAX_LOG)el.removeChild(el.firstChild);
  el.scrollTop=el.scrollHeight;
}
async function pollHealth(){
  try{const r=await fetch('/api/health');const h=await r.json();document.getElementById('health-status').textContent='● Uptime: '+h.uptime+'s Free: '+(h.free_heap/1024).toFixed(0)+'KB RSSI: '+h.rssi+'dBm'}catch(ex){}
}
document.addEventListener('DOMContentLoaded',()=>{connectWS();setInterval(pollHealth,15000)});
