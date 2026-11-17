// 运行时共用的纯几何函数，避免把文件系统或构建依赖打进网页。-codex，2026-10-16
export const projection={longitude:118.2,latitude:26.05,scale:100,cosLat:Math.cos(26.05*Math.PI/180)};
export const project=([longitude,latitude])=>[(longitude-projection.longitude)*projection.scale*projection.cosLat,(latitude-projection.latitude)*projection.scale];
export const polygonsOf=geometry=>geometry.type==='Polygon'?[geometry.coordinates]:geometry.coordinates;
