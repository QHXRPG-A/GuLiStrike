"""Promote the explicitly authorized B_v1 paint, preserving existing formal paths.

Old meshes are consolidated into the checked replacement before that replacement
is moved back to the old stable path. Shared engine meshes are never overwritten.
Excel remains the only runtime binding source. Run in the live editor.
"""
import json,sys
from pathlib import Path
import unreal

ROOT=Path(r'D:/UE5.7/test1')
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
sys.path.insert(0,str(ROOT/'Scripts/Models'))
from migrate_model_workbooks import read,write
REVIEW='/Game/GuLiStrike/Review/ModelInterface'
FORMAL='/Game/GuLiStrike/Models/TeamColor_v1'
approval=json.loads((OUT/'formal-import-authorization.json').read_text(encoding='utf8'))
staging=json.loads((OUT/'engine-candidate-staging.json').read_text(encoding='utf8'))
expected=set(approval['model_names'])|{'BiZhiMaoConstruction'}
if staging['errors'] or {m['model'] for m in staging['staged']}!=expected:
    raise RuntimeError('All authorized model replacements must be checked before promotion')
bindings=[b for m in staging['staged'] for b in m['bindings']]
lib=unreal.EditorAssetLibrary
report_path=OUT/'formal-promotion.json'
report=json.loads(report_path.read_text(encoding='utf8')) if report_path.exists() else dict(authorization=approval['user_message'],source_sha256=approval['source_sha256'],models=[],replacements=[],moved_materials=[],retired=[],shared_retained=[],errors=[],tables_imported=False)
completed={b['id']:b for b in report['replacements']}
def checkpoint():
    (OUT/'formal-promotion.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
def package(path):return path.split('.')[0]
candidate_packages={package(b['candidate']) for b in bindings}
old_materials={}
for b in bindings:
    if b['id'] in completed:
        b['formal']=completed[b['id']]['formal'];continue
    asset=unreal.load_object(None,b['candidate'])
    if not asset:raise RuntimeError('Missing checked candidate '+b['candidate'])
    if isinstance(asset,(unreal.StaticMesh,unreal.SkeletalMesh)):
        validation=json.loads(lib.get_metadata_tag(asset,'GuLi.PaintValidation'))
        if not validation['source_triangle_attributes_exact'] or not validation['geometric_vertices_exact']:
            raise RuntimeError('Color-only validation failed '+b['candidate'])
        original=unreal.load_object(None,b['source'])
        for slot in original.get_editor_property('materials' if isinstance(original,unreal.SkeletalMesh) else 'static_materials'):
            material=slot.material_interface
            while material and material.get_path_name().startswith('/Game/') and not material.get_path_name().startswith(FORMAL+'/'):
                old_materials[material.get_path_name()]=material
                material=material.get_editor_property('parent') if isinstance(material,unreal.MaterialInstanceConstant) else None

# Record material dependency edges before moves. Rename/save alone can serialize
# references through an old review path and leave a null parent after restart.
dependency_edges = []
for path in lib.list_assets(REVIEW,True,False):
    asset = unreal.load_asset(path)
    if isinstance(asset,unreal.MaterialInstanceConstant):
        parent = asset.get_editor_property('parent')
        if not parent:raise RuntimeError('Candidate material has no parent: '+path)
        dependency_edges.append(('parent',package(path),parent.get_path_name(),None))
    elif isinstance(asset,unreal.Material):
        for call in unreal.ObjectIterator(unreal.MaterialExpressionMaterialFunctionCall):
            if call.get_outer()!=asset:continue
            function=call.get_editor_property('material_function')
            if not function:raise RuntimeError('Candidate has a missing material function: '+path)
            dependency_edges.append(('function',package(path),function.get_path_name(),call.get_name()))
# Move new material graphs and the common function into the formal art library.
for path in sorted(lib.list_assets(REVIEW,True,False)):
    source=package(path)
    if source in candidate_packages:continue
    asset=unreal.load_asset(path)
    if not asset or package(asset.get_path_name())!=source:continue
    if not isinstance(asset,(unreal.Material,unreal.MaterialInstanceConstant,unreal.MaterialFunction)):continue
    target=source.replace(REVIEW,FORMAL,1)
    if lib.does_asset_exist(target):raise RuntimeError('Formal target already exists '+target)
    if not lib.rename_asset(source,target):raise RuntimeError('Unable to move derived material '+source)
    asset=unreal.load_asset(target)
    lib.set_metadata_tag(asset,'GuLi.ModelApproved','1')
    lib.set_metadata_tag(asset,'GuLi.SourceBVersion','LocalTeamColorProduction_B_v1_20261008')
    assert lib.save_loaded_asset(asset,False)
    report['moved_materials'].append(dict(source=source,target=target));checkpoint()

for kind,source,dependency,node_name in dependency_edges:
    asset=unreal.load_asset(source.replace(REVIEW,FORMAL,1))
    target=unreal.load_object(None,dependency.replace(REVIEW,FORMAL,1))
    if not asset or not target:raise RuntimeError('Moved material dependency is missing: '+source)
    if kind=='parent':
        unreal.MaterialEditingLibrary.set_material_instance_parent(asset,target)
        unreal.MaterialEditingLibrary.update_material_instance(asset)
    else:
        call=next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionMaterialFunctionCall)
                  if e.get_outer()==asset and e.get_name()==node_name)
        if not call.set_material_function(target):raise RuntimeError('Moved function could not restore its pins: '+source)
        unreal.MaterialEditingLibrary.recompile_material(asset)
    if not lib.save_loaded_asset(asset,False):raise RuntimeError('Moved dependency could not save: '+source)

