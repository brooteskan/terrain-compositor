from pathlib import Path
W=Path('D:/brooteskan/work')
s=(W/'profile_cloud_terrain_camera.py').read_text()
s=s.replace('profile_saved=None;', 'quality_saved=None;profile_saved=None;')
s=s.replace("  assert general.get_current_level_name(),'No level open'", """  assert general.get_current_level_name(),'No level open'
  reference=json.loads((P/'user/TerrainSceneProfile/20261025-cloud-terrain-camera/results.json').read_text())
  pos=general.get_current_view_position();rot=general.get_current_view_rotation();size=general.get_viewport_size()
  current=[[pos.x,pos.y,pos.z],[rot.x,rot.y,rot.z]]
  assert current==reference['camera'],('Camera changed',current)
  assert [int(size.x),int(size.y)]==reference['viewport'],'Viewport changed'
  mt=json.loads((P/'Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text())
  group=next(g for g in mt['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection')
  defaults={v['name']:v.get('defaultValue') for v in group['properties']}
  assert defaults=={'enabled':True,'detailNormals':False,'key':0},defaults""")
s=s.replace("  profile_saved=(comp,path,api._get(comp,path));api._set(comp,path,True)", """  profile_saved=(comp,path,api._get(comp,path));api._set(comp,path,True)
  quality_path=next(str(x) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp) if str(x).endswith('Reference and quality|Light samples'))
  quality_saved=(comp,quality_path,api._get(comp,quality_path))
  assert quality_saved[2]==16,quality_saved[2]
  cloud_owner=general.find_editor_entity('clouds')
  original_lighting=json.loads(render.CloudSceneRequestBus(bus.Event,'GetLighting',cloud_owner))
  assert original_lighting['viewSteps']==192 and original_lighting['lightSteps']==16,original_lighting
  def lighting():return json.loads(render.CloudSceneRequestBus(bus.Event,'GetLighting',cloud_owner))
  def select_steps(count):
   api._set(quality_saved[0],quality_saved[1],count)
   general.idle_wait_frames(4)
   actual=lighting();assert actual['lightSteps']==count,actual
   for key,value in original_lighting.items():
    if key!='lightSteps':assert actual[key]==value,(key,actual[key],value)
   assert api._get(quality_saved[0],quality_saved[1])==count
   return actual""")
s=s.replace('general.idle_wait_frames(240)','general.idle_wait_frames(30)')
start=s.index("  root=W/'lookup-cause/medium64'")
end=s.index("  output=P/",start)
s=s[:start]+"""  def case(n,suffix=''):return {'name':'light-%02d'%n+suffix,'screenshot':True,'light_steps':n}
  cases=[case(16),case(8),case(4),case(2),case(2,'-repeat'),case(4,'-repeat'),case(8,'-repeat'),case(16,'-final')]
"""+s[end:]
s=s.replace('20261025-cloud-terrain-camera\'\n  cfg=', '20261025-cloud-light-steps\'\n  cfg=')
start=s.index('  needle="  if case.get(\'material_overrides\'):"')
end=s.index('  from OverlapPageMemory import memory',start)
s=s[:start]+"""  needle="  if case.get('material_overrides'):"
  assert needle in source;source=source.replace(needle,"  if 'light_steps' in case:SELECT_STEPS(case['light_steps'])\\n"+needle)
  source=source.replace("entry={'name':name,'configuration':case,'cache_before':counters()}","entry={'name':name,'configuration':case,'cache_before':counters(),'video_memory':PROCESS_MEMORY(),'lighting':LIGHTING()}")
  source=source.replace("R['camera']=CAMERA;R['viewport']=VIEWPORT", "R['camera']=CAMERA;R['viewport']=VIEWPORT;R['shader_sha256']=SHADER_SHA256;R['environment_enabled']=ENVIRONMENT_ENABLED;R['original_lighting']=ORIGINAL_LIGHTING;R['terrain_defaults']=DEFAULTS")
  source=source.replace("entry['cache_after']=counters()","entry['cache_after']=counters();entry['enabled_pipeline_samples']=NATIVE_SAMPLES()")
"""+s[end:]
start=s.index("  exec(compile(source,'ProfileTerrainScene.py','exec'),dict(")
end=s.index('\n except Exception:',start)
s=s[:start]+"  exec(compile(source,'ProfileTerrainScene.py','exec'),dict(SELECT_STEPS=select_steps,LIGHTING=lighting,SHADER_SHA256=hashlib.sha256((P/'Assets/VueLiveLink/VueTerrainOverlap.azsli').read_bytes()).hexdigest(),PROCESS_MEMORY=memory,ENVIRONMENT_ENABLED=environment,ORIGINAL_LIGHTING=original_lighting,DEFAULTS=defaults,NATIVE_SAMPLES=native_samples,__name__='__main__'))"+s[end:]
s=s.replace('  for comp,path,old in cloud_saved:', "  if quality_saved:\n   comp,path,value=quality_saved;api._set(comp,path,value);general.idle_wait_frames(4)\n   restored=(api._get(comp,path)==value and lighting()==original_lighting) and restored\n  for comp,path,old in cloud_saved:")
s=s.replace('cloud-terrain-camera-environment.json', 'cloud-light-steps-environment.json')
s=s.replace('cloud-terrain-camera-controls.json', 'cloud-light-steps-volume-controls.json')
s=s.replace('cloud-enabled-verification.json', 'cloud-light-steps-enabled-verification.json')
s=s.replace('overlap-pages-editor-error.txt', 'cloud-light-steps-error.txt')
s=s.replace("{'enabled':environment,'aerial_controls':len(aerial),'restored':restored}", "{'enabled':environment,'aerial_controls':len(aerial),'restored':restored,'light_steps':quality_saved[2] if quality_saved else None,'lighting_after':lighting() if quality_saved else None}")
(W/'profile_cloud_light_steps.py').write_text(s)
compile(s,'profile_cloud_light_steps.py','exec')
print('Prepared 16/8/4/2-step sweep and reverse repeats; original 16 restored in finally')
