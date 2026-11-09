// Browser integration tests against a simulated HTTP API; no physical robot is driven.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root=path.resolve(__dirname,'..');
(async()=>{
  const browser=await chromium.launch({headless:true,...(process.env.BROWSER_CHANNEL?{channel:process.env.BROWSER_CHANNEL}:{})});
  try{
    const page=await browser.newPage({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    const jpeg=Buffer.from(await page.evaluate(()=>{
      const canvas=document.createElement('canvas');canvas.width=320;canvas.height=240;
      const ctx=canvas.getContext('2d');ctx.fillStyle='#bcbcbc';ctx.fillRect(0,0,320,240);
      ctx.fillStyle='#777';ctx.fillRect(0,140,320,100);ctx.strokeStyle='#999';
      for(let x=-320;x<640;x+=80){ctx.beginPath();ctx.moveTo(160,140);ctx.lineTo(x,240);ctx.stroke();}
      ctx.fillStyle='white';ctx.fillRect(122,65,76,76);ctx.fillStyle='black';ctx.fillRect(130,73,60,60);
      ctx.fillStyle='white';for(let y=0;y<6;y++)for(let x=0;x<6;x++)if((x*7+y*3)%5<2)ctx.fillRect(137+x*7,80+y*7,7,7);
      ctx.fillStyle='white';ctx.font='12px sans-serif';ctx.fillText('Simulated camera',10,225);
      return canvas.toDataURL('image/jpeg').split(',')[1];
    }),'base64');
    let cameraOffline=false,cameraStale=false,cameraSlow=false,cameraRequests=0,calibrated=true,pendingCalibration=false;
    await page.route('http://192.168.4.250/**',async route=>{
      if(cameraOffline)return route.abort();
      const headers={'Access-Control-Allow-Origin':'http://robot.test','Access-Control-Expose-Headers':'X-Frame-Age-Ms'};
      if(new URL(route.request().url()).pathname==='/calibration')return route.fulfill({headers,contentType:'application/json',body:JSON.stringify({calibrated,restart_required:pendingCalibration})});
      cameraRequests++;
      if(cameraSlow)await new Promise(resolve=>setTimeout(resolve,1000));
      await route.fulfill({headers:{...headers,'X-Frame-Age-Ms':cameraStale?'5000':'80'},contentType:'image/jpeg',body:jpeg}).catch(()=>{});
    });
    let token=0,mode='IDLE',target=0,offline=false,reject=false,driving=false,maintenance=false,updateBusy=false,link=true,arming=false;const requests=[];
    const start=Date.now();
    await page.route('http://robot.test/**',async route=>{
      const req=route.request(),url=new URL(req.url());
      if(offline)return route.abort();
      if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(root,'web/index.html'),'utf8')});
      let body={},code=200;
      if(url.pathname==='/api/state')body={now:Date.now()-start,state:arming?'ARMING':mode==='AUTO'?'TRACK':mode==='MANUAL'?'MANUAL':'IDLE',reason:'Ready for your next destination',mode,target,owned:!!token,enabled:!!token,canConnect:!maintenance&&link,maintenance,cameraMaintenance:maintenance,updateBusy,link,visible:5,range:mode==='AUTO'?.82:null,ir:[0,0]};
      else if(url.pathname==='/api/claim'){if(maintenance||!link){code=409;body={error:'Unavailable'};}else{token=123456;body={token};}}
      else if(url.pathname==='/api/maintenance'){
        if(updateBusy){code=409;body={error:'Update in progress'};}
        else{maintenance=url.searchParams.get('enabled')==='1';token=0;mode='IDLE';driving=false;body={ok:true};}
      }
      else if(url.pathname==='/api/stop'){token=0;mode='IDLE';driving=false;body={ok:true};requests.push({action:'stop'});}
      else if(url.pathname==='/api/control'){
        const action=url.searchParams.get('action');requests.push(Object.fromEntries(url.searchParams));
        if(reject||!token){code=409;body={error:'Expired command'};}
        else{if(action==='auto'){mode='AUTO';target=Number(url.searchParams.get('tag'));}
          if(action==='manual'){mode='MANUAL';driving=false;}
          if(action==='idle'){mode='IDLE';driving=false;}
          if(action==='drive')driving=url.searchParams.get('v')!=='0'||url.searchParams.get('w')!=='0';
          body={ok:true};}
      }else{code=404;body={error:'Not found'};}
      await route.fulfill({status:code,contentType:'application/json',body:JSON.stringify(body)});
    });
    await page.goto('http://robot.test/');
    await page.waitForFunction(()=>!document.getElementById('connect').disabled);
    await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
    assert((await page.locator('#lensStatus').textContent()).includes('Saved lens calibration'));
    cameraStale=true;
    await page.waitForFunction(()=>document.getElementById('cameraImage').hidden);
    assert((await page.locator('#cameraStatus').textContent()).includes('No live picture'));
    cameraStale=false;
    await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
    await page.locator('#cameraToggle').click();
    assert(await page.locator('#cameraImage').isHidden());
    const pausedCount=cameraRequests;await page.waitForTimeout(700);assert.equal(cameraRequests,pausedCount);
    await page.locator('#cameraToggle').click();
    await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
    pendingCalibration=true;await page.evaluate(()=>lensStatus());
    assert((await page.locator('#lensStatus').textContent()).includes('restart camera'));
    pendingCalibration=false;calibrated=false;await page.evaluate(()=>lensStatus());
    assert((await page.locator('#lensStatus').textContent()).includes('starting estimates'));
    calibrated=true;await page.evaluate(()=>lensStatus());
    assert.equal(await page.locator('.destination').count(),4);
    assert(!(await page.locator('body').textContent()).includes('RUN switch'));
    assert.equal(await page.locator('#armed').textContent(),'Disabled');
    await page.locator('#connect').click();
    arming=true;
    await page.waitForFunction(()=>document.getElementById('armed').textContent==='Starting…');
    assert(await page.locator('[data-tag="3"]').isDisabled());
    arming=false;
    await page.waitForFunction(()=>!document.querySelector('[data-tag="3"]').disabled);
    await page.locator('[data-tag="3"]').click();
    await page.waitForFunction(()=>document.getElementById('target').textContent==='Tag 3');
    assert(requests.some(r=>r.action==='auto'&&r.tag==='3'));
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    fs.mkdirSync(path.join(root,'.pio','ui-tests'),{recursive:true});
    await page.screenshot({path:path.join(root,'.pio/ui-tests/destinations-mobile.png'),fullPage:true});
    await page.locator('#manualTab').click();
    await page.waitForFunction(()=>!document.querySelector('.forward').disabled);
    assert((await page.locator('#message').textContent()).includes('Manual mode'));
    const cameraBox=await page.locator('.camera-frame').boundingBox();
    const reverseBox=await page.locator('.reverse').boundingBox();
    const stopBox=await page.locator('#stop').boundingBox();
    assert(cameraBox.y>=0 && reverseBox.y+reverseBox.height<stopBox.y);
    cameraSlow=true;
    await page.locator('.forward').scrollIntoViewIfNeeded();
    const forward=await page.locator('.forward').boundingBox();
    await page.mouse.move(forward.x+forward.width/2,forward.y+forward.height/2);await page.mouse.down();
    await page.waitForTimeout(250);assert(driving);
    await page.mouse.up();await page.waitForTimeout(150);assert(!driving);
    assert(requests.filter(r=>r.action==='drive'&&r.v==='100').length>=2);
    assert(requests.some(r=>r.action==='drive'&&r.v==='0'&&r.w==='0'));
    cameraSlow=false;cameraOffline=true;
    await page.waitForFunction(()=>document.getElementById('cameraImage').hidden);
    assert.equal(await page.locator('.forward').isDisabled(),false);
    assert(token);
    cameraOffline=false;
    await page.waitForFunction(()=>!document.getElementById('cameraImage').hidden);
    await page.screenshot({path:path.join(root,'.pio/ui-tests/manual-mobile.png'),fullPage:false});
    await page.locator('#maintenance').click();
    await page.waitForFunction(()=>document.getElementById('maintenance').textContent==='Finish maintenance');
    assert(maintenance&&!token&&!driving);
    assert(await page.locator('#connect').isDisabled());
    assert(await page.locator('.forward').isDisabled());
    updateBusy=true;await page.waitForTimeout(250);
    assert(await page.locator('#maintenance').isDisabled());
    updateBusy=false;await page.waitForTimeout(250);
    await page.locator('#maintenance').click();
    await page.waitForFunction(()=>!document.getElementById('connect').disabled);
    assert(!token&&!maintenance);
    link=false;await page.waitForTimeout(250);assert(await page.locator('#connect').isDisabled());
    link=true;await page.waitForTimeout(250);
    await page.locator('#connect').click();await page.waitForTimeout(200);
    await page.locator('#stop').click();await page.waitForTimeout(100);assert(!token&&!driving);
    await page.locator('#connect').click();await page.waitForTimeout(200);
    await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.waitForTimeout(100);assert(!token);
    await page.locator('#connect').click();await page.waitForTimeout(200);reject=true;
    await page.waitForTimeout(350);assert(!token);reject=false;
    await page.locator('#connect').click();await page.waitForTimeout(200);offline=true;
    await page.waitForTimeout(650);assert.equal(await page.locator('.forward').isDisabled(),true);
    assert.deepEqual(errors,[]);
    console.log('PASS: app arming, maintenance lockout, camera view, manual drive, Stop and connection loss');
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
