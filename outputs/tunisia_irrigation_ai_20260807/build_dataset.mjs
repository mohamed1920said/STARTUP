import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Workbook, SpreadsheetFile } from "@oai/artifact-tool";

const outputDir = path.dirname(fileURLToPath(import.meta.url));
const workbookPath = path.join(outputDir, "tunisia_irrigation_ai_dataset_2023_2025.xlsx");
const csvPath = path.join(outputDir, "tunisia_irrigation_ai_training_dataset_2023_2025.csv");
const apiDocs = "https://open-meteo.com/en/docs/historical-weather-api";
const apiBase = "https://archive-api.open-meteo.com/v1/archive";
const licenceUrl = "https://open-meteo.com/en/license";
const startDate = "2023-01-01";
const endDate = "2025-12-31";

const regions = [
  {region:"Jendouba", latitude:36.5011, longitude:8.7802, sensorId:1, actuatorId:257, crop:"wheat", soil:"clay_loam", area:1000, flowLph:5000, threshold:40, rootDepth:900, maxApplication:25, fc:0.36, wp:0.17,
   kc:{initial:0.45,development:0.75,mid:1.10,late:0.40,fallow:0.25,dormancy:0.25}, stages:["initial","development","development","mid","mid","late","fallow","fallow","fallow","fallow","initial","initial"]},
  {region:"Bizerte", latitude:37.2744, longitude:9.8739, sensorId:2, actuatorId:258, crop:"potato", soil:"loam", area:500, flowLph:3000, threshold:45, rootDepth:600, maxApplication:20, fc:0.30, wp:0.12,
   kc:{initial:0.50,development:0.80,mid:1.15,late:0.75,fallow:0.25,dormancy:0.25}, stages:["development","mid","late","fallow","fallow","fallow","fallow","initial","development","mid","mid","late"]},
  {region:"Nabeul", latitude:36.4561, longitude:10.7376, sensorId:3, actuatorId:259, crop:"citrus", soil:"sandy_loam", area:600, flowLph:2400, threshold:42, rootDepth:1200, maxApplication:24, fc:0.23, wp:0.10,
   kc:{initial:0.65,development:0.80,mid:0.95,late:0.75,fallow:0.60,dormancy:0.60}, stages:["late","late","initial","development","development","mid","mid","mid","mid","late","late","late"]},
  {region:"Kairouan", latitude:35.6781, longitude:10.0963, sensorId:4, actuatorId:260, crop:"tomato", soil:"sandy_loam", area:400, flowLph:2400, threshold:48, rootDepth:700, maxApplication:20, fc:0.23, wp:0.10,
   kc:{initial:0.60,development:0.85,mid:1.15,late:0.80,fallow:0.25,dormancy:0.25}, stages:["fallow","fallow","initial","development","development","mid","mid","late","late","fallow","fallow","fallow"]},
  {region:"Sfax", latitude:34.7406, longitude:10.7603, sensorId:5, actuatorId:261, crop:"olive", soil:"clay_loam", area:2000, flowLph:3200, threshold:28, rootDepth:1500, maxApplication:28, fc:0.36, wp:0.17,
   kc:{initial:0.45,development:0.55,mid:0.70,late:0.60,fallow:0.40,dormancy:0.40}, stages:["dormancy","dormancy","initial","development","mid","mid","mid","mid","late","late","late","dormancy"]},
  {region:"Tozeur", latitude:33.9197, longitude:8.1335, sensorId:6, actuatorId:262, crop:"date_palm", soil:"sand", area:800, flowLph:2400, threshold:38, rootDepth:1400, maxApplication:30, fc:0.18, wp:0.07,
   kc:{initial:0.70,development:0.85,mid:1.00,late:0.85,fallow:0.60,dormancy:0.60}, stages:["dormancy","initial","development","development","mid","mid","mid","mid","late","late","dormancy","dormancy"]},
];

const dailyVars = [
  "temperature_2m_mean","temperature_2m_max","temperature_2m_min",
  "relative_humidity_2m_mean","pressure_msl_mean","precipitation_sum","rain_sum",
  "wind_speed_10m_mean","wind_speed_10m_max","wind_direction_10m_dominant",
  "shortwave_radiation_sum","et0_fao_evapotranspiration"
];
const hourlyVars = [
  "soil_temperature_0_to_7cm","soil_moisture_0_to_7cm",
  "soil_moisture_7_to_28cm","vapour_pressure_deficit"
];

const trainingHeaders = [
  "schema_version","record_id","date_local","epoch_ms","year","day_of_year","region","latitude","longitude","elevation_m",
  "sensor_node_id","linked_actuator_id","sequence","boot_id","crop","growth_stage","soil_type","zone_area_m2","emitter_flow_lph",
  "temperature_mean_c","temperature_max_c","temperature_min_c","relative_humidity_mean_pct","pressure_msl_mean_hpa","precipitation_mm","rain_mm",
  "wind_speed_mean_ms","wind_speed_max_ms","wind_direction_deg","shortwave_radiation_mj_m2","et0_mm","vapor_pressure_deficit_kpa",
  "soil_temperature_0_7cm_c","era5_soil_moisture_0_7_vwc","era5_soil_moisture_7_28_vwc","field_soil_moisture_vwc_sim",
  "moisture_pct_sim","moisture_raw_sim","moisture_threshold_pct","crop_coefficient_kc","crop_water_demand_mm","irrigation_needed_next_day",
  "recommended_water_mm","valve_commanded","valve_actual","feedback_valid","command_result","command_source","irrigation_runtime_min",
  "flow_lpm","line_pressure_bar","tank_pct","pump_current_a","node_battery_v","gateway_battery_mv","rssi_dbm","anomaly_label","error_flags",
  "actual_delivered_water_mm","data_origin_weather","data_origin_device","is_synthetic_device","training_use","source_url"
];