for b in bindings:
    if b['id'] in completed:continue
    candidate=unreal.load_object(None,b['candidate'])
    old_package=package(b['source'])
    if old_package.startswith('/Engine/'):
        target=FORMAL+'/ResourceFactory/AccessRamp/Meshes/SM_RPF_AccessRamp'
        report['shared_retained'].append(dict(path=b['source'],reason='Shared engine mesh; only this assembly receives a painted copy'))
    else:
        old=unreal.load_object(None,b['source'])
        if isinstance(old,unreal.Class):old=unreal.load_asset(old_package)
        target=old_package
        if isinstance(candidate,unreal.Class):
            # Generated-class redirectors share the old Blueprint package.
            # Preserve their compatibility route and move the new BP to the
            # formal library instead of colliding with those class objects.
            target=FORMAL+'/ResourceFactory/Blueprints/BP_ResourceProcessingFactory'
            data=lib.find_asset_data(old_package)
            if str(data.asset_class_path.asset_name)!='ObjectRedirector' and not lib.consolidate_assets(unreal.load_asset(package(b['candidate'])),[old]):
                raise RuntimeError('Blueprint reference consolidation failed '+b['source'])
        elif not lib.consolidate_assets(candidate,[old]):
            raise RuntimeError('Reference consolidation failed '+b['source'])
        # Immediately restore the stable path with the checked new object.
        # Unloaded soft references therefore retain exactly their former path.
        if not isinstance(candidate,unreal.Class) and lib.does_asset_exist(old_package) and not lib.delete_asset(old_package):
            raise RuntimeError('Unable to remove old redirector '+old_package)
    if not lib.rename_asset(package(b['candidate']),target):raise RuntimeError('Unable to restore formal path '+target)
    asset=unreal.load_asset(target)
    lib.set_metadata_tag(asset,'GuLi.ModelApproved','1')
    lib.set_metadata_tag(asset,'GuLi.SourceBHash',approval['source_sha256'])
    lib.set_metadata_tag(asset,'GuLi.ModelId',str(b['id']))
    assert lib.save_loaded_asset(asset,False)
    final=asset.generated_class().get_path_name() if isinstance(asset,unreal.Blueprint) else asset.get_path_name()
    b['formal']=final
    report['replacements'].append(dict(id=b['id'],source=b['source'],candidate=b['candidate'],formal=final,old_object_deleted=not old_package.startswith('/Engine/')))
    checkpoint()

