// 渲染行政区挤出网格，负责镜头、拾取和空间标签；业务面板由 main.js 管理。
// 只有行政边界与投影真实，点位为演示数据，不包含地形或卫星影像。-codex，2026-10-16
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { polygonsOf, project } from '../../scripts/geo.js';

export class MapStage {
  constructor(container, labels, data, geo, onCity, onSite, compassArrow) {
    this.container=container; this.data=data; this.selected=null; this.labels=[]; this.regions=[]; this.sites=[];
    this.onCity=onCity; this.onSite=onSite; this.tween=null; this.reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
    this.renderer=new THREE.WebGLRenderer({ antialias:true,alpha:true });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio,2));
    this.renderer.setClearColor(0x07141f,0);
    this.renderer.outputColorSpace=THREE.SRGBColorSpace;
    this.renderer.toneMapping=THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure=1.05;
    this.renderer.domElement.setAttribute('aria-label','福建三维行政区地图，可拖动旋转或点击城市');
    container.prepend(this.renderer.domElement);
    this.scene=new THREE.Scene();
    this.camera=new THREE.PerspectiveCamera(38,1,1,6000);
    this.compassArrow=compassArrow;this.northInView=new THREE.Vector3();this.inverseViewRotation=new THREE.Quaternion();
    this.controls=new OrbitControls(this.camera,this.renderer.domElement);
    this.controls.enableDamping=true; this.controls.dampingFactor=0.075;
    this.controls.enablePan=false; this.controls.minDistance=65; this.controls.maxDistance=2200;
    this.controls.minPolarAngle=0.08; this.controls.maxPolarAngle=Math.PI/2.15;
    this.controls.addEventListener('start',()=>{this.tween=null;});
    this.scene.add(new THREE.HemisphereLight(0xc4f7ff,0x042339,1.2));
    const light=new THREE.DirectionalLight(0xe0faff,2); light.position.set(-180,450,250); this.scene.add(light);
    const rim=new THREE.DirectionalLight(0x49d8bc,.8); rim.position.set(220,180,-240); this.scene.add(rim);
    const grid=new THREE.GridHelper(1800,60,0x153341,0x102532); grid.position.y=-8; grid.material.transparent=true; grid.material.opacity=.45;this.scene.add(grid);
    const floor=new THREE.Mesh(new THREE.PlaneGeometry(2300,2300),new THREE.MeshBasicMaterial({color:0x081923,transparent:true,opacity:.45}));floor.rotation.x=-Math.PI/2;floor.position.y=-9;this.scene.add(floor);
    for(const [index,city] of data.cities.entries()) {
      const geometry=new THREE.BufferGeometry();
      geometry.setAttribute('position',new THREE.Float32BufferAttribute(city.mesh.vertices,3));
      geometry.setAttribute('normal',new THREE.Float32BufferAttribute(city.mesh.normals,3));
      const colors=[];
      for(let i=2;i<city.mesh.normals.length;i+=3) {const n=city.mesh.normals[i];const value=n>.5?1:n<-.5?.25:.45;colors.push(value,value,value);}
      geometry.setAttribute('color',new THREE.Float32BufferAttribute(colors,3));
      geometry.rotateX(-Math.PI/2);geometry.computeBoundingSphere();
      const material=new THREE.MeshStandardMaterial({color:new THREE.Color().setHSL(.5+index*.003,.47,.3+index*.006),vertexColors:true,roughness:.55,metalness:.32});
      const mesh=new THREE.Mesh(geometry,material);mesh.userData={code:city.code,type:'city'};this.scene.add(mesh);
      const outlines=new THREE.Group();
      const feature=geo.features.find(f=>f.properties.adcode===city.code);
      for(const polygon of polygonsOf(feature.geometry)) for(const ring of polygon) {
        const points=ring.map(point=>{const [x,y]=project(point);return new THREE.Vector3(x,9.3,-y);});
        const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0x84ebd3,transparent:true,opacity:.65}));outlines.add(line);
      }
      this.scene.add(outlines);this.regions.push({city,mesh,outlines,base:material.color.clone()});
      const element=document.createElement('button');element.className='city-label';element.dataset.city=city.code;
      element.innerHTML=`<span>${city.name.replace('市','')}</span><b>${city.count}<small> 个</small></b>`;
      element.setAttribute('aria-label',`${city.name}，${city.count} 个演示点，点击下钻`);
      element.addEventListener('click',()=>onCity(city.code));labels.append(element);
      this.labels.push({element,position:new THREE.Vector3(city.center[0],16,-city.center[1]),code:city.code});
    }
    this.markerGroup=new THREE.Group();this.scene.add(this.markerGroup);
    this.raycaster=new THREE.Raycaster();this.pointer=new THREE.Vector2();
    this.renderer.domElement.addEventListener('pointerdown',event=>{this.down={x:event.clientX,y:event.clientY,time:performance.now()};});
    this.renderer.domElement.addEventListener('pointerup',event=>{
      if(!this.down||Math.hypot(event.clientX-this.down.x,event.clientY-this.down.y)>6||performance.now()-this.down.time>600)return;
      const hits=this.intersections(event);if(!hits.length)return;
      const hit=hits[0].object.userData;
      if(hit.type==='site')onSite(hit.id);else if(!this.selected)onCity(hit.code);
    });
    this.renderer.domElement.addEventListener('pointermove',event=>{const hits=this.intersections(event);this.renderer.domElement.style.cursor=hits.length?'pointer':'grab';});
    this.resizeObserver=new ResizeObserver(()=>this.resize());this.resizeObserver.observe(container);
    this.resize();this.focus(null,true);this.frameCount=0;this.fpsStart=performance.now();this.animate();
  }
  intersections(event){const rect=this.renderer.domElement.getBoundingClientRect();this.pointer.set((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1);this.raycaster.setFromCamera(this.pointer,this.camera);return this.raycaster.intersectObjects(this.selected?this.sites:this.regions.map(r=>r.mesh),false);}
  resize(){this.width=this.container.clientWidth;this.height=this.container.clientHeight;if(!this.width||!this.height)return;this.renderer.setSize(this.width,this.height);this.camera.aspect=this.width/this.height;this.camera.updateProjectionMatrix();if(this.home)this.focus(this.selected,true);}
  focus(code,immediate=false,top=false){
    // 镜头操作可以重建标记，但同一城市的选点身份必须一并恢复；切城才清空。
    // 统一放在 focus 内，避免 reset / top / resize 各自漏掉高亮状态。-codex，2026-10-16
    const siteId=code===this.selected?this.siteId:null;
    this.selected=code;const selected=this.regions.find(r=>r.city.code===code);
    for(const region of this.regions){const visible=!code||region.city.code===code;region.mesh.visible=visible;region.outlines.visible=visible;region.mesh.material.color.copy(code?new THREE.Color(0x32ac97):region.base);}
    for(const label of this.labels)label.element.hidden=Boolean(code);
    this.clearSites();if(selected)this.addSites(selected.city);
    this.selectSite(siteId);
    const box=new THREE.Box3();for(const region of this.regions)if(region.mesh.visible)box.expandByObject(region.mesh);
    const center=box.getCenter(new THREE.Vector3());const size=box.getSize(new THREE.Vector3());
    const distance=Math.max(size.z,size.x/this.camera.aspect)/(2*Math.tan(THREE.MathUtils.degToRad(this.camera.fov/2)))*1.12;
    const target=center.clone();const position=center.clone().add(new THREE.Vector3(0,top?distance:distance*.89,top?.01:distance*.65));
    this.home={target,position};
    if(immediate||this.reduced){this.camera.position.copy(position);this.controls.target.copy(target);this.controls.update();}
    else this.tween={time:performance.now(),from:this.camera.position.clone(),to:position,fromTarget:this.controls.target.clone(),target};
  }
  reset(top=false){this.focus(this.selected,false,top);}
  clearSites(){for(const child of [...this.markerGroup.children]){this.markerGroup.remove(child);child.geometry.dispose();child.material.dispose();}this.sites=[];}
  addSites(city){
    const markerSize=Math.max(1.05,Math.min(2.7,(city.bounds[1][0]-city.bounds[0][0])/75));
    for(const site of city.sites){
      const marker=new THREE.Mesh(new THREE.SphereGeometry(markerSize,14,10),new THREE.MeshBasicMaterial({color:0xb9ffe0}));marker.position.set(site.position[0],12.5,-site.position[1]);marker.userData={type:'site',id:site.id};this.markerGroup.add(marker);this.sites.push(marker);
      const halo=new THREE.Mesh(new THREE.RingGeometry(markerSize*1.5,markerSize*2,24),new THREE.MeshBasicMaterial({color:0x49dfb4,transparent:true,opacity:.55,side:THREE.DoubleSide}));halo.rotation.x=-Math.PI/2;halo.position.copy(marker.position);halo.position.y=9.6;this.markerGroup.add(halo);
    }
  }
  selectSite(id){this.siteId=id;for(const marker of this.sites){const selected=marker.userData.id===id;marker.material.color.set(selected?0xffcd7e:0xb9ffe0);marker.scale.setScalar(selected?1.7:1);}}
  updateCompass(){
    if(!this.compassArrow)return;
    // 网页世界北向为 -Z；转进相机坐标后，用屏幕平面方向驱动现有箭头。
    // 不直接使用相机方位角，避免倾斜视角下箭头与地图投影方向不一致。-codex，2026-10-16
    this.northInView.set(0,0,-1).applyQuaternion(this.inverseViewRotation.copy(this.camera.quaternion).invert());
    const angle=THREE.MathUtils.radToDeg(Math.atan2(this.northInView.x,this.northInView.y));
    this.compassArrow.style.transform=`rotate(${angle.toFixed(2)}deg)`;
  }
  animate=()=>{
    requestAnimationFrame(this.animate);
    if(this.tween){const t=Math.min(1,(performance.now()-this.tween.time)/850),ease=1-Math.pow(1-t,3);this.camera.position.lerpVectors(this.tween.from,this.tween.to,ease);this.controls.target.lerpVectors(this.tween.fromTarget,this.tween.target,ease);if(t===1)this.tween=null;}
    this.controls.update();
    this.updateCompass();
    for(const label of this.labels){if(label.element.hidden)continue;const p=label.position.clone().project(this.camera);label.element.style.left=`${(p.x*.5+.5)*this.width}px`;label.element.style.top=`${(-p.y*.5+.5)*this.height}px`;label.element.style.visibility=p.z>1?'hidden':'visible';}
    this.renderer.render(this.scene,this.camera);this.frameCount++;
    if(performance.now()-this.fpsStart>1500){document.querySelector('#fps').textContent=`${Math.round(this.frameCount*1000/(performance.now()-this.fpsStart))} FPS`;this.frameCount=0;this.fpsStart=performance.now();}
  }
}
