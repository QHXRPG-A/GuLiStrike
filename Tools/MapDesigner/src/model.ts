import { Brush, clone, coordSystem, Density, Field, Geometry, guid, identity, Issue, MapType, Marker, Project, Region, Shape, Terrain, Vec3 } from './types';
import gameplayProfile from '../gameplay-profile.json';

export const MAX_CELLS = 4 * 1024 * 1024;
export function newRegion(shape: Shape = 'PolygonPrism', key = 'Territory'): Region {
  const geometry: Geometry = shape === 'PolygonPrism' ? {vertices:[[-1500,-1500],[1500,-1500],[1500,1500],[-1500,1500]],min_z:-50000,max_z:50000}
    : shape === 'Box' ? {half_extents:[1500,1500,1500]} : shape === 'Sphere' ? {radius:1500} : {radius:1500,min_z:-1500,max_z:1500};
  return {region_id:guid(),region_key:key,display_name:key,enabled:true,shape_type:shape,geometry,relative_transform:identity()};
}
export function defaultTypes(): MapType[] {
  const field = (key:string,label:string,value:unknown,type:Field['field_type']='int32',minimum?:number,maximum?:number): Field => ({field_id:guid(),field_key:key,display_name:label,field_type:type,default:value,required:true,...(minimum === undefined ? {}:{minimum}),...(maximum===undefined?{}:{maximum})});
  const outpost: MapType = {type_id:'Outpost',asset_path:'/Game/GuLiStrike/Editor/MapAuthoring/Types/DA_Outpost.DA_Outpost',display_name:'据点',editor_icon:'Icons.Location',color_rgb:[0.34,0.52,0.28],color_alpha:0.25,allowed_shapes:['Cylinder','Sphere','Box','PolygonPrism'],fields:[field('BoardRow','棋盘行',1,'int32',1,7),field('BoardColumn','棋盘列',1,'int32',1,7),field('InitialOwner','初始归属','Neutral','name'),field('BlueClusterBudget','蓝矿簇预算',0,'int32',0,8),field('RedClusterBudget','红矿簇预算',0,'int32',0,6),field('Tier','等级',1)],default_regions:[newRegion('Cylinder','Capture'),newRegion()]};
  return [outpost,...['Resource','Generic'].map(type_id=>({type_id,asset_path:`/Game/GuLiStrike/Editor/MapAuthoring/Types/DA_${type_id}.DA_${type_id}`,display_name:type_id==='Resource'?'资源点':'通用标记',editor_icon:'Icons.Location',color_rgb:[0.1,0.5,0.6] as Vec3,color_alpha:0.2,allowed_shapes:['Cylinder','Sphere','Box','PolygonPrism'] as Shape[],fields:[],default_regions:[]}))];
}
export function newProject(name:string,nx:number,ny:number,heightM=9): Project {
  checkDimensions(nx,ny);
  const id=guid(), types=defaultTypes();
  return {schema:'guli-map-design/v1',id,name,map_package:'/Game/Maps/LVL_CommanderMassPrototype',terrain:{cells_x:nx,cells_y:ny,cell_size_cm:300,origin_cm:[-nx*150,-ny*150],heights_cm:new Array((nx+1)*(ny+1)).fill(heightM*100)},
    layout:{schema_version:1,map_package:'/Game/Maps/LVL_CommanderMassPrototype',length_unit:'cm',angle_unit:'degree',coordinate_system:coordSystem,types,markers:[]},
    density:{schema_version:1,map_package:'/Game/Maps/LVL_CommanderMassPrototype',length_unit:'cm',coordinate_system:'Unreal left-handed Z-up; world-aligned floor cells',density_map_id:guid(),cell_size_cm:300,territory_type_id:'Outpost',territory_region_key:'Territory',layers:[{layer_id:guid(),layer_key:'BlueOre',display_name:'蓝矿',color_rgb:[0.03,0.35,1],color_alpha:0.6,cells:[]},{layer_id:guid(),layer_key:'RedOre',display_name:'红矿',color_rgb:[1,0.05,0.025],color_alpha:0.6,cells:[]}]},
    editor:{ground_follow:{},field_layouts:{}},source:{new_map:true},baseline:{}};
}
export function checkDimensions(nx:number,ny:number): void {
  if(!Number.isInteger(nx)||!Number.isInteger(ny)||nx<1||ny<1||nx>8160||ny>8160||nx*ny>MAX_CELLS) throw Error('长宽须为1至8160整数格，总格数不超过4,194,304格。');
}
export function heightAt(t:Terrain,x:number,y:number): number {
  const u=Math.max(0,Math.min(t.cells_x,(x-t.origin_cm[0])/300)),v=Math.max(0,Math.min(t.cells_y,(y-t.origin_cm[1])/300));
  const ix=Math.min(Math.floor(u),t.cells_x-1),iy=Math.min(Math.floor(v),t.cells_y-1),fx=u-ix,fy=v-iy,i=iy*(t.cells_x+1)+ix;
  const [h00,h10,h01,h11]=[t.heights_cm[i],t.heights_cm[i+1],t.heights_cm[i+t.cells_x+1],t.heights_cm[i+t.cells_x+2]];
  return fx>=fy?h00+(h10-h00)*fx+(h11-h10)*fy:h00+(h11-h01)*fx+(h01-h00)*fy;
}
export function inside(t:Terrain,x:number,y:number):boolean {return x>=t.origin_cm[0]&&y>=t.origin_cm[1]&&x<=t.origin_cm[0]+t.cells_x*300&&y<=t.origin_cm[1]+t.cells_y*300;}
export function resizeTerrain(p:Project,nx:number,ny:number):void {
  checkDimensions(nx,ny);const t=p.terrain,values=new Array((nx+1)*(ny+1));
  for(let y=0;y<=ny;y++)for(let x=0;x<=nx;x++)values[y*(nx+1)+x]=t.heights_cm[Math.min(y,t.cells_y)*(t.cells_x+1)+Math.min(x,t.cells_x)];
  t.cells_x=nx;t.cells_y=ny;t.heights_cm=values;
}
export function rotate(v:Vec3,r:Vec3):Vec3 {
  const [p,y,w]=r.map(a=>a*Math.PI/180),cp=Math.cos(p),sp=Math.sin(p),cy=Math.cos(y),sy=Math.sin(y),cr=Math.cos(w),sr=Math.sin(w);
  return [v[0]*cp*cy+v[1]*(sr*sp*cy-cr*sy)-v[2]*(cr*sp*cy+sr*sy),v[0]*cp*sy+v[1]*(sr*sp*sy+cr*cy)+v[2]*(cy*sr-cr*sp*sy),v[0]*sp-v[1]*sr*cp+v[2]*cr*cp];
}
export function worldPoint(m:Marker,r:Region,v:Vec3):Vec3 {
  const local=rotate(v,r.relative_transform.rotation_pitch_yaw_roll).map((n,i)=>n+r.relative_transform.position[i]) as Vec3;
  return rotate(local,m.world_transform.rotation_pitch_yaw_roll).map((n,i)=>n+m.world_transform.position[i]) as Vec3;
}
export function outline(r:Region):Vec3[] {
  const g=r.geometry;
  if(r.shape_type==='PolygonPrism')return(g.vertices??[]).map(v=>[v[0],v[1],0]);
  if(r.shape_type==='Box'){const h=g.half_extents??[0,0,0];return [[-h[0],-h[1],0],[h[0],-h[1],0],[h[0],h[1],0],[-h[0],h[1],0]];}
  return Array.from({length:48},(_,i)=>[Math.cos(i*Math.PI/24)*(g.radius??0),Math.sin(i*Math.PI/24)*(g.radius??0),0]);
}
export function polygonContains(x:number,y:number,vertices:number[][]):0|1|2 {
  let hit=false;
  for(let i=0,j=vertices.length-1;i<vertices.length;j=i++){
    const [ax,ay]=vertices[j],[bx,by]=vertices[i],cross=(x-ax)*(by-ay)-(y-ay)*(bx-ax);
    if(Math.abs(cross)<=1e-7*Math.max(1,Math.hypot(bx-ax,by-ay))&&x>=Math.min(ax,bx)-1e-7&&x<=Math.max(ax,bx)+1e-7&&y>=Math.min(ay,by)-1e-7&&y<=Math.max(ay,by)+1e-7)return 2;
    if((ay>y)!==(by>y)&&x<(bx-ax)*(y-ay)/(by-ay)+ax)hit=!hit;
  }return hit?1:0;
}
export function polygonError(v:number[][]):string|null {
  if(v.length<3||v.length>4096)return '多边形需要3至4096个顶点。';
  const cross=(a:number[],b:number[],c:number[])=>(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
  const on=(a:number[],b:number[],c:number[])=>Math.abs(cross(a,b,c))<1e-7&&c[0]>=Math.min(a[0],b[0])-1e-7&&c[0]<=Math.max(a[0],b[0])+1e-7&&c[1]>=Math.min(a[1],b[1])-1e-7&&c[1]<=Math.max(a[1],b[1])+1e-7;
  let area=0;
  for(let i=0;i<v.length;i++){const a=v[i],b=v[(i+1)%v.length],prev=v[(i+v.length-1)%v.length];area+=a[0]*b[1]-b[0]*a[1];if(Math.hypot(a[0]-b[0],a[1]-b[1])<.001)return '存在零长度边。';if(Math.abs(cross(prev,a,b))<1e-7&&(on(prev,a,b)||on(a,b,prev)))return '相邻边重叠。';
    for(let j=i+2;j<v.length;j++){if(i===0&&j===v.length-1)continue;const c=v[j],d=v[(j+1)%v.length];if(Math.max(Math.min(a[0],b[0]),Math.min(c[0],d[0]))>Math.min(Math.max(a[0],b[0]),Math.max(c[0],d[0]))+1e-7||Math.max(Math.min(a[1],b[1]),Math.min(c[1],d[1]))>Math.min(Math.max(a[1],b[1]),Math.max(c[1],d[1]))+1e-7)continue;if(cross(a,b,c)*cross(a,b,d)<0&&cross(c,d,a)*cross(c,d,b)<0||on(a,b,c)||on(a,b,d)||on(c,d,a)||on(c,d,b))return '多边形自交或边重叠。';}
  }return Math.abs(area)<.001?'多边形面积接近零。':null;
}
export function markerCreate(p:Project,typeId:string,x:number,y:number):Marker {
  const type=p.layout.types.find(t=>t.type_id===typeId);if(!type)throw Error('请选择类型。');
  const occupied=new Set(p.layout.markers.map(m=>m.marker_key));let key=`${typeId}_01`,i=1;while(occupied.has(key))key=`${typeId}_${String(++i).padStart(2,'0')}`;
  if(typeId==='Outpost')for(let r=1;r<=7;r++)for(let c=1;c<=7;c++)if(!occupied.has(`Outpost_R${r}C${c}`)){key=`Outpost_R${r}C${c}`;r=8;break;}
  const m:Marker={marker_id:guid(),marker_key:key,type_id:typeId,display_name:type.display_name,enabled:true,note:'',tags:[],parameters:Object.fromEntries(type.fields.map(f=>[f.field_key,clone(f.default)])),regions:type.default_regions.map(r=>({...clone(r),region_id:guid()})),world_transform:identity()};
  const match=key.match(/^Outpost_R([1-7])C([1-7])$/);if(match){const row=Number(match[1]),col=Number(match[2]);m.parameters.BoardRow=row;m.parameters.BoardColumn=col;m.parameters.BlueClusterBudget=gameplayProfile.blue_budgets[row-1][col-1];m.parameters.RedClusterBudget=gameplayProfile.red_budgets[row-1][col-1];m.parameters.InitialOwner=col===4&&row===1?'Red':col===4&&row===7?'Blue':'Neutral';}
  m.world_transform.position=[x,y,heightAt(p.terrain,x,y)];p.layout.markers.push(m);p.editor.ground_follow[m.marker_id]=true;p.editor.field_layouts[m.marker_id]=clone(type.fields);return m;
}
export function duplicateMarker(p:Project,id:string):Marker {
  const original=p.layout.markers.find(m=>m.marker_id===id);if(!original)throw Error('未选中标记。');
  const m=clone(original);m.marker_id=guid();m.regions.forEach(r=>r.region_id=guid());let n=1;while(p.layout.markers.some(v=>v.marker_key===`${original.marker_key}_Copy${n}`))n++;
  m.marker_key=`${original.marker_key}_Copy${n}`;m.display_name+=' 副本';m.world_transform.position[0]+=300;
  if(p.editor.ground_follow[id]!==false){const a=original.world_transform.position,b=m.world_transform.position;b[2]+=heightAt(p.terrain,b[0],b[1])-heightAt(p.terrain,a[0],a[1]);}
  p.editor.ground_follow[m.marker_id]=p.editor.ground_follow[id]!==false;p.editor.field_layouts[m.marker_id]=clone(p.editor.field_layouts[id]??p.layout.types.find(t=>t.type_id===m.type_id)?.fields??[]);p.layout.markers.push(m);return m;
}
export function syncFields(p:Project,m:Marker,reset=false):void {
  const type=p.layout.types.find(t=>t.type_id===m.type_id);if(!type)throw Error('类型不存在。');const old=p.editor.field_layouts[m.marker_id]??type.fields;
  const conflicts=old.filter(f=>!type.fields.some(n=>n.field_id===f.field_id&&n.field_type===f.field_type&&(n.value_type_object??'')===(f.value_type_object??'')));
  if(conflicts.length&&!reset)throw Error('字段删除或类型变化：'+conflicts.map(f=>f.field_key).join('、')+'。可修复类型，或显式升级并重置冲突字段。');
  const values:Record<string,unknown>={};for(const f of type.fields){const prev=old.find(o=>o.field_id===f.field_id&&o.field_type===f.field_type&&(o.value_type_object??'')===(f.value_type_object??''));values[f.field_key]=clone(prev&&Object.hasOwn(m.parameters,prev.field_key)?m.parameters[prev.field_key]:f.default);}
  m.parameters=values;p.editor.field_layouts[m.marker_id]=clone(type.fields);
}

export class TerrainStroke {
  readonly before:Project;visited=new Set<number>();last:[number,number]|null=null;
  constructor(readonly project:Project,readonly mode:string,readonly brush:Brush,readonly selection:[number,number,number,number]|null=null){this.before=clone(project);}
  private allowed(x:number,y:number){const s=this.selection;return !s||(x>=s[0]&&y>=s[1]&&x<=s[2]+1&&y<=s[3]+1);}
  stamp(cx:number,cy:number):void {
    const t=this.project.terrain,b=this.brush,n=Math.max(1,Math.round(b.size)),x0=cx-Math.floor(n/2),y0=cy-Math.floor(n/2),base=this.before.terrain;
    for(let y=Math.max(0,y0);y<=Math.min(t.cells_y,y0+n);y++)for(let x=Math.max(0,x0);x<=Math.min(t.cells_x,x0+n);x++){
      const i=y*(t.cells_x+1)+x;if(!this.allowed(x,y)||this.visited.has(i))continue;this.visited.add(i);const h=base.heights_cm[i];
      if(this.mode==='raise'||this.mode==='lower')t.heights_cm[i]=h+b.step*100*(this.mode==='raise'?1:-1);
      else if(this.mode==='flatten')t.heights_cm[i]=h+(b.target*100-h)*b.strength;
      else if(this.mode==='smooth'){let sum=0,count=0;for(const [dx,dy]of [[-1,0],[1,0],[0,-1],[0,1]])if(x+dx>=0&&x+dx<=t.cells_x&&y+dy>=0&&y+dy<=t.cells_y){sum+=base.heights_cm[(y+dy)*(t.cells_x+1)+x+dx];count++;}t.heights_cm[i]=h+(sum/count-h)*b.strength;}
    }
  }
  paint(cx:number,cy:number):void {
    if(this.last){const [ax,ay]=this.last,n=Math.max(Math.abs(cx-ax),Math.abs(cy-ay));for(let i=1;i<=n;i++)this.stamp(Math.round(ax+(cx-ax)*i/n),Math.round(ay+(cy-ay)*i/n));}else this.stamp(cx,cy);
    this.last=[cx,cy];this.follow();
  }
  ramp(ax:number,ay:number,bx:number,by:number):void {
    const t=this.project.terrain,b=this.brush,dx=bx-ax,dy=by-ay,d2=dx*dx+dy*dy,r=b.size/2;
    for(const i of this.visited)t.heights_cm[i]=this.before.terrain.heights_cm[i];this.visited.clear();
    for(let y=Math.max(0,Math.floor(Math.min(ay,by)-r));y<=Math.min(t.cells_y,Math.ceil(Math.max(ay,by)+r));y++)for(let x=Math.max(0,Math.floor(Math.min(ax,bx)-r));x<=Math.min(t.cells_x,Math.ceil(Math.max(ax,bx)+r));x++){
      const f=d2?Math.max(0,Math.min(1,((x-ax)*dx+(y-ay)*dy)/d2)):0;
      if(!this.allowed(x,y)||Math.max(Math.abs(x-ax-f*dx),Math.abs(y-ay-f*dy))>r+1e-8)continue;const i=y*(t.cells_x+1)+x;this.visited.add(i);t.heights_cm[i]=(b.rampStart+(b.rampEnd-b.rampStart)*f)*100;
    }this.follow();
  }
  follow():void {
    for(let k=0;k<this.project.layout.markers.length;k++){const m=this.project.layout.markers[k],old=this.before.layout.markers[k],pos=old.world_transform.position;
      if(this.project.editor.ground_follow[m.marker_id]!==false&&inside(this.project.terrain,pos[0],pos[1]))m.world_transform.position[2]=pos[2]+heightAt(this.project.terrain,pos[0],pos[1])-heightAt(this.before.terrain,pos[0],pos[1]);}
  }
}

export class DensityStroke {
  readonly before:Project;readonly base=new Map<string,number>();coverage=new Map<string,number>();last:[number,number]|null=null;private carry=0;
  constructor(readonly project:Project,readonly layerKey:string,readonly erase:boolean,readonly brush:Brush){this.before=clone(project);this.layer.cells.forEach(c=>this.base.set(`${c.cell_x},${c.cell_y}`,c.density_u8));}
  get layer(){const l=this.project.density?.layers.find(l=>l.layer_key===this.layerKey);if(!l)throw Error('资源图层不存在。');return l;}
  stamp(wx:number,wy:number):void {
    const cell=this.project.density!.cell_size_cm,b=this.brush,r=b.size*cell/2;
    for(let y=Math.floor((wy-r)/cell);y<=Math.floor((wy+r)/cell);y++)for(let x=Math.floor((wx-r)/cell);x<=Math.floor((wx+r)/cell);x++){
      const px=(x+.5)*cell,py=(y+.5)*cell;if(!inside(this.project.terrain,px,py))continue;
      const d=b.shape==='square'?Math.max(Math.abs(px-wx),Math.abs(py-wy)):Math.hypot(px-wx,py-wy);if(d>r)continue;
      const cover=b.falloff>0?Math.min(1,(r-d)/Math.max(1e-9,r*b.falloff)):1,key=`${x},${y}`;this.coverage.set(key,Math.max(this.coverage.get(key)??0,cover));
    }
  }
  paint(wx:number,wy:number):void {
    if(this.last){const [x,y]=this.last,dx=wx-x,dy=wy-y,length=Math.hypot(dx,dy),spacing=this.project.density!.cell_size_cm*.25;let distance=spacing-this.carry;
      while(distance<=length+1e-9){const f=length?distance/length:0;this.stamp(x+dx*f,y+dy*f);distance+=spacing;}this.carry=length-(distance-spacing);
    }else this.stamp(wx,wy);
    this.last=[wx,wy];this.rebuild();
  }
  finish():void {if(this.last)this.stamp(...this.last);this.rebuild();}
  private rebuild():void {const result=new Map(this.base);
    for(const [key,cover]of this.coverage){const n=Math.round(255*this.brush.strength*cover),v=Math.max(0,Math.min(255,(this.base.get(key)??0)+(this.erase?-n:n)));if(v)result.set(key,v);else result.delete(key);}
    this.layer.cells=Array.from(result,([k,density_u8])=>{const [cell_x,cell_y]=k.split(',').map(Number);return {cell_x,cell_y,density_u8};}).sort((a,b)=>a.cell_y-b.cell_y||a.cell_x-b.cell_x);
  }
}

type Delta={label:string;indices?:Int32Array;old?:Float64Array;next?:Float64Array;terrain?:[Terrain,Terrain];sections:Record<string,[any,any]>;bytes:number};
export class History {
  undoStack:Delta[]=[];redoStack:Delta[]=[];
  commit(label:string,before:Project,after:Project):boolean {
    const d:Delta={label,sections:{},bytes:0},a=before.terrain,b=after.terrain;
    if(a.cells_x!==b.cells_x||a.cells_y!==b.cells_y||a.origin_cm.some((v,i)=>v!==b.origin_cm[i])){d.terrain=[clone(a),clone(b)];d.bytes=(a.heights_cm.length+b.heights_cm.length)*8;}
    else {const ids:number[]=[],old:number[]=[],next:number[]=[];for(let i=0;i<a.heights_cm.length;i++)if(a.heights_cm[i]!==b.heights_cm[i]){ids.push(i);old.push(a.heights_cm[i]);next.push(b.heights_cm[i]);}if(ids.length){d.indices=new Int32Array(ids);d.old=new Float64Array(old);d.next=new Float64Array(next);d.bytes=ids.length*20;}}
    for(const key of ['name','map_package','layout','density','editor','source','baseline'] as const){const x=JSON.stringify(before[key]),y=JSON.stringify(after[key]);if(x!==y){d.sections[key]=[clone(before[key]),clone(after[key])];d.bytes+=(x.length+y.length)*2;}}
    if(!d.bytes)return false;this.undoStack.push(d);this.redoStack=[];let bytes=this.undoStack.reduce((n,c)=>n+c.bytes,0);while(bytes>96*1024*1024&&this.undoStack.length>1)bytes-=this.undoStack.shift()!.bytes;return true;
  }
  apply(p:Project,d:Delta,forward:boolean):void {
    if(d.terrain)p.terrain=clone(d.terrain[forward?1:0]);else if(d.indices)d.indices.forEach((v,i)=>p.terrain.heights_cm[v]=(forward?d.next!:d.old!)[i]);
    for(const [key,values]of Object.entries(d.sections))(p as any)[key]=clone(values[forward?1:0]);
  }
  undo(p:Project):string|null {const d=this.undoStack.pop();if(!d)return null;this.apply(p,d,false);this.redoStack.push(d);return d.label;}
  redo(p:Project):string|null {const d=this.redoStack.pop();if(!d)return null;this.apply(p,d,true);this.undoStack.push(d);return d.label;}
}