const rawHeaders = [
  "date_local","region","latitude","longitude","grid_latitude","grid_longitude","elevation_m","timezone","model",
  "temperature_mean_c","temperature_max_c","temperature_min_c","relative_humidity_mean_pct","pressure_msl_mean_hpa",
  "precipitation_mm","rain_mm","wind_speed_mean_ms","wind_speed_max_ms","wind_direction_deg","shortwave_radiation_mj_m2","et0_mm",
  "vapor_pressure_deficit_kpa","soil_temperature_0_7cm_c","soil_moisture_0_7_vwc","soil_moisture_7_28_vwc","source_url"
];

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const round = (v, d=3) => v == null || !Number.isFinite(v) ? null : Number(v.toFixed(d));
const avg = arr => {
  const good = arr.filter(Number.isFinite);
  return good.length ? good.reduce((a,b)=>a+b,0)/good.length : null;
};
const dayOfYear = date => Math.floor((Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate()) - Date.UTC(date.getUTCFullYear(),0,0))/86400000);
const excelCol = index => {
  let n=index+1, s="";
  while(n){n--;s=String.fromCharCode(65+n%26)+s;n=Math.floor(n/26);}return s;
};

function makeUrl(region) {
  const q = new URLSearchParams({
    latitude:String(region.latitude), longitude:String(region.longitude),
    start_date:startDate, end_date:endDate, daily:dailyVars.join(","),
    hourly:hourlyVars.join(","), timezone:"Africa/Tunis", wind_speed_unit:"ms", models:"era5"
  });
  return `${apiBase}?${q.toString()}`;
}

async function fetchJson(url, attempts=6) {
  let last;
  for (let i=0;i<attempts;i++) {
    try {
      const response = await fetch(url, {headers:{"User-Agent":"AMR-Tunisia-Dataset/1.0"}});
      if (response.status===429 && i<attempts-1) {
        await response.text();
        await new Promise(resolve=>setTimeout(resolve,30000));
        continue;
      }
      if (!response.ok) throw new Error(`HTTP ${response.status}: ${await response.text()}`);
      return await response.json();
    } catch (error) {
      last=error;
      await new Promise(resolve=>setTimeout(resolve,1000*(i+1)));
    }
  }
  throw last;
}

function aggregateHourly(hourly) {
  const map = new Map();
  for (let i=0;i<hourly.time.length;i++) {
    const date=hourly.time[i].slice(0,10);
    if (!map.has(date)) map.set(date,{soilT:[],soil0:[],soil28:[],vpd:[]});
    const row=map.get(date);
    row.soilT.push(hourly.soil_temperature_0_to_7cm[i]);
    row.soil0.push(hourly.soil_moisture_0_to_7cm[i]);
    row.soil28.push(hourly.soil_moisture_7_to_28cm[i]);
    row.vpd.push(hourly.vapour_pressure_deficit[i]);
  }
  for (const [date,row] of map) map.set(date,{soilT:avg(row.soilT),soil0:avg(row.soil0),soil28:avg(row.soil28),vpd:avg(row.vpd)});
  return map;
}

function anomalyFor(regionIndex, date, irrigation) {
  const doy=dayOfYear(date), key=(date.getUTCFullYear()*17+doy*31+regionIndex*97)>>>0;
  if (key%251===0) return "sensor_fault";
  if (!irrigation) return "normal";
  if (key%173===0) return "leak";
  if (key%149===0) return "blocked_pipe";
  if (key%197===0) return "empty_tank";
  if (key%227===0) return "valve_fault";
  return "normal";
}

const rawRows=[];
const trainingRows=[];
const sourceRows=[];
let missingWeather=0;

