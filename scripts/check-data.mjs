// 核对两端数据一致、统计口径、有限网格与点位归属，不能代替 UE 编译运行。-codex，2026-10-16
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { insidePolygon, polygonsOf, project } from './prepare-data.mjs';
const dataText = await fs.readFile(new URL('../shared/map-data.json', import.meta.url),'utf8');
const data = JSON.parse(dataText);
const geo = JSON.parse(await fs.readFile(new URL('../shared/fujian.geojson', import.meta.url),'utf8'));
const nativeText = await fs.readFile(new URL('../unreal/FujianSandbox/Content/Data/map-data.json',import.meta.url),'utf8');
assert.equal(dataText,nativeText,'网页与 UE 数据副本必须相同');
assert.equal(data.demo,true);
assert.equal(data.cities.length,9);
assert.equal(new Set(data.cities.map(c=>c.code)).size,9);
assert.equal(data.total,data.cities.reduce((sum,c)=>sum+c.sites.length,0));
for(const city of data.cities) {
  assert.equal(city.count,city.sites.length);
  const feature = geo.features.find(f=>f.properties.adcode===city.code);
  const polygons = polygonsOf(feature.geometry);
  for(const site of city.sites) {
    assert.equal(site.demo,true);
    assert.ok(polygons.some(p=>insidePolygon(site.coordinate,p)),`${site.id} 必须在对应市边界内`);
    assert.ok(project(site.coordinate).every((n,i)=>Math.abs(n-site.position[i])<0.001));
  }
  assert.equal(city.mesh.vertices.length,city.mesh.normals.length);
  assert.equal(city.mesh.vertices.length%9,0);
  assert.ok(city.mesh.vertices.length>0);
  assert.ok(city.mesh.vertices.every(Number.isFinite));
  assert.ok(city.mesh.triangles.every(i=>Number.isInteger(i)&&i>=0&&i<city.mesh.vertices.length/3));
  const vertices=city.mesh.vertices;
  for(let i=0;i<vertices.length;i+=9){
    const a=[vertices[i+3]-vertices[i],vertices[i+4]-vertices[i+1],vertices[i+5]-vertices[i+2]];
    const b=[vertices[i+6]-vertices[i],vertices[i+7]-vertices[i+1],vertices[i+8]-vertices[i+2]];
    assert.ok(Math.hypot(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])>1e-9,`${city.code} 网格不能有退化三角形`);
  }
}
const polygons=geo.features.flatMap(f=>polygonsOf(f.geometry));
// 调研点在 A 屏按所属城市定位。检查坐标归属，避免契约通过但实际镜头落到其他城市。-codex，2026-11-17
const survey = JSON.parse(await fs.readFile(new URL('../shared/xinluo-survey.json', import.meta.url), 'utf8'));
for (const site of survey.subjects) {
  if (site.longitude === null && site.latitude === null) continue;
  const region = geo.features.find(feature => feature.properties.adcode === site.cityCode);
  assert.ok(region && polygonsOf(region.geometry).some(p => insidePolygon([site.longitude,site.latitude],p)),
    site.id + ' 的演示坐标必须位于所属区域');
}
console.log(`PASS：九市、${data.total} 个模拟点的归属/统计/投影、${polygons.length} 个多边形（含岛屿）和非退化网格及两端文件一致。当前数据孔洞数 ${polygons.reduce((sum,p)=>sum+p.length-1,0)}；UE 引擎编译运行未验证。`);
