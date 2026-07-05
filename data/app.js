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
    case'provisioned':provisionDone(m);break;
  }
}
function updateWeather(w){
  document.getElementById('rain').textContent=w.rain.toFixed(2);
  document.getElementById('wind').textContent=w.wind.toFixed(1);
  document.getElementById('wind-dir').textContent=w.dir;
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
      +'<div class="sensor-footer"><span>RSSI: <strong id="sr-'+s.id+'">--</strong></span><span>Seq: <strong id="sq-'+s.id+'">--</strong></span></div>';
    c.appendChild(card);sensorCards[s.id]=card;
  }
  const m=s.moisture.toFixed(1);
  document.getElementById('mb-'+s.id).style.width=Math.min(m,100)+'%';
  document.getElementById('mv-'+s.id).textContent=m+'%';
  const t=s.temp.toFixed(1);
  const tv=document.getElementById('tv-'+s.id);
  tv.textContent=t+'°C';
  tv.style.color=t<0?'#60a5fa':t>30?'#f87171':'#e0e4ec';
  const b=s.batt.toFixed(2);
  document.getElementById('bb-'+s.id).style.width=Math.min((b/4.2)*100,100)+'%';
  document.getElementById('bb-'+s.id).style.background=b<3.3?'#f87171':b<3.6?'#fbbf24':'#4ade80';
  document.getElementById('bv-'+s.id).textContent=b+'V';
  if(s.rssi!==undefined)document.getElementById('sr-'+s.id).textContent=s.rssi+' dBm';
  document.getElementById('sq-'+s.id).textContent=s.seq;
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
function provisionDone(m){
  const el=document.getElementById('provision-status');
  el.textContent=m.status==='ok'?'Node 0x'+m.id.toString(16).padStart(4,'0')+' ('+m.node_type+') registered!':'Failed: '+m.status;
  el.style.color=m.status==='ok'?'#4ade80':'#f87171';
  fetchNodes();
}
async function fetchNodes(){
  try{const r=await fetch('/api/nodes');const nodes=await r.json();renderNodes(nodes)}catch(ex){console.error(ex)}
}
function nodeStatus(id){
  const last=sensorLastSeen[id];if(!last)return'○ Offline';
  const ago=(Date.now()-last)/1000;
  return ago<600?'● Online':'○ Offline';
}
function updateNodeStatuses(){
  const t=document.getElementById('nodes-tbody');
  if(!t)return;
  const rows=t.children;
  for(let i=0;i<rows.length;i++){
    const id=parseInt(rows[i].dataset.nid,16);
    if(!id)continue;
    const st=rows[i].querySelector('.node-status');
    if(st){
      const last=sensorLastSeen[id];
      if(!last){st.textContent='○ Offline';st.style.color='#f87171';continue}
      const ago=(Date.now()-last)/1000;
      st.textContent=ago<600?'● Online':'○ Offline';
      st.style.color=ago<600?'#4ade80':'#f87171';
    }
  }
}
function removeNode(id){
  if(!confirm('Remove node 0x'+id.toString(16).padStart(4,'0')+'?'))return;
  ws.send(JSON.stringify({action:'remove_node',node_id:id}));
}
function renderNodes(nodes){
  const t=document.getElementById('nodes-tbody');t.innerHTML='';
  nodes.forEach(n=>{
    const r=document.createElement('tr');r.dataset.nid='0x'+n.id.toString(16).padStart(4,'0');
    const st=nodeStatus(n.id);
    r.innerHTML='<td>0x'+n.id.toString(16).padStart(4,'0')+'</td>'
      +'<td><span class="node-status" id="ns-'+n.id+'">'+st+'</span></td>'
      +'<td>'+['','sensor','actuator'][n.type]+'</td>'
      +'<td>'+n.alias+'</td>'
      +'<td>'+n.lastSeq+'</td>'
      +'<td><button class="remove-btn" onclick="removeNode('+n.id+')">✕</button></td>';
    t.appendChild(r);
    const el=document.getElementById('ns-'+n.id);
    if(el)el.style.color=st.indexOf('Online')>=0?'#4ade80':'#f87171';
  });
}
document.getElementById('provision-form').addEventListener('submit',e=>{
  e.preventDefault();
  const id=parseInt(document.getElementById('p-id').value,16);
  const psk=document.getElementById('p-psk').value.trim();
  const type=document.getElementById('p-type').value;
  const alias=document.getElementById('p-alias').value.trim()||'unnamed';
  if(!id||isNaN(id)||psk.length!==32)return;
  ws.send(JSON.stringify({action:'add_node',node_id:id,psk:psk,node_type:type,alias:alias}));
  document.getElementById('provision-status').textContent='Sending provisioning request for node 0x'+id.toString(16).padStart(4,'0')+'...';
});
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
document.addEventListener('DOMContentLoaded',()=>{connectWS();setInterval(pollHealth,15000);setInterval(fetchNodes,10000);setInterval(updateNodeStatuses,5000);fetchNodes()});