for (let regionIndex=0; regionIndex<regions.length; regionIndex++) {
  const region=regions[regionIndex];
  const url=makeUrl(region);
  const payload=await fetchJson(url);
  const hourly=aggregateHourly(payload.hourly);
  sourceRows.push([region.region,region.latitude,region.longitude,payload.latitude,payload.longitude,payload.elevation,url]);
  let fieldVwc=null;
  for (let i=0;i<payload.daily.time.length;i++) {
    const dateText=payload.daily.time[i];
    const date=new Date(`${dateText}T00:00:00Z`);
    const hourlyDay=hourly.get(dateText) || {};
    const daily={};
    for (const key of dailyVars) daily[key]=payload.daily[key]?.[i] ?? null;
    const weatherValues=[...Object.values(daily),hourlyDay.soilT,hourlyDay.soil0,hourlyDay.soil28,hourlyDay.vpd];
    if (weatherValues.some(v=>v==null || !Number.isFinite(v))) missingWeather++;
    rawRows.push([
      date,region.region,region.latitude,region.longitude,payload.latitude,payload.longitude,payload.elevation,payload.timezone,"ERA5",
      daily.temperature_2m_mean,daily.temperature_2m_max,daily.temperature_2m_min,daily.relative_humidity_2m_mean,daily.pressure_msl_mean,
      daily.precipitation_sum,daily.rain_sum,daily.wind_speed_10m_mean,daily.wind_speed_10m_max,daily.wind_direction_10m_dominant,
      daily.shortwave_radiation_sum,daily.et0_fao_evapotranspiration,hourlyDay.vpd,hourlyDay.soilT,hourlyDay.soil0,hourlyDay.soil28,apiDocs
    ]);

    if (!Number.isFinite(fieldVwc)) fieldVwc=Number.isFinite(hourlyDay.soil28)?hourlyDay.soil28:(region.wp+region.fc)/2;
    const stage=region.stages[date.getUTCMonth()];
    const kc=region.kc[stage] ?? region.kc.fallow;
    const cropDemand=Math.max(0,(daily.et0_fao_evapotranspiration ?? 0)*kc);
    const effectiveRain=Math.max(0,(daily.rain_sum ?? 0)*0.80);
    const naturalEnd=clamp(fieldVwc+(effectiveRain-cropDemand)/region.rootDepth,region.wp*0.85,region.fc);
    const moisturePct=clamp((naturalEnd-region.wp)/(region.fc-region.wp)*100,0,100);
    const active=!['fallow','dormancy'].includes(stage);
    const irrigation=active && moisturePct<region.threshold;
    const targetPct=Math.min(85,region.threshold+20);
    const targetVwc=region.wp+(region.fc-region.wp)*(targetPct/100);
    const required=irrigation?clamp((targetVwc-naturalEnd)*region.rootDepth+cropDemand,0,region.maxApplication):0;
    const runtime=irrigation?required*region.area/region.flowLph*60:0;
    const anomaly=anomalyFor(regionIndex,date,irrigation);
    let valveActual=irrigation?1:0, feedbackValid=1, result=irrigation?0:-1;
    let flow=irrigation?region.flowLph/60:0, pressure=irrigation?2.4:2.2, current=irrigation?3.2:0;
    let tank=clamp(90-((dayOfYear(date)*3+regionIndex*13)%60),25,95);
    let errorFlags=0;
    if (anomaly==='sensor_fault') errorFlags=1;
    if (anomaly==='leak') {flow*=1.70;pressure=1.2;}
    if (anomaly==='blocked_pipe') {flow*=0.15;pressure=3.8;}
    if (anomaly==='empty_tank') {flow=0;pressure=0.2;tank=0;current=2.8;result=1;}
    if (anomaly==='valve_fault') {valveActual=0;flow=0;pressure=0;current=0;result=1;errorFlags=2;}
    const delivered=irrigation?flow*runtime/region.area:0;
    fieldVwc=clamp(naturalEnd+delivered/region.rootDepth,region.wp*0.85,region.fc);
    const dryRaw=2500, wetRaw=400;
    const moistureRaw=Math.round(dryRaw-(moisturePct/100)*(dryRaw-wetRaw));
    const doy=dayOfYear(date);
    const sequence=(date.getUTCFullYear()-2023)*366+doy;
    const bootId=(0xA0000000+region.sensorId*0x10000+date.getUTCFullYear())>>>0;
    const epochMs=Date.UTC(date.getUTCFullYear(),date.getUTCMonth(),date.getUTCDate(),12,0,0)-3600000;
    const nodeBattery=4.08-(doy%120)*0.005;
    const rssi=-65-((doy*7+regionIndex*11)%35);
    const recordId=`TN-${region.region.slice(0,3).toUpperCase()}-${dateText}`;
    trainingRows.push([
      2,recordId,date,epochMs,date.getUTCFullYear(),doy,region.region,region.latitude,region.longitude,payload.elevation,
      region.sensorId,region.actuatorId,sequence,bootId,region.crop,stage,region.soil,region.area,region.flowLph,
      daily.temperature_2m_mean,daily.temperature_2m_max,daily.temperature_2m_min,daily.relative_humidity_2m_mean,daily.pressure_msl_mean,
      daily.precipitation_sum,daily.rain_sum,daily.wind_speed_10m_mean,daily.wind_speed_10m_max,daily.wind_direction_10m_dominant,
      daily.shortwave_radiation_sum,daily.et0_fao_evapotranspiration,hourlyDay.vpd,hourlyDay.soilT,hourlyDay.soil0,hourlyDay.soil28,
      round(naturalEnd,4),round(moisturePct,2),moistureRaw,region.threshold,kc,round(cropDemand,2),irrigation?1:0,round(required,2),
      irrigation?1:0,valveActual,feedbackValid,result,irrigation?"simulation_auto":"none",round(runtime,2),round(flow,2),round(pressure,2),
      round(tank,1),round(current,2),round(nodeBattery,3),5000,rssi,anomaly,errorFlags,round(delivered,2),
      "Open-Meteo ERA5 reanalysis","deterministic water-balance simulation v1",true,"prototype_only_not_field_validated",apiDocs
    ]);
  }
}

