"""Shared readable three-tone shading for formal local team paint."""
GRAY = (.02518686, .03820437, .03560131, 1)
INK = 'float3(.02518686,.03820437,.03560131)'
FILL = 'float3(.030,.030,.030)*(1-saturate(dot(PALETTE,float3(.2126,.7152,.0722))/.18))'

def revise(code):
    if 'GuLiDarkSurfaceFill_v1' in code:
        return code
    # Only the confirmed body tone stage is touched. Animation, UV, masks,
    # lighting direction/thresholds, fixed paint and CPD bindings are unchanged.
    if 'return lerp(paint,Ink,saturate(Mask));' in code:
        code = code.replace('float3(.43,.52,.66)', 'float3(.62,.66,.74)').replace('float3(.72,.78,.88)', 'float3(.82,.86,.92)')
        code = code.replace('return lerp(paint,Ink,saturate(Mask));',
                            'paint+=' + FILL.replace('PALETTE', 'Base') + ';return lerp(paint,Ink,saturate(Mask)*.78);')
    elif 'return lerp(Color*band' in code:
        code = code.replace('?.42:light<.55?.74:1', '?.62:light<.55?.82:1')
        code = code.replace('Color*band', 'Color*band+' + FILL.replace('PALETTE', 'Color'))
        code = code.replace('float3(.01764,.013,.01444)', INK).replace('Mask*Lines)', 'Mask*Lines*.78)')
    elif 'return lerp(palette*Base*band,Ink,inkMask);' in code:
        code = code.replace('?0.40:(d<0.68?0.72:1.0)', '?0.62:(d<0.68?0.82:1.0)')
        code = code.replace('return lerp(palette*Base*band,Ink,inkMask);',
                            'return lerp(palette*Base*band+' + FILL.replace('PALETTE', 'palette*Base') + ',Ink,inkMask*.78);')
    elif 'lerp(c*band,float3(' in code:
        code = code.replace('?.42:(d<.55?.74:1)', '?.62:(d<.55?.82:1)')
        code = code.replace('c*band,float3(0.010960094006488246,0.017641954488384078,0.01599629336550963)',
                            'c*band+' + FILL.replace('PALETTE', 'c') + ',' + INK)
    elif 'return lerp(Palette*band' in code:
        code = code.replace('?.40:(d<.68?.72:1.0)', '?.62:(d<.68?.82:1.0)')
        code = code.replace('Palette*band,float3(.025,.030,.028),edge)',
                            'Palette*band+' + FILL.replace('PALETTE', 'Palette') + ',' + INK + ',edge*.65)')
    else:
        raise RuntimeError('Unexpected body tone stage: ' + code[:100])
    return '// GuLiDarkSurfaceFill_v1: user requested lighter dark blocks, 2026-10-08\n' + code
