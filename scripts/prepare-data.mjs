// 获取福建行政边界，生成两端共用的演示点与原生三角网格。
// 演示点不是真实鱼塘，固定种子仅用于可重复地验证交互。-codex，2026-10-16
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Shape, Path, ExtrudeGeometry } from 'three';
import { projection, project, polygonsOf } from './geo.js';
export { projection, project, polygonsOf } from './geo.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const dataDir = path.join(root, 'shared');
const source = 'shared/fujian.geojson';
export function insideRing(point, ring) {
  let inside = false;
  for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
    const [xi, yi] = ring[i], [xj, yj] = ring[j];
    if ((yi > point[1]) !== (yj > point[1]) && point[0] < (xj - xi) * (point[1] - yi) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}
export function insidePolygon(point, polygon) {
  return insideRing(point, polygon[0]) && !polygon.slice(1).some(ring => insideRing(point, ring));
}
const cityEnglish = {350100:'Fuzhou',350200:'Xiamen',350300:'Putian',350400:'Sanming',350500:'Quanzhou',350600:'Zhangzhou',350700:'Nanping',350800:'Longyan',350900:'Ningde'};
const counts = {350100:24,350200:8,350300:12,350400:18,350500:22,350600:20,350700:16,350800:14,350900:18};

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  await fs.mkdir(dataDir, { recursive: true });
  const raw = await fs.readFile(path.join(dataDir, 'fujian.geojson'), 'utf8');
  const geo = JSON.parse(raw);
  if (geo.type !== 'FeatureCollection' || geo.features.length !== 9) throw new Error('预期福建九市 FeatureCollection；禁止继续使用不匹配数据。');
  await fs.writeFile(path.join(dataDir, 'fujian.geojson'), raw);
  let seed = 20261016;
  const random = () => { seed = (1664525 * seed + 1013904223) >>> 0; return seed / 4294967296; };
  const cities = geo.features.map(feature => {
    const code = feature.properties.adcode;
    const polygons = polygonsOf(feature.geometry);
    const allPoints = polygons.flat(2);
    const xs = allPoints.map(p => p[0]), ys = allPoints.map(p => p[1]);
    const bounds = [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)];
    const sites = [];
    for (let attempts = 0; sites.length < counts[code] && attempts < 50000; attempts++) {
      const coordinate = [bounds[0] + random() * (bounds[2] - bounds[0]), bounds[1] + random() * (bounds[3] - bounds[1])];
      if (!polygons.some(polygon => insidePolygon(coordinate, polygon))) continue;
      sites.push({ id: `${code}-${String(sites.length+1).padStart(2,'0')}`, name: `${feature.properties.name}演示塘 ${String(sites.length+1).padStart(2,'0')}`, coordinate: coordinate.map(v=>+v.toFixed(6)), position: project(coordinate).map(v=>+v.toFixed(3)), areaMu: Math.round(10 + random()*60), kind: ['示范养殖塘','生态养殖塘','标准养殖塘'][sites.length%3], demo: true });
    }
    if (sites.length !== counts[code]) throw new Error(`无法为 ${code} 生成足量区内点位`);
    const vertices = [], normals = [], triangles = [];
    for (const polygon of polygons) {
      if (polygon[0].length < 4) continue;
      const ring = polygon[0].map(project);
      const shape = new Shape(ring.map(([x,y]) => ({x,y})));
      for(const hole of polygon.slice(1)) {
        const holePath = new Path(hole.map(project).map(([x,y]) => ({x,y})));
        shape.holes.push(holePath);
      }
      const geometry = new ExtrudeGeometry(shape, { depth: 9, bevelEnabled: false, steps: 1, curveSegments: 1 });
      const points = geometry.attributes.position;
      const normal = geometry.attributes.normal;
      const offset = vertices.length / 3;
      for (let i=0;i<points.count;i++) {
        vertices.push(+points.getX(i).toFixed(4), +points.getY(i).toFixed(4), +points.getZ(i).toFixed(4));
        normals.push(normal.getX(i),normal.getY(i),normal.getZ(i));
        // 保留共享原始绕序；UE 交换 XY 后正面朝向已匹配，不能再反转索引。-codex，2026-10-16
        triangles.push(offset+i);
      }
      geometry.dispose();
    }
    const center = project(feature.properties.centroid ?? feature.properties.center);
    return { code, name: feature.properties.name, english: cityEnglish[code], center, bounds: [project([bounds[0],bounds[1]]), project([bounds[2],bounds[3]])], count: sites.length, sites, mesh: {vertices,normals,triangles} };
  }).sort((a,b)=>a.code-b.code);
  const dataset = { schemaVersion:1, demo:true, title:'福建省三维鱼塘分布 · 技术演示', source, generatedAt:new Date().toISOString(), projection, total:cities.reduce((sum,city)=>sum+city.count,0), note:'默认边界为虚构示意方块；点位、数量、面积均为固定种子模拟，禁止用于业务或测绘。', cities };
  await fs.writeFile(path.join(dataDir,'map-data.json'), JSON.stringify(dataset));
  for (const project of ['unreal/FujianSandbox','unreal/FujianSandbox58','experiments/fujian-mountain-art-ab','experiments/fujian-dashboard-motion']) {
    const nativeDir = path.join(root,project,'Content','Data');
    await fs.mkdir(nativeDir,{recursive:true});
    await fs.copyFile(path.join(dataDir,'map-data.json'),path.join(nativeDir,'map-data.json'));
  }
  console.log(JSON.stringify({ cities:cities.length,total:dataset.total,triangles:cities.reduce((s,c)=>s+c.mesh.triangles.length/3,0),source }));
}