if (rawRows.length!==trainingRows.length) throw new Error("Raw/training row count mismatch");
const expectedDays=Math.round((Date.parse(`${endDate}T00:00:00Z`)-Date.parse(`${startDate}T00:00:00Z`))/86400000)+1;
if (trainingRows.length!==expectedDays*regions.length) throw new Error(`Expected ${expectedDays*regions.length} rows, got ${trainingRows.length}`);

const dictionary = [
  ["schema_version","integer","version","Generated","Dataset schema; 2 matches the gateway's current dataset generation."],
  ["record_id","text","identifier","Generated","Unique region-date key."],
  ["date_local","date","Africa/Tunis","Open-Meteo","Local calendar day."],
  ["epoch_ms","integer","ms since Unix epoch","Generated","Local noon converted to UTC; synthetic timing anchor."],
  ["year","integer","year","Generated","Calendar year."],
  ["day_of_year","integer","1-366","Generated","Seasonality feature."],
  ["region","text","name","Scenario","Representative Tunisian agricultural region."],
  ["latitude","number","degrees","Scenario","Requested WGS84 latitude."],
  ["longitude","number","degrees","Scenario","Requested WGS84 longitude."],
  ["elevation_m","number","m","Open-Meteo","Elevation used by the reanalysis grid."],
  ["sensor_node_id","integer","uint16","Scenario","Prototype sensor identifier."],
  ["linked_actuator_id","integer","uint16","Scenario","Prototype linked actuator identifier."],
  ["sequence","integer","count","Generated","Deterministic daily sequence; not a real LoRa counter."],
  ["boot_id","integer","uint32","Generated","Synthetic traceability identifier."],
  ["crop","text","category","Scenario","Representative crop, not a surveyed farm record."],
  ["growth_stage","text","category","Scenario","Month-based simulated stage."],
  ["soil_type","text","category","Scenario","Assumed soil texture."],
  ["zone_area_m2","number","m²","Scenario","Assumed irrigated zone area."],
  ["emitter_flow_lph","number","L/h","Scenario","Assumed total zone flow capacity."],
  ["temperature_mean_c","number","°C","Open-Meteo ERA5","Daily 2 m mean."],
  ["temperature_max_c","number","°C","Open-Meteo ERA5","Daily 2 m maximum."],
  ["temperature_min_c","number","°C","Open-Meteo ERA5","Daily 2 m minimum."],
  ["relative_humidity_mean_pct","number","%","Open-Meteo ERA5","Daily mean relative humidity."],
  ["pressure_msl_mean_hpa","number","hPa","Open-Meteo ERA5","Daily mean sea-level pressure."],
  ["precipitation_mm","number","mm/day","Open-Meteo ERA5","Daily precipitation sum."],
  ["rain_mm","number","mm/day","Open-Meteo ERA5","Daily rain sum."],
  ["wind_speed_mean_ms","number","m/s","Open-Meteo ERA5","Daily mean 10 m wind speed."],
  ["wind_speed_max_ms","number","m/s","Open-Meteo ERA5","Daily maximum 10 m wind speed."],
  ["wind_direction_deg","number","degrees","Open-Meteo ERA5","Dominant 10 m wind direction."],
  ["shortwave_radiation_mj_m2","number","MJ/m²/day","Open-Meteo ERA5","Daily shortwave radiation sum."],
  ["et0_mm","number","mm/day","Open-Meteo ERA5","FAO reference evapotranspiration."],
  ["vapor_pressure_deficit_kpa","number","kPa","Open-Meteo ERA5","Mean of hourly VPD."],
  ["soil_temperature_0_7cm_c","number","°C","Open-Meteo ERA5","Mean of hourly shallow soil temperature."],
  ["era5_soil_moisture_0_7_vwc","number","m³/m³","Open-Meteo ERA5","Mean hourly shallow volumetric water content."],
  ["era5_soil_moisture_7_28_vwc","number","m³/m³","Open-Meteo ERA5","Mean hourly root-zone reference moisture."],
  ["field_soil_moisture_vwc_sim","number","m³/m³","Simulation","End-of-day field water-balance state before irrigation."],
  ["moisture_pct_sim","number","0-100%","Simulation","Field state mapped between assumed wilting point and field capacity."],
  ["moisture_raw_sim","integer","ADC 0-4095","Simulation","Inverse mapping using dry=2500 and wet=400."],
  ["moisture_threshold_pct","number","%","Scenario","Decision threshold."],
  ["crop_coefficient_kc","number","ratio","Scenario","Stage-dependent crop coefficient."],
  ["crop_water_demand_mm","number","mm/day","Derived","ET₀ × Kc."],
  ["irrigation_needed_next_day","integer","0/1","Simulation target","Next-day irrigation decision after daily water balance."],
  ["recommended_water_mm","number","mm","Simulation target","Water required toward target moisture, capped by max application."],
  ["valve_commanded","integer","0/1","Simulation","Synthetic actuator command."],
  ["valve_actual","integer","0/1","Simulation","Synthetic feedback state after anomaly injection."],
  ["feedback_valid","integer","0/1","Simulation","Synthetic feedback availability."],
  ["command_result","integer","-1/0/1","Simulation","-1 none, 0 success, 1 failure."],
  ["command_source","text","category","Simulation","Source of synthetic command."],
  ["irrigation_runtime_min","number","minutes","Derived","Recommended volume divided by nominal flow."],
  ["flow_lpm","number","L/min","Simulation","Hydraulic flow after anomaly injection."],
  ["line_pressure_bar","number","bar","Simulation","Hydraulic pressure after anomaly injection."],
  ["tank_pct","number","%","Simulation","Synthetic tank level."],
  ["pump_current_a","number","A","Simulation","Synthetic current."],
  ["node_battery_v","number","V","Simulation","Synthetic node battery cycle."],
  ["gateway_battery_mv","number","mV","Simulation","Fixed nominal gateway supply."],
  ["rssi_dbm","integer","dBm","Simulation","Synthetic radio strength."],
  ["anomaly_label","text","category","Simulation target","normal, leak, blocked_pipe, empty_tank, valve_fault or sensor_fault."],
  ["error_flags","integer","bitmask","Simulation","Firmware-style sensor/valve error bits only."],
  ["actual_delivered_water_mm","number","mm","Derived","Flow × runtime ÷ area after anomaly effects."],
  ["data_origin_weather","text","provenance","Generated","Identifies real reanalysis source."],
  ["data_origin_device","text","provenance","Generated","Identifies deterministic simulation."],
  ["is_synthetic_device","boolean","true/false","Generated","Always true in this prototype dataset."],
  ["training_use","text","governance","Generated","Prevents accidental production use."],
  ["source_url","text","URL","Generated","Historical API documentation/attribution link."],
];

