"""Save the approved map's real F4 entry, fixture markers and observation cameras.

This is editor authoring and entity readback only. It never starts PIE, invokes
the fixture, applies a card or enables the production cluster visual.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'WM01MissileReview20260929'
ED = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
report = {'success':False, 'map':MAP, 'saved':False, 'actors':[], 'runtime_validation':'not_run'}

def actor(cls, label, location, tags=()):
    matches = [a for a in ED.get_all_level_actors() if a.get_actor_label() == label]
    assert len(matches) <= 1, label
    a = matches[0] if matches else ED.spawn_actor_from_class(cls,unreal.Vector(*location))
    assert isinstance(a,cls), label
    a.set_actor_label(label)
    a.set_actor_location(unreal.Vector(*location),False,True)
    a.set_folder_path('Review/WM01Missiles')
    a.set_editor_property('tags',[unreal.Name(t) for t in [TAG,*tags]])
    return a

try:
    assert not LEVEL.is_in_play_in_editor()
    assert WORLD.get_path_name().split('.')[0] == MAP, 'Load the existing designated map before authoring.'
    assert globals().get('WM01_SMOKE_PREVIEW',{}).get('complete',True), 'Finish the transient art preview first.'
    center = (-8000,72000,-1300)
    actor(unreal.TargetPoint,'WM01Missile_FixtureCenter',center,['WM01MissileFixtureCenter'])
    for label,tag,location,target,fov in [
        ('WM01Missile_Camera_Overview','WM01MissileFixedCamera',(-8000,62000,7200),(-8000,67600,-1200),95),
        ('WM01Missile_Camera_Near','WM01MissileNearCamera',(-12000,64200,1200),(-11200,67400,-1200),65)]:
        a=actor(unreal.CameraActor,label,location,[tag])
        a.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*location),unreal.Vector(*target)),True)
        a.camera_component.set_field_of_view(fov)
        a.camera_component.set_editor_property('constrain_aspect_ratio',True)
        a.camera_component.set_editor_property('aspect_ratio',16/9)
    entry=actor(unreal.Note,'WM01Missile_CandidateInstructions',(-8000,73500,-1200))
    entry.set_editor_property('text',
        '重防号 WM01 / UnitTypeId=2 导弹候选 v2；加载本轮原生尺寸/寿命改动后使用。\n'
        'v2：弹体/尾焰/烟宽1.5倍；尾焰峰值3倍；浓黑烟迹2.4秒，退场槽保留2.45秒。\n'
        '玩法验收：新局就绪指挥官，选择重防号，初始无 Q 且无背部导弹仓；F4 获 04.01 后现有及后续本队重防号解锁，另一队保持原状；05.01 每次齐射 +1。换任保留，新局重置。\n'
        '候选开关：gs.MissileCluster.Candidate 1；旧表现对照：gs.MissileCluster.Candidate 0。正式 DA 开关保持关闭，待用户可播放候选审核。\n'
        '冷启动验收：候选资源准备完成后应自动切到放大弹体、短亮尾焰与2.4秒黑烟，不能持续回退为旧白色尾焰；本轮已修正延迟编译请求未被触发的问题，游戏内结果待玩家复查。\n'
        '性能场景：每次新开 PIE 并就绪，执行一组 Build，再执行 gs.MissileFixture.Fire；每次施放遵守真实冷却。Build 仅为性能准备解锁和弹量，不替代 F4 功能验收，不移除其他地图单位。\n'
        '镜头：Build 自动固定总览；gs.MissileFixture.Camera near 查看近景；overview 恢复对照镜头。\n'
        '指标：gs.MissileFixture.Metrics。新旧必须同机、分辨率、画质、总览镜头；分别记录发射/飞行/命中后 CPU 表现、Niagara GPU、半透明和帧时间。当前未运行或测量。')
    cases=[]
    for row,count in enumerate([100,500]):
        for col,salvo in enumerate([1,4,8]):
            label=f'WM01Missile_Case_{count}x{salvo}'
            a=actor(unreal.Note,label,(-10500+col*2500,75000+row*1000,-1200),[f'WM01MissileCase_{count}_{salvo}'])
            command=f'gs.MissileFixture.Build {count} {salvo}'
            a.set_editor_property('text',f'{count} 台重防号 × 每台 {salvo} 发 = {count*salvo} 枚。\n每组重新开始 PIE，指挥官就绪后执行：\n{command}\ngs.MissileFixture.Fire\ngs.MissileFixture.Metrics\n新旧对照仅切 Candidate 0/1，保持总览镜头和画质相同。')
            cases.append({'actor':label,'units':count,'salvo':salvo,'missiles':count*salvo,'command':command})
    f4=next(a for a in ED.get_all_level_actors() if a.get_actor_label()=='RogueCards_F4_Entry')
    text=str(f4.get_editor_property('text'))
    marker='\n[WM01 导弹候选 20260929]'
    text=text.split(marker)[0]
    f4.set_editor_property('text',text+marker+'\n初始重防号无 Q、无导弹仓；F4 获 04.01 后本队现有及后续单位解锁。03.01/05.01 依赖 04.01；04.01 每队每局一次。候选与六组集群场景见 WM01Missile_CandidateInstructions，构建/运行/性能/视觉审核状态分别记录。')
    report['cases']=cases
    report['saved']=bool(LEVEL.save_current_level())
    assert report['saved'], 'Map save failed'
    # Reread actual related entities after saving; do not infer them from creation calls.
    related=[a for a in ED.get_all_level_actors() if a.actor_has_tag(TAG) or a.get_actor_label()=='RogueCards_F4_Entry']
    for a in sorted(related,key=lambda x:x.get_actor_label()):
        row={'label':a.get_actor_label(),'object':a.get_path_name(),'class':a.get_class().get_name(),
             'location':list(a.get_actor_location().to_tuple()),'rotation':list(a.get_actor_rotation().to_tuple()),
             'tags':[str(t) for t in a.tags]}
        if isinstance(a,unreal.Note):row['text']=str(a.get_editor_property('text'))
        if isinstance(a,unreal.CameraActor):row['fov']=a.camera_component.field_of_view
        report['actors'].append(row)
    assert len(related)==11 and len([a for a in related if a.actor_has_tag('WM01MissileFixedCamera')])==1
    report['game_mode']=WORLD.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    report['native_capacity_helper_loaded']=hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'get_missile_pod_mesh_diagnostics')
    report['cluster_assets']=[unreal.load_asset('/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_'+q).get_path_name() for q in ['Full','Lite','Minimal']]
    report['projectile']=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile').get_path_name()
    report['production_cluster_enabled']=bool(unreal.load_asset(report['projectile']).get_editor_property('use_missile_cluster_rendering'))
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
output=ROOT/'Artifacts/WM01MissileCards/Visual_v2/scene-readback.json'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],'actors':len(report['actors']),'error':report.get('error'),'report':str(output)}))
