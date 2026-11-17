// 用同一标准无头浏览器会话补充真实输入证据，不拍摄替代截图。
// 闲置检查会实际等待约 61 秒；滚轮/触屏手感仍由真实设备体验。-codex，2026-10-16
async page => {
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.setViewportSize({width:1920,height:959});
  await page.goto('http://127.0.0.1:5178/');
  await page.waitForSelector('html[data-ready="true"]');await page.waitForTimeout(1000);
  const results=[];
  // 此坐标来自同视口正式全省截图中的南平顶部表面，避开 HTML 标签。
  await page.mouse.click(770,385);await page.waitForSelector('html[data-city="350700"]');
  results.push('南平三维表面射线拾取');await page.locator('#back').click();await page.waitForTimeout(1000);
  const label=page.locator('.city-label[data-city="350100"]');const before=await label.boundingBox();
  await page.mouse.move(400,400);await page.mouse.down();await page.mouse.move(530,440,{steps:15});await page.mouse.up();await page.waitForTimeout(500);
  const after=await label.boundingBox();if(Math.hypot(before.x-after.x,before.y-after.y)<5)throw new Error('拖动未使空间标签位置变化');
  if((await page.locator('html').getAttribute('data-view'))!=='province')throw new Error('旋转误触发了下钻');
  results.push('真实拖动旋转，未误触下钻');await page.locator('#reset-view').click();await page.waitForTimeout(1000);
  await page.locator('#item-list [data-code="350100"]').click();await page.waitForTimeout(1000);
  // 来自正式福州截图的可见球形点位，不经过列表按钮。
  await page.mouse.click(690,366);await page.waitForSelector('#site-detail:not([hidden])');
  const detail=await page.locator('#site-detail').textContent();if(!detail.includes('350100-'))throw new Error('拾取点不属于当前城市');
  results.push('三维点位射线拾取及归属');await page.keyboard.press('Escape');
  if(await page.locator('#site-detail').isVisible())throw new Error('Escape 未关闭点位详情');
  await page.keyboard.press('Escape');await page.waitForSelector('html[data-view="province"]');results.push('Escape 两层返回');
  await page.locator('#top-view').click();await page.waitForTimeout(1000);await page.locator('#reset-view').click();
  await page.locator('#fullscreen').click();await page.waitForTimeout(300);
  const fullscreen=await page.evaluate(()=>Boolean(document.fullscreenElement));
  if(fullscreen)await page.locator('#fullscreen').click();
  else if(!(await page.locator('#render-status').textContent()).includes('未允许全屏'))throw new Error('全屏未允许时缺少真实提示');
  results.push(fullscreen?'全屏进入/退出':'浏览器未允许全屏，页面保留可用提示');
  await page.locator('#item-list [data-code="350200"]').click();
  const started=Date.now();await page.waitForSelector('html[data-view="province"]',{timeout:65000});
  const idleMs=Date.now()-started;if(idleMs<59000)throw new Error('闲置返回提前触发');
  results.push(`真实闲置返回 ${idleMs}ms`);
  if(errors.length)throw new Error(errors.join(';'));
  return {url:page.url(),viewport:'1920x959@100%',results,pageErrors:errors,endingView:'province',wheelFeel:'未验证',touchFeel:'未验证'};
}
