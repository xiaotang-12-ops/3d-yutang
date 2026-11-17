// 仅由前端专项标准 wrapper 调用；通过真实按钮走完九市再返回全省。
// 不是临时测试框架，也不能证明 UE5 已运行。-codex，2026-10-16
async page => {
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.waitForSelector('html[data-ready="true"]');
  const expected=[['350100',24],['350200',8],['350300',12],['350400',18],['350500',22],['350600',20],['350700',16],['350800',14],['350900',18]];
  const results=[];
  for(const [code,count] of expected){
    await page.locator(`#item-list [data-code="${code}"]`).click();
    await page.waitForSelector(`html[data-city="${code}"]`);
    const countText=await page.locator('#total-count').textContent();
    const items=await page.locator('#item-list [data-site]').count();
    if(Number(countText)!==count||items!==count)throw new Error(`城市 ${code} 统计或点数不匹配`);
    const first=page.locator('#item-list [data-site]').first();const id=await first.getAttribute('data-site');await first.click();
    await page.waitForSelector('#site-detail:not([hidden])');
    if(!(await page.locator('#site-detail').textContent()).includes(id))throw new Error('点位详情身份不匹配');
    await page.locator('#close-detail').click();await page.locator('#back').click();
    await page.waitForSelector('html[data-view="province"]');results.push({code,count,site:id,roundTrip:true});
  }
  // 标签按钮是地图上实际的可访问入口，额外检验它不只是一张静态标签图。-codex，2026-10-16
  await page.locator('.city-label[data-city="350100"]').click();await page.waitForSelector('html[data-city="350100"]');await page.locator('#back').click();
  await page.locator('#engine-info').click();await page.waitForSelector('#engine-dialog[open]');
  if(!(await page.locator('#engine-dialog').textContent()).includes('未验证'))throw new Error('UE 限制提示缺失');
  await page.locator('.dialog-close-button').click();
  await page.waitForTimeout(1000);
  const network=await page.evaluate(()=>performance.getEntriesByType('resource').map(entry=>entry.name).filter(url=>/^https?:/.test(url)&&new URL(url).origin!==location.origin));
  if(network.length)throw new Error(`运行时出现外部请求：${network.join(',')}`);
  if(errors.length)throw new Error(errors.join(';'));
  console.log(JSON.stringify({url:page.url(),cities:results,externalRequests:network,pageErrors:errors,endingView:'province'}));
}