const wb=Workbook.create();
const readme=wb.worksheets.add("README");
const training=wb.worksheets.add("Training Dataset");
const raw=wb.worksheets.add("Raw Weather");
const quality=wb.worksheets.add("Quality Summary");
const assumptions=wb.worksheets.add("Assumptions");
const dataDictionary=wb.worksheets.add("Data Dictionary");
const sources=wb.worksheets.add("Sources");

const green="#166534", dark="#0F3D2E", pale="#DCFCE7", cyan="#0E7490", light="#F8FAFC", amber="#D97706", red="#B91C1C", white="#FFFFFF", gray="#475569";
const titleFormat={fill:dark,font:{bold:true,color:white,size:16},verticalAlignment:"center"};
const headerFormat={fill:green,font:{bold:true,color:white},verticalAlignment:"center",wrapText:true,borders:{preset:"inside",style:"thin",color:"#86A58F"}};
const sectionFormat={fill:pale,font:{bold:true,color:dark}};

readme.showGridLines=false;
readme.getRange("A1:H2").merge();
readme.getRange("A1").values=[["Tunisia Irrigation Edge-AI Prototype Dataset"]];
readme.getRange("A1:H2").format=titleFormat;
readme.getRange("A4:B15").values=[
  ["Coverage",`${startDate} to ${endDate}; ${regions.length} regions; ${trainingRows.length.toLocaleString('en-US')} daily records`],
  ["Real source","Open-Meteo Historical Weather API, ERA5 reanalysis"],
  ["Real variables","Temperature, humidity, pressure, rain, wind, solar radiation, ET₀, VPD, soil temperature and soil moisture"],
  ["Synthetic variables","Field water balance, raw ADC, irrigation decisions, valve/ACK state, hydraulics, battery, RSSI and anomaly labels"],
  ["Training status","PROTOTYPE ONLY — NOT FIELD VALIDATED"],
  ["Recommended use","Schema testing, dashboard development, model-pipeline prototyping and shadow-mode experiments"],
  ["Prohibited use","Do not enable autonomous irrigation or commercial safety decisions from this dataset alone"],
  ["Required next step","Replace/augment synthetic columns with calibrated prototype measurements and farmer-verified event labels"],
  ["Weather source",apiDocs],
  ["Licence",`${licenceUrl} — CC BY 4.0 attribution required; check commercial API terms before resale`],
  ["Attribution","Weather data by Open-Meteo.com; transformed and combined with deterministic simulation"],
  ["Missing weather rows",missingWeather],
];
readme.getRange("A4:A15").format=sectionFormat;
readme.getRange("A4:B15").format.wrapText=true;
readme.getRange("A4:B15").format.borders={preset:"outside",style:"thin",color:"#94A3B8"};
readme.getRange("A17:H17").merge(); readme.getRange("A17").values=[["Workflow"]]; readme.getRange("A17:H17").format=sectionFormat;
readme.getRange("A18:H23").values=[
  ["1. Inspect Raw Weather before modeling.",null,null,null,null,null,null,null],
  ["2. Use Training Dataset only for pipeline/prototype work.",null,null,null,null,null,null,null],
  ["3. Split train/validation/test by date or region, never random adjacent rows.",null,null,null,null,null,null,null],
  ["4. Collect 4–8+ weeks of calibrated field data and verified faults.",null,null,null,null,null,null,null],
  ["5. Validate in shadow mode before allowing any model to control water.",null,null,null,null,null,null,null],
  ["6. Keep hard safety limits, valve feedback and hydraulic protection outside the AI model.",null,null,null,null,null,null,null],
];
readme.getRange("A18:H23").format.wrapText=true;
for(let row=18;row<=23;row++) readme.getRange(`A${row}:H${row}`).merge();
readme.getRange("A1:H23").format.font.name="Aptos";
readme.getRange("A1:A23").format.columnWidth=24; readme.getRange("B1:H23").format.columnWidth=18;
readme.getRange("B4:B15").format.columnWidth=90;
readme.getRange("A1:H2").format.rowHeight=30;

