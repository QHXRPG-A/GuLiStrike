"""Check saved assets, live controls, rigid sampling bounds and RGB SSIM.
No screenshot or visual inspection is performed.
"""
from pathlib import Path
import sys,json,hashlib,urllib.request,base64,io
sys.path.insert(0,'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright
from skimage.metrics import structural_similarity
B=Path(__file__).resolve().parent
m=json.loads((B/'manifest.json').read_text(encoding='utf-8'))
url=m['preview']['url']
resources=[]
for l in m['layers']:
    local=(B/l['file']).read_bytes()
    with urllib.request.urlopen(url+l['file']) as r:
        remote=r.read();code=r.status
    assert hashlib.sha256(local).hexdigest()==l['sha256']==hashlib.sha256(remote).hexdigest()
    with Image.open(io.BytesIO(remote)) as im:
        assert list(im.size)==m['geometry']['canvas']
        ar=np.array(im.convert('RGBA'))[:,:,3]
    assert (int(ar.min()),int(ar.max()))==((255,255) if l['index']==6 else (0,255))
    resources.append(dict(index=l['index'],http=code,sha256=l['sha256'],actual_alpha_range=[int(ar.min()),int(ar.max())]))
errors=[]
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe')
    page=browser.new_page(viewport=dict(width=1440,height=1100),device_scale_factor=1)
    page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto(url,wait_until='networkidle')
    page.wait_for_function('window.cardPreview && window.cardPreview.getState().ready')
    def state():return page.evaluate('window.cardPreview.getState()')
    initial=state()
    assert len(initial['drawLog'])==6 and all('layer' in x for x in initial['drawLog'])
    page.click('button[data-mode="reference"]');page.evaluate('window.cardPreview.renderNow()')
    assert state()['drawLog']==[dict(kind='reference')]
    page.click('button[data-mode="compare"]');page.evaluate('window.cardPreview.renderNow()')
    assert len(state()['drawLog'])==7
    page.click('button[data-mode="stack"]');page.evaluate('window.cardPreview.renderNow()')
    solo_checks=[]
    for idx in range(1,7):
        page.click(f'.layer[data-layer="{idx}"] button')
        page.evaluate('window.cardPreview.renderNow()')
        st=state()
        assert st['solo']==idx and len(st['drawLog'])==1 and st['drawLog'][0]['layer']==idx
        size=page.eval_on_selector('#art','c=>[c.width,c.height]')
        assert size==m['geometry']['canvas']
        solo_checks.append(dict(index=idx,draw_count=1,canvas=size))
        page.click(f'.layer[data-layer="{idx}"] button')
    page.click('#reset');page.click('button[data-mode="parallax"]')
    page.eval_on_selector('#strength',"e=>{e.value='100';e.dispatchEvent(new Event('input',{bubbles:true}))}")
    bounds=[]
    g=m['geometry'];W,H=g['canvas'];x0,y0,x1,y1=g['visible_window']
    for x,y in [(-1,-1),(-1,1),(1,-1),(1,1)]:
        page.evaluate('p=>window.cardPreview.setPose(p[0],p[1])',[x,y])
        st=state();calls={d['layer']:d for d in st['drawLog']}
        assert calls[4]['dx']==calls[5]['dx'] and calls[4]['dy']==calls[5]['dy']
        for idx,c in calls.items():
            margins=[x0-c['dx'],y0-c['dy'],W-(x1-c['dx']),H-(y1-c['dy'])]
            assert min(margins)>g['filter_margin']
            bounds.append(dict(pose=[x,y],layer=idx,dx=c['dx'],dy=c['dy'],minimum_canvas_margin=min(margins)))
    slider=page.locator('.layer[data-layer="3"] input[type=range]')
    slider.evaluate("e=>{e.value='37';e.dispatchEvent(new Event('input',{bubbles:true}))}")
    page.evaluate('window.cardPreview.renderNow()')
    assert next(x for x in state()['drawLog'] if x['layer']==3)['opacity']==.37
    page.click('#reset')
    page.click('button[data-mode="stack"]')
    page.evaluate('window.cardPreview.renderNow()')
    ratio=page.eval_on_selector('#card','e=>{const r=e.getBoundingClientRect();return r.width/r.height}')
    assert abs(ratio/g['aperture_ratio']-1)<.0001
    stacked=page.eval_on_selector('#art','c=>c.toDataURL("image/png")').split(',')[1]
    page.click('button[data-mode="reference"]')
    page.evaluate('window.cardPreview.renderNow()')
    reference=page.eval_on_selector('#art','c=>c.toDataURL("image/png")').split(',')[1]
    bs=base64.b64decode(stacked);br=base64.b64decode(reference)
    (B/'Checks/browser_stack_numeric.png').write_bytes(bs)
    (B/'Checks/browser_reference_numeric.png').write_bytes(br)
    sa=np.array(Image.open(io.BytesIO(bs)).convert('RGB'),np.float32)/255
    ra=np.array(Image.open(io.BytesIO(br)).convert('RGB'),np.float32)/255
    metric=float(structural_similarity(ra,sa,data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,win_size=11,use_sample_covariance=False))
    page.click('button[data-mode="stack"]')
    page.evaluate('window.cardPreview.renderNow()')
    final=state()
    assert not errors
    browser.close()
report=dict(resources=resources,original_mode='only_user_reference',stack_mode='six_generated_layers_only',comparison='reference_plus_six_layers',solo=solo_checks,extreme_poses=bounds,explosion_and_ground_motion='identical',opacity_control='passed',display_aspect=ratio,target_aspect=g['aperture_ratio'],browser_visible_window_ssim=metric,full_original_window_ssim=m['similarity']['rgb_ssim'],ssim_threshold=.8,browser_ssim_passed=metric>=.8,console_errors=errors,assistant_visual_validation=False,canvas_exports='numerical_measurement_only',final_mode=final['mode'])
(B/'Checks/browser_verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(resources=len(resources),solo_layers=len(solo_checks),pose_layer_checks=len(bounds),display_ratio=ratio,browser_ssim=metric,ssim_passed=metric>=.8,errors=errors)),flush=True)

