"""Assemble actual Blender/UE evidence and the two newly completed deliveries."""
import hashlib
import html
import json
import zipfile
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
OUT=ROOT/'UE_Integration'
LABELS={'Twin_Barrel_Turret':'双联炮','CIWS':'CIWS 近防炮','Thor_MissilePod':'Thor 一级 / 二级导弹舱',
    'Autocannon':'自动炮','Triple_Barrel_Turret':'三联炮','Single_Barrel_Turret':'单管炮',
    'Drone_LaunchBay':'无人机发射舱','Electronic_JammingDevice':'电子干扰装置','Shield_Generator':'护盾发生器',
    'Bottom_Twin_Barrel_Turret':'底置双联炮','High_Rate_Fire_Cannon':'高射速炮',
    'Incendiary_Bomb_LaunchBay':'燃烧弹发射舱','Missile_Bay':'导弹舱'}
CSS='''*{box-sizing:border-box}body{margin:0;background:#162733;color:#e7eeee;font:16px/1.7 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1480px;margin:auto;padding:35px 30px 80px}h1{font-size:36px;margin:8px 0}h2{font-size:24px;margin:30px 0 12px}.lead{color:#b5c9d2;max-width:1000px}nav{display:flex;flex-wrap:wrap;gap:10px;margin:25px 0}a{color:#dfb469;text-decoration:none}nav a,.links a{padding:6px 12px;border:1px solid #3c5667;border-radius:6px}section{padding:24px;background:#203642;border:1px solid #3a505e;border-radius:12px;margin:24px 0}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.views{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}figure{margin:0}img,video{width:100%;border-radius:7px;display:block;background:#728792}figcaption{color:#bfd0d6;padding:7px 0}.links{display:flex;flex-wrap:wrap;gap:10px;margin:18px 0}.badge{color:#efc980}small{color:#aac0ca}code{font-size:13px;overflow-wrap:anywhere}@media(max-width:850px){.grid,.views{grid-template-columns:1fr}main{padding:15px}}'''
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def dump(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
def image(path,label):return f'<figure><a href="{path}" target="_blank"><img src="{path}" loading="lazy" alt="{html.escape(label)}"></a><figcaption>{label}</figcaption></figure>'
def document(title,intro,body):return f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{title}</title><style>{CSS}</style><main><small>GuLiStrike · Ship components · 2026.09.17</small><h1>{title}</h1><p class="lead">{intro}</p>{body}</main></html>'

def main():
    allrows={}
    for batch,version in (('','v4'),('Batch02','v2'),('Batch03','v3'),('Batch04','v1')):
        folder=ROOT/batch
        snapshot=json.loads((folder/'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
        for key,source in snapshot['parts'].items():
            delivery=folder/'Delivery'/version
            readback=json.loads((delivery/'Validation'/f'{key}_FBX_Readback.json').read_text(encoding='utf-8'))
            assert readback['passed']
            allrows[key]={'batch':batch or 'Batch01','version':version,'root':folder,'delivery':delivery,'source':source,'readback':readback}
    actual=json.loads((OUT/'formal_import_report.json').read_text(encoding='utf-8')) if (OUT/'formal_import_report.json').exists() else {}
    evidence=json.loads((OUT/'UE_visual_motion_report.json').read_text(encoding='utf-8')) if (OUT/'UE_visual_motion_report.json').exists() else {}
    saved=json.loads((OUT/'saved_asset_readback.json').read_text(encoding='utf-8'))
    assert actual.get('success') and evidence.get('success') and saved.get('passed')
    nav='<nav>'+''.join(f'<a href="#{k}">{LABELS[k]}</a>' for k in allrows)+'</nav>'
    body=nav
    for key,row in allrows.items():
        prefix='../'+(row['batch']+'/' if row['batch']!='Batch01' else '')
        blend=f'{prefix}Delivery/{row["version"]}/Blender/SC_{key}_Styled.blend'
        fbx=f'{prefix}Delivery/{row["version"]}/'+row['readback']['fbx_file'].replace('\\','/')
        hero=f'{prefix}Previews/{row["version"]}/{key}_hero.png'
        body+=f'<section id="{key}"><h2>{LABELS[key]}</h2><p><span class="badge">{row["batch"]} · {row["version"]}</span>　原网格保留 · 2K 色块 / 内线遮罩 · 独立外轮廓</p><div class="grid">'
        body+=image(hero,'Blender 实际材质')
        ue=OUT/'Previews'/f'{key}_hero.png'
        if ue.exists():body+=image('Previews/'+ue.name,'UE 正式资源实际渲染')
        body+='</div><div class="links">'+f'<a href="{blend}">可编辑 Blender</a><a href="{fbx}">FBX</a></div>'
        body+=f'<small><code>{html.escape(row["source"]["visual_mesh"].split(".")[0])}</code></small>'
        if key=='Incendiary_Bomb_LaunchBay':
            body+='<p>白色火焰绘制在两端朝外的橙色装甲上；五组舱体和五个发射挂点保留。</p><div class="grid">'
            for view in ('flame_front','flame_rear'):
                file=OUT/'Previews'/f'{key}_{view}.png'
                if file.exists():body+=image('Previews/'+file.name,'UE 外侧火焰标识')
            body+='</div>'
        if key=='Drone_LaunchBay':
            body+='<p>僚机与发射箭头位于薄壁朝外一侧。</p>'
            file=OUT/'Previews'/f'{key}_exterior_mark.png'
            if file.exists():body+=image('Previews/'+file.name,'UE 无人机标识外侧')
        body+='</section>'
    body+='<section><h2>版本与验证</h2><p>首批 v4、第三批 v3 维持已有 B 通过记录；第二批 v2 与第四批 v1 依据用户“开始实施，然后把这些组件导入至ue替换正式资源”的直接放行执行，不补写未发生的 B 视觉通过。</p><p>UE 沿用舰体的三档美术光方向材质，外框线由可关闭的独立叠加材质绘制，主体投射场景阴影。表面明暗方向通过 ArtLightDirection 调整，未声明自动采样任意场景灯光及自阴影。</p><div class="links"><a href="formal_import_report.json">UE 导入核对</a><a href="UE_visual_motion_report.json">UE 画面与运动</a><a href="rollback_manifest.json">回退资源清单</a><a href="release_authorization_20260917.json">用户放行记录</a><a href="../Batch04/Review_B_v1.html">最后四件完整对照</a></div></section>'
    body+='<section><h2>保存与装船核对</h2><p>13件保存回读通过；7门炮637个连续姿态通过，26个发射挂点保持。静态碰撞已从导入前备份完整恢复并比对。</p><p>原有船体缺少 bottom_mid_0，双联炮和自动炮在该首选槽安装失败；另6个蓝图未配置兼容槽位。本轮保持原玩法配置，完整实战与性能未验证。</p><div class="links"><a href="saved_asset_readback.json">保存后独立回读</a><a href="collision_restoration.json">原碰撞恢复</a><a href="delivery_manifest.json">交付与文件哈希</a></div></section>'
    (OUT/'Review_Formal_v1.html').write_text(document('Ship 全组件正式资源',
        '13 个独立模型，覆盖 14 个正式组件蓝图。原模型尺寸、安装原点、骨骼和挂点保留。'+('UE 正式替换与回读已完成。' if actual.get('success') else 'UE 正式替换正在验证中。'),body),encoding='utf-8')
    batch=ROOT/'Batch04';body=''
    approval=json.loads((batch/'approval_A_20260917.json').read_text(encoding='utf-8'))
    for key,part in approval['parts'].items():
        body+=f'<section><h2>{LABELS[key]}</h2><div class="grid">'+image(part['reference_sheet']['path'],'已确认参考 '+part['reference_version'])+image(f'Previews/v1/{key}_hero.png','Blender 实际效果')+'</div><div class="views">'
        for view,label in (('front','正视'),('right','右侧'),('rear','背视')):body+=image(f'Previews/v1/{key}_{view}.png',label)
        body+='</div><div class="grid">'+image(f'Previews/v1/{key}_original_geometry_gray.png','原网格灰模')+image(f'Previews/v1/{key}_alternate_light.png','替换光照方向')+'</div>'
        if key in ('Bottom_Twin_Barrel_Turret','High_Rate_Fire_Cannon'):
            body+=f'<video src="Previews/v1/{key}_pitch_motion.mp4" controls preload="metadata"></video><p>−15° 至 75° 连续俯仰；底置炮正俯仰沿原有 −Z 向外。</p>'
        body+='</section>'
    (batch/'Review_B_v1.html').write_text(document('剩余四件 · 实际材质 v1',
        '原模型表面制作完成。燃烧弹舱两端白色火焰；底置双联炮赭黄、高射速炮钢蓝、导弹舱砖红。用户已直接授权实施并替换正式 UE 资源；此页面提供实际成品对照，不冒充额外视觉批准。',body),encoding='utf-8')
    archives=[]
    for batch,version in (('Batch02','v2'),('Batch04','v1')):
        folder=ROOT/batch;delivery=folder/'Delivery'/version
        archive=folder/f'ShipComponentStyle_{batch}_20260917_{version}_Delivery.zip'
        files=sorted(p for p in delivery.rglob('*') if p.is_file() and p.suffix!='.blend1' and p.name!='Delivery_File_Hashes.json')
        hashes={str(p.relative_to(delivery)).replace('\\','/'):digest(p) for p in files}
        dump(delivery/'Validation/Delivery_File_Hashes.json',hashes)
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
            for p in files:z.write(p,str(p.relative_to(delivery)))
            z.write(delivery/'Validation/Delivery_File_Hashes.json','Validation/Delivery_File_Hashes.json')
        with zipfile.ZipFile(archive) as z:
            assert len(z.namelist())==len(set(z.namelist()))
            assert z.testzip() is None
            assert all(hashlib.sha256(z.read(name)).hexdigest()==value for name,value in hashes.items())
        archives.append({'batch':batch,'version':version,'path':str(archive.relative_to(ROOT)),
            'sha256':digest(archive),'bytes':archive.stat().st_size,'files':len(files)+1})
    dump(OUT/'delivery_manifest.json',{'unique_models':13,'formal_blueprints':14,'new_archives':archives,
        'UE_import_success':actual.get('success',False),'UE_visual_motion_success':evidence.get('success',False),
        'UE_saved_readback_passed':saved.get('passed',False),
        'parts':{k:{'batch':v['batch'],'version':v['version'],'delivery':str(v['delivery'].relative_to(ROOT)),
            'FBX_sha256':v['readback']['fbx_sha256'],'FBX_readback_passed':True,'formal_mesh':v['source']['visual_mesh']} for k,v in allrows.items()}})
    print('FORMAL_REVIEW_PACKAGED',len(allrows),json.dumps(archives),flush=True)

if __name__=='__main__':main()
