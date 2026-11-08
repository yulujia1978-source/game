// Execute the real page script: verify runtime updates reload stale HTML.
const fs=require('fs');const vm=require('vm');const assert=require('assert/strict');
const version=fs.readFileSync('../payload/version.h','utf8').match(/TURBO_VERSION "([^"]+)"/)[1];
const script=fs.readFileSync('../payload/web/portal.html','utf8').split('<script>')[1].split('</script>')[0].replaceAll('{{TURBO_VERSION}}',version);
async function fixture() {
  const ids={};for(const id of ['status','enabled','controls','msg','form','clear','save','speed'])ids['#'+id]={disabled:id==='controls',checked:false,value:id==='speed'?'25000':'',textContent:''};
  const keys=[16384,8192,32768,4096,1024,2048,256,512].map(value=>({value:String(value),checked:false}));
  const shortcuts=[1,2].map(value=>({value:String(value),checked:false}));
  const f={state:{version,token:'original',enabled:1,mask:0,half_us:25000,shortcuts:1,attached:false,title:'',process:''},reloads:0,fail:false,ids};
  const document={querySelector:s=>{assert(ids[s],s);return ids[s]},querySelectorAll:s=>s==='.key input'?keys:shortcuts};
  vm.runInNewContext(script,{document,URLSearchParams,location:{reload:()=>f.reloads++},setInterval:fn=>{f.poll=fn},fetch:async()=>{
    if(f.fail)throw Error('disconnected');return {ok:true,json:async()=>({...f.state})};
  }});
  await new Promise(setImmediate);assert(!ids['#controls'].disabled);return f;
}
(async()=>{
  let f=await fixture();await f.poll();assert.equal(f.reloads,0);
  f.state.version='99.0.0';await f.poll();assert.equal(f.reloads,1);assert(f.ids['#controls'].disabled);
  f=await fixture();f.state.token='replacement';await f.poll();assert.equal(f.reloads,1);
  f=await fixture();f.fail=true;await f.poll();assert.equal(f.reloads,0);assert(f.ids['#status'].textContent.includes('断开'));
  f.fail=false;f.state.version='99.0.0';await f.poll();assert.equal(f.reloads,1);
  console.log('PASS: actual page reloads on version/token change and reconnect after an interrupted upgrade');
})().catch(e=>{console.error(e);process.exitCode=1});
