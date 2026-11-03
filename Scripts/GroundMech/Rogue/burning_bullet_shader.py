"""Pure, idempotent material-code migration, including the previously saved V1."""
import re


def migrate(code):
    existing = re.search(r'GULI_BURNING_BULLET_V[123]', code) is not None
    if existing:
        code = re.sub(r'// GULI_BURNING_BULLET_V[123][^\n]*\n', '', code)
        code = re.sub(r'float flameTime\s*=.*?;\s*', '', code)
        code = re.sub(r'float tongueWave\s*=.*?;\s*', '', code)
    else:
        code = re.sub(r'(float d\s*=.*?;)', r'\1\nd=max(0.0,d-abs(tongueWave)*max(Width*0.7,0.2));', code, count=1)
        code = code.replace('float body =', 'float tongues=exp(-pow(d/max(Width*2.2,0.2),2.0))*saturate(tongueWave*0.5+0.5)*0.5;\nfloat body =', 1)
        if 'float tongues=' not in code:
            raise ValueError('Unknown Ground trail material contract')
        code = code.replace('return float4(rgb,', 'rgb+=Emission*TailColor.rgb*tongues;\nreturn float4(rgb,', 1)
        code = code.replace('saturate(body+glow)', 'saturate(body+glow+tongues)')
    # A narrow orange core must not wash the requested red fire into white/yellow.
    code = code.replace('core*lerp(0.22,0.6,heat)', 'core*lerp(0.04,0.1,heat)')
    marker = '''// GULI_BURNING_BULLET_V3: ordered modulation, red fire with restrained hot core.
float flameTime=max(HeadDistance,0.0)/max(Speed,1.0)+max(EndAge,0.0);
float tongueWave=sin(along*0.027-flameTime*23.0)+0.6*sin(along*0.071+flameTime*17.0);
'''
    code, count = re.subn(r'(float along\s*=.*?;\s*)', lambda m: m[1] + marker, code, count=1)
    if count != 1 or code.index('float along') >= code.index('float tongueWave'):
        raise ValueError('Missing/invalid along declaration')
    return code


def apply_palette(unreal, material):
    """Absolute values, including duplicate parameter nodes in the source graph."""
    def linear(hex_color, gain):
        channels = [int(hex_color[i:i+2], 16) / 255.0 for i in (0, 2, 4)]
        return unreal.LinearColor(*[(v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4) * gain for v in channels], 1.0)
    # Art-standard sRGB bases, same-family emission only. Status fire is unchanged.
    colors = {'TailColor': ('8E3A2A', 1.8), 'HeadColor': ('8E3A2A', 2.5), 'CoreColor': ('EE9D58', 1.0)}
    found = set()
    for node in unreal.ObjectIterator(unreal.MaterialExpression):
        if node.get_outer() != material:
            continue
        if isinstance(node, unreal.MaterialExpressionVectorParameter):
            name = str(node.get_editor_property('parameter_name'))
            if name in colors:
                node.modify()
                node.set_editor_property('default_value', linear(*colors[name]))
                found.add(name)
        elif isinstance(node, unreal.MaterialExpressionScalarParameter) and str(node.get_editor_property('parameter_name')) == 'Emission':
            node.modify()
            node.set_editor_property('default_value', 2.5)
            found.add('Emission')
    assert found == set(colors) | {'Emission'}, 'Unknown burning material color contract'
    return {'bases_srgb': colors, 'emission': 2.5, 'core_weight': [.04, .1]}
