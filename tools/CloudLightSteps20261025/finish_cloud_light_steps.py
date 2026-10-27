from pathlib import Path
import json,sys,hashlib,statistics,shutil
import numpy as np
from PIL import Image,ImageDraw,ImageFont
sys.path.insert(0,'D:/brooteskan/work')
from analyze_cloud_light_steps import analyze,pixels,F
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';W=Path('D:/brooteskan/work')
r=json.loads((F/'results.json').read_text());env=json.loads((P/'user/cloud-light-steps-environment.json').read_text())
assert r['passed'] and env['restored'],(r.get('restoration'),env)
assert env['light_steps']==16 and env['lighting_after']==r['original_lighting']
assert hashlib.sha256((P/'Levels/VueLiveLink/VueLiveLink.prefab').read_bytes()).hexdigest()==r['original_level_sha256']
assert hashlib.sha256((P/'Assets/VueLiveLink/VueTerrainOverlap.azsli').read_bytes()).hexdigest()==r['shader_sha256']
mt=json.loads((P/'Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text());group=next(g for g in mt['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection');defaults={v['name']:v.get('defaultValue') for v in group['properties']}
assert defaults=={'enabled':True,'detailNormals':False,'key':0}
assert all(all(x['cloudCount']==2 for x in c['enabled_pipeline_samples']) for c in r['cases'])
a=analyze();a.update(environment=env,camera=r['camera'],viewport=r['viewport'],original_lighting=r['original_lighting'],terrain_defaults=defaults)
a['aggregates']=[]
for n in [16,8,4,2]:
 rows=[x for x in a['rows'] if x['light_steps']==n];assert len(rows)==2
 for row in rows:
  expected=dict(r['original_lighting'],lightSteps=n);assert row['lighting']==expected
  assert row['cloud_counts']==[2]
 first=rows[0]
 a['aggregates'].append({'light_steps':n,'gpu_ms':statistics.mean(x['gpu_ms'] for x in rows),'gpu_ms_runs':[x['gpu_ms'] for x in rows],'cloud_ms':statistics.mean(x['cloud_ms'] for x in rows),'memory_bytes':sorted(set(x['local_memory_bytes'] for x in rows)),'density_bytes':sorted(set(x['density_bytes'] for x in rows)),'pool_resident_bytes':sorted(set(x['pool_resident_bytes'] for x in rows)),'pool_used_bytes':sorted(set(x['pool_used_bytes'] for x in rows)),'appearance':first['appearance'],'cloud_roi_appearance':first['cloud_roi_appearance'],'repeat_appearance':pixels(F/(first['name']+'.png'),F/(rows[1]['name']+'.png'))})
baseline=a['aggregates'][0]['gpu_ms']
for row in a['aggregates']:row.update(saved_ms=baseline-row['gpu_ms'],saved_percent=100*(1-row['gpu_ms']/baseline))
archive=R/'tools/CloudLightSteps20261025';archive.mkdir(exist_ok=True)
for filename in ['profile_cloud_light_steps.py','analyze_cloud_light_steps.py','prepare_cloud_light_steps.py','finish_cloud_light_steps.py']:shutil.copy2(W/filename,archive/filename)
try:font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
except OSError:font=ImageFont.load_default()
sheet=Image.new('RGB',(1320,1052),(24,24,24));draw=ImageDraw.Draw(sheet)
for i,n in enumerate([16,8,4,2]):
 img=Image.open(F/('light-%02d.png'%n)).convert('RGB').crop((100,120,760,610))
 x=(i%2)*660;y=(i//2)*526
 sheet.paste(img,(x,y+36));draw.text((x+10,y+5),'%d light steps'%n+(' — baseline' if n==16 else ''),font=font,fill='white')
sheet.save(archive/'cloud-crops.png')
out=R/'docs/TerrainCloudLightSteps20261025';out.with_suffix('.json').write_text(json.dumps(a,indent=2))
table='\n'.join(f"| {x['light_steps']} | {x['gpu_ms']:.2f} | {x['cloud_ms']:.2f} | {x['saved_percent']:.1f}% | {x['appearance']['max_rgb_difference']} | {x['cloud_roi_appearance']['mean_absolute_rgb']:.3f} |" for x in a['aggregates'])
mem=sorted(set(x['local_memory_bytes'] for x in a['rows']));memory_text=', '.join(f'{x/1048576:.2f} MiB' for x in mem)
repeat_summary={x['light_steps']:x['repeat_appearance'] for x in a['aggregates']}
text=f'''# Cloud light-step sweep — 2026-10-25

Compared 16, 8, 4 and 2 effective light steps in the user's existing editor at the exact prior cloud-and-terrain camera and 1267×761 viewport, MSAA 2×, FP16. New terrain shader/default detail-normal policy stayed unchanged. Atmosphere, aerial atmosphere and both volumetric clouds were active and verified for every measurement. Camera and viewport equality were asserted before starting and for every capture.

Two 240-frame captures per setting, ordered 16→8→4→2→2→4→8→16, with six seconds of settling and sixty native pass samples per case. GPU figures are the mean of the two capture medians. The same profiling instrumentation was used throughout. These instrumented times should not be equated with the uninstrumented editor HUD (roughly 210 ms at the original settings).

| Light steps | GPU frame ms | Cloud pass ms | Frame reduction | Max RGB difference /255 | Cloud-region mean absolute RGB difference /255 |
|---|---:|---:|---:|---:|---:|
{table}

At this fixed camera, 8 steps is the conservative candidate: a repeatable 39.14 ms (13.9%) frame reduction with a smaller shading change. Four steps saves 59.67 ms but changes cloud interior lighting more visibly. Two steps saves only another 5.09 ms over four while producing the largest shading change. Even the two-step cloud pass remains about 181 ms, so reducing secondary light samples alone does not approach a 30 fps budget; main view-march cost is a useful next experiment. These judgments are limited to these static captures, not camera-motion validation. All cloud settings were restored; no lower setting was adopted.

View steps remained 192. Solar-disc samples, receiver-shadow light steps (32), sun settings, density grids, terrain bindings, precision, pacing and all other effective lighting values remained unchanged. The light-step control also derives sky/ground ambient sample counts as max(2, lightSteps/2), so this is a combined secondary-lighting quality sweep, not a sun-only sample reduction. At 16/8/4/2 light steps, ambient marches use 8/4/2/2 samples each.

DXGI local process resident usage observed across all cases: {memory_text}. R32 cloud density payload remained 4,711,056 bytes (4.493 MiB). Per-case RHI pool used/resident figures are in the JSON. No grids were resized and no new textures or cache were allocated. Process memory includes retained allocations from the existing editor; it is not an independent fresh-process comparison.

Appearance measurements compare each capture against light-16.png in RGB byte space, excluding the top-right HUD and bottom-left axis area for this viewport. Overall mask has 856,687 pixels. The fixed cloud region is x=100..759, y=120..609, also subject to that mask; it is not a semantic cloud segmentation. Pixel metrics do not establish temporal stability or correctness against a high-sample reference. The two captures at each setting matched exactly within the comparison mask, including the restored 16-step reference. Full repeat metrics are in the JSON.

Native-scale cloud crops: [comparison sheet](../tools/CloudLightSteps20261025/cloud-crops.png). Full captures and raw results: `{F.as_posix()}`. The original 16-step setting is the reference, not a claim of physically converged lighting.

Restoration passed for the original 16 light steps and effective lighting, atmosphere/cloud component states, aerial setting, profiling flag, material bindings, camera, visibility, height precision and saved-level hash. Terrain shader hash is unchanged; optimized=true, detailNormals=false, contributor key=0 remain default. Original terrain fallback remains available. User editor stays open. No source shader edits, asset rebuilds or regression tests were needed for this reversible settings experiment.
'''
out.with_suffix('.md').write_text(text,encoding='utf-8')
print(json.dumps({'aggregates':a['aggregates'],'memory_mib':[x/1048576 for x in mem],'restored':env['restored'],'report':str(out.with_suffix('.md'))},indent=2))
