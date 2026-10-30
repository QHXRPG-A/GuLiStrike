"""The approved outpost palette and its current material-only HDR treatment.

These are internal authored shader constants, not new runtime/CPD parameters.
"""
STYLE_VERSION = 'GuLiOutpostHDR_WhiteBelt_Core_x4_v1'
PALETTE_HEX = ('#FEE4D9', '#274E61', '#6AA4BE', '#1A182F', '#0D9099', '#6AA4BE')
HDR_PEAKS = {'column': 8.8, 'line': 24.0, 'teal_slot': 5.6, 'cyan_slot': 8.0}
GAIN_VS_HDR_V1 = {'column': 4.0, 'line': 4.0, 'teal_slot': 2.0, 'cyan_slot': 2.0}
# Both waist light layers occupy +/-21 cm; other fixed slots start beyond 3 m.
BELT_HALF_HEIGHT_CM = 50.0
CHROMA_PROTECTION = 1.35

def linear(hex_color):
    rgb = [int(hex_color[i:i+2], 16) / 255 for i in (1, 3, 5)]
    return tuple(v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in rgb)

def surface_code():
    colors = ['float3(' + ','.join(f'{v:.10f}' for v in linear(h)) + ')' for h in PALETTE_HEX]
    return '''// ''' + STYLE_VERSION + ''': HDR is restricted to source material classes 4..7.
float m=floor(Class.x+.5);
float owned=step(.5,Owned)*step(.5,Enabled);
float3 c=''' + colors[0] + ''';
if(m==1)c=''' + colors[1] + ''';else if(m==2)c=''' + colors[2] + ''';else if(m==3)c=''' + colors[3] + ''';
float light=saturate(dot(normalize(Normal),normalize(float3(-.45,-.5,.74)))*.5+.5);
float band=light<.36?.46:(light<.76?.73:1.0);
float3 ink=float3(.01033,.00913,.02843);
float3 paint=lerp(c*band,ink,saturate(Line)*.85);
// Retain a colored broad column; concentrate the highest HDR in its narrow lines.
// Normalize the peak before boosting so darker enemy paint is not a dim lamp.
// A modest chroma curve keeps low channels below the highlight shoulder.
if(m>=4){
    float3 energy=m==4?''' + colors[4] + ''':(m==5?''' + colors[5] + ''':lerp(float3(1,1,1),Primary.rgb,owned));
    // The user's white waist band is separate from the colored guard/crown slots.
    if((m==4||m==5)&&abs(LocalPosition.z)<''' + str(BELT_HALF_HEIGHT_CM) + ''')energy=float3(1,1,1);
    energy=max(energy,float3(0,0,0));
    float peak=max(max(energy.r,energy.g),energy.b);
    float3 chroma=pow(saturate(energy/max(peak,.0001)),''' + str(CHROMA_PROTECTION) + ''');
    float hdr=m==4?''' + str(HDR_PEAKS['teal_slot']) + ''':(m==5?''' + str(HDR_PEAKS['cyan_slot']) + ''':(m==6?''' + str(HDR_PEAKS['column']) + ''':''' + str(HDR_PEAKS['line']) + '''));
    float gain=m>=6?lerp(1.0,max(0.0,Strength)/.35,owned):1.0;
    paint=chroma*hdr*gain;
}
return paint;'''