async function writeLargeSheet(sheet, headers, rows, tableName) {
  sheet.showGridLines=false;
  sheet.getRangeByIndexes(0,0,1,headers.length).values=[headers];
  const chunk=350;
  for(let start=0;start<rows.length;start+=chunk){
    const block=rows.slice(start,start+chunk);
    sheet.getRangeByIndexes(start+1,0,block.length,headers.length).values=block;
  }
  const lastRow=rows.length+1, lastCol=excelCol(headers.length-1);
  sheet.getRange(`A1:${lastCol}1`).format=headerFormat;
  sheet.getRange(`A2:${lastCol}${lastRow}`).format.font={name:"Aptos",size:9,color:"#0F172A"};
  sheet.getRange(`A2:${lastCol}${lastRow}`).format.borders={insideHorizontal:{style:"thin",color:"#E2E8F0"}};
  sheet.freezePanes.freezeRows(1); sheet.freezePanes.freezeColumns(3);
  const table=sheet.tables.add(`A1:${lastCol}${lastRow}`,true,tableName); table.style="TableStyleMedium4";
  for(let c=0;c<headers.length;c++){
    let width=12;
    if(["record_id","region","crop","growth_stage","soil_type","anomaly_label","training_use"].includes(headers[c])) width=20;
    if(headers[c].includes("source")||headers[c].includes("origin")) width=34;
    sheet.getRange(`${excelCol(c)}1:${excelCol(c)}${lastRow}`).format.columnWidth=width;
  }
  return lastRow;
}

const trainingLast=await writeLargeSheet(training,trainingHeaders,trainingRows,"TrainingDatasetTable");
const rawLast=await writeLargeSheet(raw,rawHeaders,rawRows,"RawWeatherTable");
const formatCol=(sheet,headers,name,last,fmt)=>{const c=excelCol(headers.indexOf(name));sheet.getRange(`${c}2:${c}${last}`).format.numberFormat=fmt;};
formatCol(training,trainingHeaders,"date_local",trainingLast,"yyyy-mm-dd");
formatCol(raw,rawHeaders,"date_local",rawLast,"yyyy-mm-dd");
for(const h of trainingHeaders){
  if(h.endsWith("_pct")||h.includes("humidity")||h==="moisture_pct_sim"||h==="tank_pct") formatCol(training,trainingHeaders,h,trainingLast,"0.0");
  else if(h.includes("latitude")||h.includes("longitude")||h.includes("_vwc")) formatCol(training,trainingHeaders,h,trainingLast,"0.0000");
  else if(h.includes("temperature")||h.includes("pressure")||h.includes("water_mm")||h.includes("runtime")||h.includes("flow_lpm")||h.includes("current_a")||h.includes("battery_v")||h.includes("et0")||h.includes("rain_mm")||h.includes("precipitation")) formatCol(training,trainingHeaders,h,trainingLast,"0.00");
}
for(const h of rawHeaders){
  if(h==="date_local") continue;
  if(h.includes("latitude")||h.includes("longitude")||h.includes("moisture")) formatCol(raw,rawHeaders,h,rawLast,"0.0000");
  else if(!["region","timezone","model","source_url"].includes(h)) formatCol(raw,rawHeaders,h,rawLast,"0.00");
}
const anomalyCol=excelCol(trainingHeaders.indexOf("anomaly_label"));
training.getRange(`${anomalyCol}2:${anomalyCol}${trainingLast}`).conditionalFormats.add("containsText",{text:"normal",format:{fill:"#ECFDF5",font:{color:green}}});
training.getRange(`${anomalyCol}2:${anomalyCol}${trainingLast}`).conditionalFormats.add("notContainsText",{text:"normal",format:{fill:"#FEE2E2",font:{color:red,bold:true}}});
const irrigCol=excelCol(trainingHeaders.indexOf("irrigation_needed_next_day"));
training.getRange(`${irrigCol}2:${irrigCol}${trainingLast}`).conditionalFormats.add("cellIs",{operator:"equal",formula:1,format:{fill:"#DBEAFE",font:{color:cyan,bold:true}}});

