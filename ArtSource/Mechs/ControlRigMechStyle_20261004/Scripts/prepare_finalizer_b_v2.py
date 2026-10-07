from pathlib import Path
p=Path(__file__).with_name('finalize_production_b_v2.py')
s=p.read_text(encoding='utf-8')
changes=[
('无同向重复面','无同向重复或反向重合面'),
('LOD1–3 分别存在 4 / 45 / 140 个简化后反向重合面，作为薄件厚度/双面处理余项登记，不当成无缺陷拓扑。','B-v1 的远档整体压面破坏装配、序列被编码成首帧重复，已由助手终检撤回；用户未审核或否定该版。B-v2 保留相同近景网格，远档改为保守角度整理和近景法线转移，保留全部源部件，预算差额明显增大；视频改用逐帧独立条带并解码比对。'),
('预算/远档薄件和可迁移细线','预算和可迁移细线'),
('远档薄件尚有反向重合面，','远档保留完整结构而预算差额较大，'),
('可迁移细线和远档薄件','可迁移细线'),
("'finish_review_scene_b_v1.py',","'finish_review_scene_b_v1.py','build_safe_lods_b_v2.py',")]
for old,new in changes:
    assert old in s,old
    s=s.replace(old,new)
for name in ('render_motion','audit_production','audit_motion_contact','render_lod_comparison','verify_movies','finalize_production'):
    s=s.replace(name+'_b_v1.py',name+'_b_v2.py')
p.write_text(s,encoding='utf-8')
