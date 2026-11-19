import math
import numpy as np
from mathutils import Vector

def fit_round(obj,ids):
    xyz=np.asarray([obj.matrix_world @ obj.data.vertices[i].co for i in ids],dtype=np.float64)
    center=xyz.mean(axis=0)
    eigen,axes=np.linalg.eigh(np.cov((xyz-center).T,bias=True))
    pairs=[(abs(eigen[a]-eigen[b])/max(eigen[a],eigen[b],1e-20),a,b) for a,b in ((0,1),(1,2),(0,2))]
    mismatch,a,b=min(pairs)
    if mismatch>0.0002:return None
    axial=3-a-b
    axis=axes[:,axial]
    major=int(np.argmax(np.abs(axis)))
    if axis[major]<0:axis=-axis
    helper=np.eye(3)[int(np.argmin(np.abs(axis)))]
    u=np.cross(axis,helper);u/=np.linalg.norm(u)
    v=np.cross(axis,u)
    q=xyz-center
    z=q @ axis
    radial=np.sqrt((q @ u)**2+(q @ v)**2)
    radius=(radial.max()+radial.min())/2
    minor=np.sqrt((radial-radius)**2+z*z)
    result={'center':center.tolist(),'axis':axis.tolist(),'u':u.tolist(),'v':v.tolist(),'radius':float(radial.max()),'height':float(z.max()-z.min())}
    if float(np.ptp(minor))<1e-5 and minor.mean()<radius*.35:
        result.update(kind='torus',major_radius=float(radius),minor_radius=float(minor.mean()))
    elif radial.min()>radial.max()*.65 and len(set(np.round(z,5)))<=12 and all(np.ptp(radial[np.round(z,5)==level])<0.00003 for level in set(np.round(z,5))):
        result.update(kind='cylinder')
    else:return None
    return result

def build_round(obj,fit,segments):
    c=np.asarray(fit['center']);axis=np.asarray(fit['axis']);u=np.asarray(fit['u']);v=np.asarray(fit['v'])
    xyz=[];normals=[];faces=[]
    if fit['kind']=='torus':
        R,r=fit['major_radius'],fit['minor_radius']
        tube=4
        for i in range(segments):
            theta=math.tau*i/segments
            direction=u*math.cos(theta)+v*math.sin(theta)
            for j in range(tube):
                phi=math.tau*j/tube
                xyz.append(c+direction*(R+r*math.cos(phi))+axis*(r*math.sin(phi)))
                normals.append(direction*math.cos(phi)+axis*math.sin(phi))
        for i in range(segments):
            for j in range(tube):
                a=i*tube+j;b=((i+1)%segments)*tube+j;cc=((i+1)%segments)*tube+(j+1)%tube;d=i*tube+(j+1)%tube
                faces.extend(((a,b,cc),(a,cc,d)))
    else:
        R,h=fit['radius'],fit['height']
        for z in (-h/2,h/2):
            for i in range(segments):
                theta=math.tau*i/segments
                direction=u*math.cos(theta)+v*math.sin(theta)
                xyz.append(c+axis*z+direction*R)
                normals.append(direction)
        for i in range(segments):
            j=(i+1)%segments
            faces.extend(((i,j,segments+j),(i,segments+j,segments+i)))
        for i in range(1,segments-1):
            faces.extend(((0,i+1,i),(segments,segments+i,segments+i+1)))
    inverse=obj.matrix_world.inverted()
    transform=inverse.to_3x3()
    points=[inverse @ Vector(p) for p in xyz]
    local_normals=[(transform @ Vector(n)).normalized() for n in normals]
    # Correct any orientation convention once, preserving the common loop ordering.
    p=faces[0]
    n=(points[p[1]]-points[p[0]]).cross(points[p[2]]-points[p[0]])
    if n.dot(local_normals[p[0]])<0:faces=[tuple(reversed(f)) for f in faces]
    return points,faces,local_normals

def rules(obj,component,fit,extent,center):
    name=obj.name
    armor='蓝灰' in name or name.startswith('Side_Panel')
    rear_layers=name.startswith('Rear_Armor') and '深灰' in name
    support=name.startswith('Body_Chassis') and '黑色' in name and component>=2
    outline=(max(extent)>.35 and min(extent)>.035 and fit is None)
    error=.035
    minimum=8
    if armor:error=.010;minimum=48;outline=True
    if name.startswith('Side_Panel'):error=.006;minimum=48
    if rear_layers:error=.010;minimum=48;outline=True
    if name.startswith('Body_Chassis') and '黑色' in name and component<2:error=.012;minimum=96;outline=True
    if support:error=.012;minimum=28;outline=False
    if name.startswith('Leg_') and ('深灰' in name or '米白' in name):error=.025;minimum=16;outline='米白' not in name
    if '橡胶' in name:outline=False
    round_segments=None
    if fit:
        outline=False
        if fit['kind']=='torus':
            round_segments=16 if fit['major_radius']>.15 else 12
            if abs(fit['axis'][0])<.99:round_segments=8
        elif name.startswith('Leg_') and fit['radius']>.035 and abs(fit['axis'][0])>.99:
            round_segments=12 if fit['radius']<.16 else 16
            if '橡胶' in name:round_segments=8
    return {'outline':outline,'error':error,'minimum':minimum,'round_segments':round_segments,'rigid_bearing':bool(fit and name.startswith('Leg_') and (fit['kind']=='torus' or (fit['radius']>.035 and abs(fit['axis'][0])>.99)))}