assumptions.showGridLines=false;
const assumptionHeaders=["region","crop","soil_type","field_capacity_vwc","wilting_point_vwc","threshold_pct","root_depth_mm","zone_area_m2","nominal_flow_lph","max_application_mm","kc_initial","kc_development","kc_mid","kc_late","kc_fallow","kc_dormancy"];
const assumptionRows=regions.map(r=>[r.region,r.crop,r.soil,r.fc,r.wp,r.threshold,r.rootDepth,r.area,r.flowLph,r.maxApplication,r.kc.initial,r.kc.development,r.kc.mid,r.kc.late,r.kc.fallow,r.kc.dormancy]);
assumptions.getRangeByIndexes(0,0,1,assumptionHeaders.length).values=[assumptionHeaders];
assumptions.getRangeByIndexes(1,0,assumptionRows.length,assumptionHeaders.length).values=assumptionRows;
assumptions.getRange(`A1:${excelCol(assumptionHeaders.length-1)}1`).format=headerFormat;
assumptions.tables.add(`A1:${excelCol(assumptionHeaders.length-1)}${assumptionRows.length+1}`,true,"AssumptionsTable").style="TableStyleMedium4";
assumptions.freezePanes.freezeRows(1); assumptions.getRange(`A1:P${assumptionRows.length+9}`).format.columnWidth=15;
assumptions.getRange("D2:E7").format.numberFormat="0.000"; assumptions.getRange("F2:P7").format.numberFormat="0.00";
assumptions.getRange("A9:P9").merge(); assumptions.getRange("A9").values=[["Simulation rules"]]; assumptions.getRange("A9:P9").format=sectionFormat;
assumptions.getRange("A10:P15").values=[
  ["Daily water balance","end VWC = prior VWC + (80% rain − ET₀×Kc + delivered irrigation) / root depth",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
  ["Decision horizon","irrigation_needed_next_day is evaluated at end of each day",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
  ["Target moisture","threshold + 20 percentage points, capped at 85% of available water",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
  ["Anomalies","deterministic modular schedule; no random functions",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
  ["Raw ADC","linear inverse mapping: dry 2500, wet 400",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
  ["Important","All agronomic parameters and every device/control/fault field are assumptions, not Tunisian field observations.",null,null,null,null,null,null,null,null,null,null,null,null,null,null],
];
assumptions.getRange("A10:P15").format.wrapText=true; assumptions.getRange("A10:A15").format.font={bold:true,color:dark}; assumptions.getRange("B10:P15").format.columnWidth=14;
for(let row=10;row<=15;row++) assumptions.getRange(`B${row}:P${row}`).merge();

dataDictionary.showGridLines=false;
dataDictionary.getRange("A1:E1").values=[["field","type","unit/domain","origin","definition"]];
dataDictionary.getRangeByIndexes(1,0,dictionary.length,5).values=dictionary;
dataDictionary.getRange(`A1:E${dictionary.length+1}`).format.font={name:"Aptos",size:10};
dataDictionary.getRange("A1:E1").format=headerFormat;
dataDictionary.tables.add(`A1:E${dictionary.length+1}`,true,"DataDictionaryTable").style="TableStyleMedium4";
dataDictionary.freezePanes.freezeRows(1); dataDictionary.getRange(`A1:A${dictionary.length+1}`).format.columnWidth=34; dataDictionary.getRange(`B1:D${dictionary.length+1}`).format.columnWidth=18; dataDictionary.getRange(`E1:E${dictionary.length+1}`).format.columnWidth=65; dataDictionary.getRange(`A2:E${dictionary.length+1}`).format.wrapText=true;

sources.showGridLines=false;
sources.getRange("A1:G1").values=[["region","requested_latitude","requested_longitude","grid_latitude","grid_longitude","elevation_m","exact_api_query"]];
sources.getRangeByIndexes(1,0,sourceRows.length,7).values=sourceRows;
sources.getRange("A1:G1").format=headerFormat; sources.tables.add(`A1:G${sourceRows.length+1}`,true,"SourcesTable").style="TableStyleMedium4";
sources.getRange(`A1:G${sourceRows.length+7}`).format.columnWidth=18; sources.getRange(`G1:G${sourceRows.length+7}`).format.columnWidth=100; sources.getRange(`G2:G${sourceRows.length+1}`).format.wrapText=true;
sources.getRange("A9:G9").merge(); sources.getRange("A9").values=[["Attribution and licence"]]; sources.getRange("A9:G9").format=sectionFormat;
sources.getRange("A10:B13").values=[
  ["API documentation",apiDocs],
  ["Licence",licenceUrl],
  ["Required attribution","Weather data by Open-Meteo.com; transformed and combined with deterministic simulation."],
  ["Commercial note","Use an appropriate commercial API plan/endpoint and retain CC BY 4.0 attribution when deploying a sold product."],
];
sources.getRange("A10:A13").format=sectionFormat; sources.getRange("B10:B13").format.columnWidth=90; sources.getRange("A10:B13").format.wrapText=true;

quality.showGridLines=false;
quality.getRange("A1:H2").merge(); quality.getRange("A1").values=[["Dataset Quality & Coverage"]]; quality.getRange("A1:H2").format=titleFormat;
quality.getRange("A4:B12").values=[
  ["Metric","Value"],["Total records",null],["Start date",null],["End date",null],["Regions",regions.length],["Irrigation decisions",null],["Anomaly records",null],["Missing weather rows",missingWeather],["Production-ready","NO"]
];
quality.getRange("A4:B4").format=headerFormat; quality.getRange("A5:A12").format=sectionFormat;
const tCol=name=>excelCol(trainingHeaders.indexOf(name));
quality.getRange("B5").formulas=[[`=COUNTA('Training Dataset'!$A$2:$A$${trainingLast})`]];
quality.getRange("B6").formulas=[[`=MIN('Training Dataset'!$C$2:$C$${trainingLast})`]];
quality.getRange("B7").formulas=[[`=MAX('Training Dataset'!$C$2:$C$${trainingLast})`]];
quality.getRange("B9").formulas=[[`=COUNTIF('Training Dataset'!$${tCol('irrigation_needed_next_day')}$2:$${tCol('irrigation_needed_next_day')}$${trainingLast},1)`]];
quality.getRange("B10").formulas=[[`=COUNTIF('Training Dataset'!$${tCol('anomaly_label')}$2:$${tCol('anomaly_label')}$${trainingLast},"<>normal")`]];
quality.getRange("B6:B7").format.numberFormat="yyyy-mm-dd"; quality.getRange("B12").format={fill:"#FEE2E2",font:{bold:true,color:red}};

quality.getRange("A15:H15").values=[["Region","Rows","Mean temp °C","Rain mm","Mean ET₀ mm","Mean moisture %","Irrigation days","Anomaly days"]]; quality.getRange("A15:H15").format=headerFormat;
quality.getRange("A16:A21").values=regions.map(r=>[r.region]);
for(let row=16;row<=21;row++){
  quality.getRange(`B${row}`).formulas=[[`=COUNTIF('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row})`]];
  quality.getRange(`C${row}`).formulas=[[`=AVERAGEIF('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('temperature_mean_c')}$2:$${tCol('temperature_mean_c')}$${trainingLast})`]];
  quality.getRange(`D${row}`).formulas=[[`=SUMIF('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('rain_mm')}$2:$${tCol('rain_mm')}$${trainingLast})`]];
  quality.getRange(`E${row}`).formulas=[[`=AVERAGEIF('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('et0_mm')}$2:$${tCol('et0_mm')}$${trainingLast})`]];
  quality.getRange(`F${row}`).formulas=[[`=AVERAGEIF('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('moisture_pct_sim')}$2:$${tCol('moisture_pct_sim')}$${trainingLast})`]];
  quality.getRange(`G${row}`).formulas=[[`=COUNTIFS('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('irrigation_needed_next_day')}$2:$${tCol('irrigation_needed_next_day')}$${trainingLast},1)`]];
  quality.getRange(`H${row}`).formulas=[[`=COUNTIFS('Training Dataset'!$${tCol('region')}$2:$${tCol('region')}$${trainingLast},A${row},'Training Dataset'!$${tCol('anomaly_label')}$2:$${tCol('anomaly_label')}$${trainingLast},"<>normal")`]];
}
quality.getRange("C16:F21").format.numberFormat="0.00"; quality.getRange("A4:H21").format.columnWidth=18; quality.freezePanes.freezeRows(2);
quality.getRange("A24:H27").values=[
  ["Quality gate","Status","Reason",null,null,null,null,null],
  ["Weather completeness",missingWeather===0?"PASS":"REVIEW",missingWeather===0?"No missing source rows detected":"Some source rows contain missing values",null,null,null,null,null],
  ["Provenance separation","PASS","Every record labels real reanalysis and synthetic device/control origins",null,null,null,null,null],
  ["Autonomous control","BLOCKED","Requires calibrated field data, verified labels, temporal holdout and shadow-mode acceptance",null,null,null,null,null],
];
quality.getRange("A24:C24").format=headerFormat; quality.getRange("B25:B26").format={fill:"#DCFCE7",font:{bold:true,color:green}}; quality.getRange("B27").format={fill:"#FEE2E2",font:{bold:true,color:red}}; quality.getRange("A24:C27").format.wrapText=true;
for(let row=24;row<=27;row++) quality.getRange(`C${row}:H${row}`).merge();
quality.getRange("A24:H24").format=headerFormat;
quality.getRange("A24:H27").format.wrapText=true;

const csvEscape=value=>{
  if(value instanceof Date) value=value.toISOString().slice(0,10);
  if(value==null) return "";
  const s=String(value);
  return /[",\n\r]/.test(s)?`"${s.replaceAll('"','""')}"`:s;
};
const csv=[trainingHeaders.join(","),...trainingRows.map(row=>row.map(csvEscape).join(","))].join("\r\n")+"\r\n";
await fs.writeFile(csvPath,csv,"utf8");

const inspect=await wb.inspect({kind:"table",range:"Quality Summary!A1:H27",include:"values,formulas",tableMaxRows:30,tableMaxCols:10,maxChars:12000});
console.log(inspect.ndjson);
const errors=await wb.inspect({kind:"match",searchTerm:"#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",options:{useRegex:true,maxResults:300},summary:"final formula error scan"});
console.log(errors.ndjson);

const previews=[
  ["README","A1:H23"],["Training Dataset","A1:L22"],["Raw Weather","A1:L22"],
  ["Quality Summary","A1:H27"],["Assumptions","A1:P15"],["Data Dictionary",`A1:E${dictionary.length+1}`],["Sources","A1:G13"]
];
for(const [sheetName,range] of previews){
  const blob=await wb.render({sheetName,range,scale:1.25,format:"png"});
  const safe=sheetName.toLowerCase().replaceAll(" ","_");
  await fs.writeFile(path.join(outputDir,`preview_${safe}.png`),new Uint8Array(await blob.arrayBuffer()));
}

const output=await SpreadsheetFile.exportXlsx(wb);
await output.save(workbookPath);
console.log(JSON.stringify({workbookPath,csvPath,rows:trainingRows.length,missingWeather,sheets:7}));
