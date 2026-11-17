// 为市级详情截图复现真实点击；截图本身由标准 wrapper 完成。-codex，2026-10-16
async page => {
  await page.waitForSelector('html[data-ready="true"]');
  await page.locator('#item-list [data-code="350100"]').click();
  await page.waitForSelector('html[data-city="350100"]');
  await page.waitForTimeout(1000);
  await page.locator('#item-list [data-site="350100-01"]').click();
  await page.waitForSelector('#site-detail:not([hidden])');
  if(!(await page.locator('#site-detail').textContent()).includes('350100-01'))throw new Error('未进入目标点位详情');
  await page.locator('#top-view').click();await page.waitForTimeout(1000);
  await page.locator('#reset-view').click();await page.waitForTimeout(1000);
  if(!(await page.locator('#site-detail').textContent()).includes('350100-01'))throw new Error('镜头操作后点位身份丢失');
}
