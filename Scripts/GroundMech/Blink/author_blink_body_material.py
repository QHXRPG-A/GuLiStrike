"""Build the approved skeletal phase lattice; no image/runtime validation."""
import json
from pathlib import Path
import unreal

TARGET = '/Game/GuLiStrike/FX/GroundMech/Blink/M_GroundMech_BlinkPhase'
COMMON = r'''
float a = saturate(Strength);
float frame = floor(max(0.0, Now-StartAt) * max(1.0, FlickerHz));
float3 cell = floor(P / 23.0);
float h = frac(sin(dot(cell,float3(12.9898,78.233,37.719))+frame*17.13)*43758.5453);
float band = frac(sin(floor(P.z/16.0)*23.13+frame*3.1)*43758.54);
float3 q=P*.019; q += .28*sin(q.yzx*1.7+float3(.2,1.7,3.1));
float3 grid=abs(frac(float3(q.x+q.y*.57,q.y+q.z*.67,q.z+q.x*.43))-.5);
float wire=1-smoothstep(.018,.060,min(grid.x,min(grid.y,grid.z)));
float keep=(1-smoothstep(.94,.998,band))*max(smoothstep(.02,.12,h),wire);
'''
COLOR = COMMON + r'''
float rim=pow(1-saturate(abs(dot(normalize(Nrm),normalize(View)))),3);
float pulse=.85+.15*frac(sin(frame*18.63+.3)*43758.54);
float3 base=SourceColor.rgb;
float tone=saturate(dot(base,float3(.2126,.7152,.0722)));
float plate=saturate(ArmorLight*pow(tone,.6));
float3 shell=lerp(float3(.002,.017,.001),float3(.55,1.0,.26)*Emission*.72,plate);
shell=lerp(shell,float3(.95,.88,.012)*Emission*.9,AccentLight);
shell=lerp(shell,float3(.45,1.0,.25)*Emission*1.3,EdgeSolid);
float3 light=shell+EnergyTint.rgb*Emission*pulse*(wire*1.2+rim*.75);
light+=float3(.7,1,.6)*Emission*pow(saturate(wire*rim),3)*.14;
return lerp(base,light,a)*saturate(ProxyFade);
'''
MASK = COMMON + r'''
float seed=frac(sin(dot(floor(P/9.0),float3(13.91,71.31,29.71)))*19341.31);
float dissolve=smoothstep(seed-.025,seed+.025,saturate(ProxyFade));
return lerp(1,keep,a)*dissolve;
'''
OFFSET = r'''
float frame=floor(max(0.0,Now-StartAt)*max(1.0,FlickerHz));
float band=floor(P.z/16.0);
float h=frac(sin(band*23.13+frame*3.1)*43758.54);
float jump=step(.78,h)*(h-.5)*2;
return saturate(Strength)*JitterCm*jump*(-normalize(DashDirection)*.85+normalize(Nrm)*.22);
'''


def main():
    raise RuntimeError('Direct body-material authoring is retired. Use author_blink_material_variants.py for the independent energy proxy.')


if __name__ == '__main__':main()
