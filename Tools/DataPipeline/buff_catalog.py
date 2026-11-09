"""Excel contracts for persistent Buffs and Ground effect references."""
import math

def validate(tables, error):
    def rows(name): return tables.get(name, {}).get('rows', [])
    buffs={r['Id']:r for r in rows('DT_GuLiStrikeBuffs_Buffs')}
    effects={r['Id']:r for r in rows('DT_GuLiStrikeRogueCards_Effects')}
    texts={r['TextId'] for r in rows('DT_GuLiStrikeGameTexts_Texts')}
    vfx={r['Id'] for r in rows('DT_GuLiStrikeVfx_Effects')}
    configs={s:{r['Id']:r for r in rows('DT_GuLiStrikeBuffs_'+s)} for s in ('Burning','BurningBurst')}
    classes={'/Script/GuLiStrike.GuLiBurningBuffHandler':'Burning','/Script/GuLiStrike.GuLiBurningBurstBuffHandler':'BurningBurst'}
    def fail(message): raise error(message)
    def positive(value): return isinstance(value,(int,float)) and math.isfinite(value) and value>0
    for ident,b in buffs.items():
        kind=classes.get(b['ImplementationClass'])
        if ident<=0 or b['Version']!=2 or b['DescriptionTextId'] not in texts or not kind:
            fail(f'Buff {ident}: ID、版本、公共描述或处理器类无效')
        c=configs[kind].get(b['ConfigId'])
        if not c: fail(f'Buff {ident}: 缺少 {kind} 配置 {b["ConfigId"]}')
        if kind=='Burning':
            if not all(positive(c[k]) for k in ('DurationSeconds','TickIntervalSeconds','MaxHealthDamageRatio')) or c['DurationSeconds']>3600 or c['TickIntervalSeconds']>c['DurationSeconds'] or c['MaxHealthDamageRatio']>1:
                fail(f'Buff {ident}: 灼烧持续时间、周期或伤害比例无效')
            if any(c[k] not in vfx for k in ('StatusVfxId','BulletVfxId')): fail(f'Buff {ident}: 未知VFX引用')
        else:
            parent=buffs.get(c['ParentBuffId'])
            if not parent or parent['ImplementationClass']!='/Script/GuLiStrike.GuLiBurningBuffHandler' or c['ParentBuffId']==ident:
                fail(f'Buff {ident}: 爆裂必须引用灼烧父Buff')
            if not all(positive(c[k]) for k in ('IntervalSeconds','LostHealthDamageRatio','RadiusCentimeters')) or c['IntervalSeconds']>3600 or c['LostHealthDamageRatio']>1 or c['RadiusCentimeters']>100000:
                fail(f'Buff {ident}: 爆裂周期、伤害比例或范围无效')
            if c['BurstVfxId'] not in vfx: fail(f'Buff {ident}: 未知爆裂VFX')
    ricochet={r['Id']:r for r in rows('DT_GuLiStrikeRogueCards_GroundRicochet')}
    weapon={r['Id']:r for r in rows('DT_GuLiStrikeRogueCards_GroundWeaponEffects')}
    for ident,e in effects.items():
        if ident<=0 or e['Version']!=(2 if e['Handler']=='ground/explosive_package' else 1): fail(f'Effect {ident}: ID或版本无效')
        if e['Handler']=='ricochet':
            c=ricochet.get(e['ConfigId'])
            if not c or e['BuffId']!=0 or not 0<c['MinimumHops']<=c['MaximumHops']<=(65536-8192)//192 or c['HopsPerStack']<=0 or not positive(c['RangeCentimeters']) or c['RangeCentimeters']>1000000 or not positive(c['DamageRetention']) or c['DamageRetention']>1:
                fail(f'Effect {ident}: 折射配置无效')
        elif e['Handler']=='buff/on_hit':
            if e['BuffId'] not in buffs or e['ConfigId']!=0: fail(f'Effect {ident}: Buff引用无效')
        elif e['Handler'] in ('ground/fire_rate','ground/enhanced_bullet','ground/explosive_package','ground/explosive_enhancement'):
            c=weapon.get(e['ConfigId'])
            if not c or e['BuffId']!=0: fail(f'Effect {ident}: 缺少武器效果配置')
            if e['Handler']=='ground/fire_rate' and (not positive(c['FireRateBonusPercent']) or c['FireRateBonusPercent']>=29):
                fail(f'Effect {ident}: 攻速增量无效')
            if e['Handler']=='ground/explosive_enhancement':
                try:
                    radius=list(map(float,c['ExplosiveRadiusBonuses']))
                    damage=list(map(float,c['ExplosiveDamageBonuses']))
                except (ValueError,TypeError): fail(f'Effect {ident}: 爆炸强化增量不是数字')
                if not radius or len(radius)>16 or len(radius)!=len(damage) or any(not positive(v) or v>1 for v in radius+damage):
                    fail(f'Effect {ident}: 爆炸强化逐级半径/伤害增量无效')
            if e['Handler']=='ground/explosive_package':
                intervals=c['ExplosiveIntervals']
                if not intervals or len(intervals)>16 or any(not str(n).isdigit() or int(n)<=0 for n in intervals): fail(f'Effect {ident}: 发射间隔无效')
                nums=list(map(int,intervals))
                if any(a<=b for a,b in zip(nums,nums[1:])): fail(f'Effect {ident}: 等级间隔必须递减')
                if not positive(c['ExplosiveDamageMultiplier']) or not positive(c['ExplosiveRadiusCentimeters']) or c['ExplosiveRadiusCentimeters']>100000: fail(f'Effect {ident}: 爆炸数值无效')
                if c['ExplosiveBulletVfxId'] not in vfx or c['ExplosiveImpactVfxId'] not in vfx: fail(f'Effect {ident}: 爆破VFX引用无效')
                impact=next(r for r in rows('DT_GuLiStrikeVfx_Effects') if r['Id']==c['ExplosiveImpactVfxId'])
                if impact.get('RadiusAtUnitScaleCm')!=c['ExplosiveRadiusCentimeters']:
                    fail(f'Effect {ident}: 爆炸基础半径必须与命中特效的单位缩放标定半径一致')
                if not positive(c['ExplosiveSpeedMultiplier']) or c['ExplosiveSpeedMultiplier']>1 or not math.isfinite(c['ExplosiveOutwardOffsetCentimeters']) or not 0<=c['ExplosiveOutwardOffsetCentimeters']<=1000:
                    fail(f'Effect {ident}: 爆炸弹速度或外侧枪口偏移无效')
        else: fail(f'Effect {ident}: 未注册的处理器 {e["Handler"]}')
    for card in rows('DT_GuLiStrikeRogueCards_Cards'):
        refs=card.get('EffectIds',[])
        if card['Type']==3:
            if not refs or len(refs)!=len(set(refs)) or any(not str(i).isdigit() or int(i) not in effects for i in refs):
                fail(f'卡牌 {card["Id"]}: 未知或重复EffectIds')
            for ref in refs:
                effect=effects[int(ref)]
                if effect['Handler']=='ground/explosive_enhancement':
                    config=weapon[effect['ConfigId']]
                    if card['MaxAcquisitions']!=len(config['ExplosiveRadiusBonuses']):
                        fail(f'卡牌 {card["Id"]}: 等级上限与爆炸强化参数数量不一致')
                    cards={r['Id']:r for r in rows('DT_GuLiStrikeRogueCards_Cards')}
                    if not any(any(effects[int(i)]['Handler']=='ground/explosive_package' for i in cards.get(p,{}).get('EffectIds',[])) for p in card['RequiredCardIds']):
                        fail(f'卡牌 {card["Id"]}: 爆炸强化必须前置爆破套餐')
        elif refs: fail(f'卡牌 {card["Id"]}: 指挥官卡不混用Ground效果目录')
    return effects,buffs
