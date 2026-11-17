// 技术样机的省市/点位状态与可访问按钮入口；两端演示统计均来自 shared 文件。
// 没有真实业务接口，不能把此处的点位名称与面积用于业务结论。-codex，2026-10-16
import './style.css';
import data from '../../shared/map-data.json';
import geo from '../../shared/fujian.geojson?raw';
import { MapStage } from './stage.js';

const $=selector=>document.querySelector(selector);
let stage,selectedCity=null,selectedSite=null,lastActivity=Date.now();
const countLabel=value=>String(value).padStart(2,'0');
function chooseCity(code){
  const city=data.cities.find(c=>c.code===Number(code));if(!city)return;
  selectedCity=city;selectedSite=null;lastActivity=Date.now();stage.focus(city.code);render();
}
function home(){if(!stage)return;selectedCity=null;selectedSite=null;lastActivity=Date.now();stage.focus(null);render();}
function chooseSite(id){
  if(!selectedCity)return;const site=selectedCity.sites.find(s=>s.id===id);if(!site)return;
  selectedSite=site;stage.selectSite(id);render();
}
function render(){
  const city=selectedCity;
  $('#map-title').textContent=city?`${city.name.replace('市','')}，近一点看。`:'一张地图，走进福建。';
  $('#map-subtitle').textContent=city?'点击发光点或右侧条目，查看模拟点位详情':'点击城市，探索市级演示点位分布';
  $('#city-crumb').textContent=city?` / ${city.name}`:'';
  $('#view-label').textContent=city?'市级视图':'全省视图';$('#back').hidden=!city;
  $('#panel-title').textContent=city?`${city.name}点位分布`:'全省分布概览';$('#total-count').textContent=countLabel(city?city.count:data.total);
  $('#summary-text').textContent=city?'本市模拟点位，可逐个查看':'覆盖 9 个城市';$('#region-code').textContent=city?`${city.code} / ${city.english.toUpperCase()}`:'35 / FUJIAN';
  $('#list-title').textContent=city?'演示点位':'城市分布';$('#list-hint').textContent=city?'点击查看详情 ↘':'点击下钻 ↘';
  const list=$('#item-list');list.replaceChildren();
  for(const [index,item] of (city?city.sites:data.cities).entries()){
    const button=document.createElement('button');button.className=`list-row ${city?'site-row':''} ${selectedSite?.id===item.id&&city?'is-selected':''}`;
    if(city){button.dataset.site=item.id;button.innerHTML=`<span class="row-number">${String(index+1).padStart(2,'0')}</span><span class="row-name">${item.name.replace(city.name,'')}</span><span class="row-value">查看 <small>↗</small></span>`;button.addEventListener('click',()=>chooseSite(item.id));}
    else{button.dataset.code=item.code;button.innerHTML=`<span class="row-number">${String(index+1).padStart(2,'0')}</span><span class="row-name">${item.name}<i style="width:${item.count/24*100}%"></i></span><span class="row-value">${countLabel(item.count)} <small>↗</small></span>`;button.addEventListener('click',()=>chooseCity(item.code));}
    list.append(button);
  }
  const detail=$('#site-detail');detail.hidden=!selectedSite;
  if(selectedSite){const site=selectedSite;detail.innerHTML=`<button id="close-detail" aria-label="关闭点位详情">×</button><span class="eyebrow">DEMO SITE / ${site.id}</span><h3>${site.name}</h3><dl><div><dt>演示类型</dt><dd>${site.kind}</dd></div><div><dt>模拟面积</dt><dd>${site.areaMu} 亩</dd></div><div><dt>模拟经度</dt><dd>${site.coordinate[0].toFixed(4)}</dd></div><div><dt>模拟纬度</dt><dd>${site.coordinate[1].toFixed(4)}</dd></div></dl><p>此点由程序生成，非真实鱼塘。</p>`;$('#close-detail').addEventListener('click',()=>{selectedSite=null;stage.selectSite(null);render();});}
  // 供无头验收读出真实 UI 所用状态；不提供改变业务状态的隐藏控制接口。-codex，2026-10-16
  document.documentElement.dataset.view=city?'city':'province';document.documentElement.dataset.city=city?String(city.code):'';
}
try{
  stage=new MapStage($('#map-stage'),$('#map-labels'),data,JSON.parse(geo),chooseCity,chooseSite,$('.compass span'));
  render();$('#loading').hidden=true;$('#render-status').textContent='WebGL · 三维地图已就绪';document.documentElement.dataset.ready='true';
}catch(error){console.error(error);$('#loading').hidden=true;$('#map-error').hidden=false;$('#map-error').textContent='三维渲染未能启动。请使用启用硬件加速的现代浏览器，检查 WebGL 是否可用后刷新。';$('#render-status').textContent='三维渲染不可用';document.documentElement.dataset.ready='error';}
$('#back').addEventListener('click',home);$('#home-crumb').addEventListener('click',home);
$('#reset-view').addEventListener('click',()=>stage?.reset());$('#top-view').addEventListener('click',()=>stage?.reset(true));
$('#fullscreen').addEventListener('click',async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await document.documentElement.requestFullscreen();}catch{$('#render-status').textContent='当前浏览器未允许全屏，可继续使用窗口视图';}});
const dialog=$('#engine-dialog');$('#engine-info').addEventListener('click',()=>dialog.showModal());
for(const button of document.querySelectorAll('.dialog-close,.dialog-close-button'))button.addEventListener('click',()=>dialog.close());
window.addEventListener('keydown',event=>{if(event.key==='Escape'&&!dialog.open){if(selectedSite){selectedSite=null;stage?.selectSite(null);render();}else if(selectedCity)home();}});
for(const eventName of ['pointerdown','pointermove','keydown','wheel','touchstart'])window.addEventListener(eventName,()=>{lastActivity=Date.now();},{passive:true});
setInterval(()=>{const remaining=Math.max(0,60-Math.floor((Date.now()-lastActivity)/1000));$('#idle-note').textContent=selectedCity?`无人操作 ${remaining} 秒返回全省`:'无人操作 60 秒返回全省';if(selectedCity&&remaining===0&&!dialog.open)home();},1000);