# Old red meshes are color-only duplicates of the former blue canonical mesh.
for m in staging['staged']:
    if not m['model'].startswith('SSF_'):continue
    b=m['bindings'][0]
    red=b['source'].replace('/Blue/','/Red/').replace('SK_Blue_','SK_Red_')
    old=unreal.load_object(None,red)
    if not old:continue
    for slot in old.get_editor_property('materials'):
        if slot.material_interface:old_materials[slot.material_interface.get_path_name()]=slot.material_interface
    new=unreal.load_object(None,b['formal'])
    if not lib.consolidate_assets(new,[old]):raise RuntimeError('Unable to retire color duplicate '+red)
    report['retired'].append(dict(path=red,replacement=b['formal'],kind='old_red_mesh'))
    checkpoint()

# Remove only obsolete material objects with no remaining asset or source-code
# references. Protected glass/display/outline and shared dependencies stay live.
source_files=list((ROOT/'Source').rglob('*.cpp'))+list((ROOT/'Source').rglob('*.h'))
source_text='\n'.join(p.read_text(encoding='utf8',errors='replace') for p in source_files)
for _ in range(3):
    for path,old in list(old_materials.items()):
        if not lib.does_asset_exist(package(path)):old_materials.pop(path);continue
        refs=[str(p) for p in lib.find_package_referencers_for_asset(package(path),True) if str(p)!=package(path)]
        if refs or package(path) in source_text:continue
        if lib.delete_asset(package(path)):
            report['retired'].append(dict(path=path,kind='unused_old_material'));old_materials.pop(path);checkpoint()
for path in old_materials:
    report['shared_retained'].append(dict(path=path,reason='Still referenced by fixed display/outline, shared material, or source code'))

book=ROOT/'Data/Excel/GuLiStrikeModels.xlsx'
grid=read(book,'Models');cols=grid[0]
by_id={int(r[0]):r for r in grid[3:] if r and r[0]}
for b in bindings:
    row=by_id[b['id']];row+=['']*(len(cols)-len(row))
    row[cols.index('ResourcePath')]=b['formal']
    row[cols.index('CandidateResourcePath')]=''
    row[cols.index('CandidateVATDefinition')]=''
    row[cols.index('Note')]='B_v1 已按用户指令导入正式资源；只改配色，固定区和队色区分开；旧网格引用已替换'
write(book,'Models',grid)
ids={m['id'] for m in staging['staged']}
for sheet in ('MaterialParameters','ColorRegions'):
    grid=read(book,sheet);cols=grid[0]
    for row in grid[3:]:
        row+=['']*(len(cols)-len(row))
        if int(row[cols.index('ModelId')]) not in ids:continue
        scope=row[cols.index('Scope')]
        if sheet=='MaterialParameters':
            if scope=='Existing' and str(row[cols.index('bTeamManaged')]).lower() in ('true','1'):
                row[cols.index('ParameterKey')]='LegacyTeamColor'
                row[cols.index('bTeamManaged')]=False
                row[cols.index('bRuntimeWritable')]=False
            if scope=='Candidate':row[cols.index('Scope')]='Existing'
        elif scope=='Candidate':row[cols.index('Scope')]='Existing'
        else:
            row[cols.index('Scope')]='Candidate'
            row[cols.index('Note')]='历史队色遮罩；已由正式顶点 Alpha 分区替代，不参与运行时'
    write(book,sheet,grid)
report['models']=[dict(name=m['model'],id=m['id']) for m in staging['staged']]
report['success']=True
checkpoint()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,models=len(report['models']),replaced=len(report['replacements']),retired=len(report['retired']),formal_materials=len(report['moved_materials']))))
