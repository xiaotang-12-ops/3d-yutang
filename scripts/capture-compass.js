// 正式截图入口的状态准备：用真实拖动检查地图和北向提示是否同步。
// 验证旋转、重置、俯视与城市详情往返，最后保留旋转后的全省画面。-codex，2026-10-16
async page => {
  // 正式 wrapper 在准备回调后还会设置视口；提前一致化，避免 resize 自动重置镜头。
  await page.setViewportSize({width:1920,height:959});
  await page.waitForSelector('html[data-ready="true"]');
  await page.locator('#reset-view').click();
  await page.waitForTimeout(1000);
  const label=page.locator('.city-label[data-city="350100"]');
  const before=await label.boundingBox();
  const arrowBefore=await page.locator('.compass span').evaluate(element=>getComputedStyle(element).transform);
  await page.mouse.move(400,420);await page.mouse.down();
  await page.mouse.move(650,420,{steps:20});await page.mouse.up();
  await page.waitForTimeout(750);
  const after=await label.boundingBox();
  const arrowAfter=await page.locator('.compass span').evaluate(element=>getComputedStyle(element).transform);
  if(Math.hypot(after.x-before.x,after.y-before.y)<10)throw new Error('地图未实际旋转');
  if(await page.locator('html').getAttribute('data-view')!=='province')throw new Error('拖动误触下钻');
  if(arrowBefore===arrowAfter)throw new Error('地图已旋转但北向箭头未同步');
  const angleOf=()=>page.locator('.compass span').evaluate(element=>{
    const matrix=new DOMMatrixReadOnly(getComputedStyle(element).transform);return Math.atan2(matrix.b,matrix.a)*180/Math.PI;
  });
  const rotatedAngle=await angleOf();
  if(Math.abs(rotatedAngle)<20)throw new Error('本次大角度拖动未产生明显北向变化');
  await page.locator('#reset-view').click();await page.waitForTimeout(1000);
  if(Math.abs(await angleOf())>1)throw new Error('重置后未恢复北向朝上');
  await page.locator('#top-view').click();await page.waitForTimeout(1000);
  if(Math.abs(await angleOf())>1)throw new Error('俯视后北向不正确');
  await page.locator('#item-list [data-code="350100"]').click();
  await page.locator('#item-list [data-site="350100-01"]').click();
  if(!(await page.locator('#site-detail').textContent()).includes('350100-01'))throw new Error('城市详情回归失败');
  await page.locator('#back').click();await page.waitForTimeout(1000);
  await page.mouse.move(400,420);await page.mouse.down();await page.mouse.move(650,420,{steps:20});await page.mouse.up();
  await page.waitForTimeout(750);
  return {state:'rotated province',rotatedAngle,finalAngle:await angleOf(),checks:['真实拖动同步','重置北向','俯视北向','福州点位详情及返回']};
}
