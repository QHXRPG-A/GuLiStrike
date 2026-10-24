import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { Project, Marker, Vec3 } from './types';
import { heightAt, outline, worldPoint } from './model';

/** UE XY -> preview X,-Z, and UE Z -> preview Y. Source arrays stay in cm. */
export class Preview {
  private renderer:THREE.WebGLRenderer|null=null;private scene=new THREE.Scene();private camera=new THREE.PerspectiveCamera(45,1,.05,100000);
  private controls:OrbitControls|null=null;private mesh:THREE.Mesh|null=null;private grid:THREE.LineSegments|null=null;private overlays=new THREE.Group();
  private drawPending=false;private lastId='';private resize:ResizeObserver;private showGrid=true;
  constructor(readonly host:HTMLElement){
    this.scene.background=new THREE.Color('#021716');
    try{this.renderer=new THREE.WebGLRenderer({antialias:true});this.renderer.setPixelRatio(Math.min(devicePixelRatio,1.7));host.append(this.renderer.domElement);
      this.controls=new OrbitControls(this.camera,this.renderer.domElement);this.controls.enableDamping=false;this.controls.addEventListener('change',()=>this.render());
      this.resize=new ResizeObserver(()=>{const {width,height}=host.getBoundingClientRect();if(width&&height){this.renderer!.setSize(width,height);this.camera.aspect=width/height;this.camera.updateProjectionMatrix();this.render();}});this.resize.observe(host);
    }catch(e){host.innerHTML='<div class="empty">此浏览器无法启动WebGL三维预览。<br>二维网格仍可正常编辑。</div>';this.resize=new ResizeObserver(()=>{});}
    this.scene.add(new THREE.HemisphereLight(0xfee4d9,0x032e30,2));const light=new THREE.DirectionalLight(0xfee4d9,2.4);light.position.set(-1000,2000,1000);this.scene.add(light);this.scene.add(this.overlays);
  }
  private render(){if(this.renderer)this.renderer.render(this.scene,this.camera);}
  toggleGrid(){this.showGrid=!this.showGrid;if(this.grid)this.grid.visible=this.showGrid;this.render();}
  private dispose(object:THREE.Object3D){object.traverse(o=>{const obj=o as THREE.Mesh;if(obj.geometry)obj.geometry.dispose();if(obj.material){for(const m of Array.isArray(obj.material)?obj.material:[obj.material])m.dispose();}});object.removeFromParent();}
  update(p:Project,selected:string,visibility:Record<string,boolean>){
    if(!this.renderer)return;const t=p.terrain,nx=t.cells_x,ny=t.cells_y;
    // Full precision data is never decimated; only the preview uses a bounded LOD.
    const step=Math.max(1,Math.ceil(Math.max(nx,ny)/700)),xs:number[]=[],ys:number[]=[];
    for(let x=0;x<nx;x+=step)xs.push(x);xs.push(nx);for(let y=0;y<ny;y+=step)ys.push(y);ys.push(ny);
    const cx=t.origin_cm[0]+nx*150,cy=t.origin_cm[1]+ny*150,positions=new Float32Array(xs.length*ys.length*3),colors=new Float32Array(positions.length);
    let minimum=Infinity,maximum=-Infinity;for(const h of t.heights_cm){minimum=Math.min(minimum,h);maximum=Math.max(maximum,h);}const color=new THREE.Color(),low=new THREE.Color('#274E61'),mid=new THREE.Color('#557B78'),high=new THREE.Color('#D5C09C');
    let n=0;for(const y of ys)for(const x of xs){const z=t.heights_cm[y*(nx+1)+x],v=(z-minimum)/Math.max(1,maximum-minimum);positions[n]=(t.origin_cm[0]+x*300-cx)/100;positions[n+1]=z/100;positions[n+2]=-(t.origin_cm[1]+y*300-cy)/100;color.copy(v<.55?low:mid).lerp(v<.55?mid:high,v<.55?v/.55:(v-.55)/.45);colors[n++]=color.r;colors[n++]=color.g;colors[n++]=color.b;}
    const indices:number[]=[];for(let y=0;y<ys.length-1;y++)for(let x=0;x<xs.length-1;x++){const a=y*xs.length+x,b=a+1,c=a+xs.length,d=c+1;indices.push(a,b,d,a,d,c);}
    if(this.mesh)this.dispose(this.mesh);if(this.grid)this.dispose(this.grid);
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.BufferAttribute(positions,3));geometry.setAttribute('color',new THREE.BufferAttribute(colors,3));geometry.setIndex(indices);geometry.computeVertexNormals();
    this.mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({vertexColors:true,roughness:1,metalness:0,side:THREE.DoubleSide,flatShading:true}));this.scene.add(this.mesh);
    // Square cell edges, rather than triangle diagonals, express the editing grid.
    const lines:number[]=[];const lineStep=Math.max(step,Math.ceil(Math.max(nx,ny)/180));
    const point=(x:number,y:number)=>[(t.origin_cm[0]+x*300-cx)/100,t.heights_cm[y*(nx+1)+x]/100+.06,-(t.origin_cm[1]+y*300-cy)/100];
    for(let x=0;x<=nx;x+=lineStep)for(let y=0;y<ny;y+=step)lines.push(...point(x,y),...point(x,Math.min(ny,y+step)));
    for(let y=0;y<=ny;y+=lineStep)for(let x=0;x<nx;x+=step)lines.push(...point(x,y),...point(Math.min(nx,x+step),y));
    const lg=new THREE.BufferGeometry();lg.setAttribute('position',new THREE.Float32BufferAttribute(lines,3));this.grid=new THREE.LineSegments(lg,new THREE.LineBasicMaterial({color:'#D5C09C',transparent:true,opacity:.16}));this.grid.visible=this.showGrid;this.scene.add(this.grid);
    for(const child of [...this.overlays.children])this.dispose(child);
    const convert=(v:Vec3)=>new THREE.Vector3((v[0]-cx)/100,v[2]/100+.4,-(v[1]-cy)/100);
    const addLine=(points:Vec3[],hex:string,closed=true)=>{const values=points.map(convert);if(closed&&values.length)values.push(values[0]);const g=new THREE.BufferGeometry().setFromPoints(values);this.overlays.add(new THREE.Line(g,new THREE.LineBasicMaterial({color:hex,transparent:true,opacity:.85,depthTest:false})));};
    if(visibility.markers!==false)for(const m of p.layout.markers){if(!m.enabled)continue;const v=m.world_transform.position;
      const dot=new THREE.Mesh(new THREE.SphereGeometry(Math.max(1.5,nx*3/500),8,6),new THREE.MeshBasicMaterial({color:m.marker_id===selected?'#EE9D58':'#FEE4D9',depthTest:false}));dot.position.copy(convert(v));this.overlays.add(dot);
      if(visibility.regions!==false)for(const r of m.regions){if(!r.enabled)continue;const shape=outline(r).map(v=>worldPoint(m,r,v));
        if(r.shape_type==='Sphere'){
          addLine(shape,m.marker_id===selected?'#EE9D58':'#8EAAA4');continue;
        }
        // Ground-projected footprint and actual vertical extent are both available.
        addLine(shape.map(v=>[v[0],v[1],heightAt(t,v[0],v[1])]),m.marker_id===selected?'#EE9D58':'#6AA4BE');
        if(m.marker_id===selected){const g=r.geometry,z0=r.shape_type==='Box'?-(g.half_extents?.[2]??0):g.min_z??0,z1=r.shape_type==='Box'?(g.half_extents?.[2]??0):g.max_z??0;
          addLine(outline(r).map(v=>worldPoint(m,r,[v[0],v[1],z0])),'#D5C09C');addLine(outline(r).map(v=>worldPoint(m,r,[v[0],v[1],z1])),'#D5C09C');}
      }
    }
    if(p.density)for(const layer of p.density.layers){if(visibility[layer.layer_key]===false)continue;const c=p.density.cell_size_cm;
      const data:number[]=[],cs:number[]=[];const col=new THREE.Color().setRGB(...layer.color_rgb);const stride=Math.max(1,Math.ceil(layer.cells.length/180000));
      for(let i=0;i<layer.cells.length;i+=stride){const cell=layer.cells[i],x=(cell.cell_x+.5)*c,y=(cell.cell_y+.5)*c;data.push((x-cx)/100,heightAt(t,x,y)/100+.8,-(y-cy)/100);cs.push(col.r,col.g,col.b);}
      const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(data,3));g.setAttribute('color',new THREE.Float32BufferAttribute(cs,3));this.overlays.add(new THREE.Points(g,new THREE.PointsMaterial({vertexColors:true,size:Math.max(2,c/100*.7),transparent:true,opacity:.5,depthWrite:false})));
    }
    if(this.lastId!==p.id){this.lastId=p.id;this.fit(p);}this.render();
  }
  fit(p:Project){const width=Math.max(p.terrain.cells_x,p.terrain.cells_y)*3;let sum=0;for(const z of p.terrain.heights_cm)sum+=z;const z=sum/p.terrain.heights_cm.length/100;
    this.camera.position.set(width*.6,z+width*.85,width*.8);this.camera.far=Math.max(100000,width*20);this.camera.updateProjectionMatrix();this.controls?.target.set(0,z,0);this.controls?.update();this.render();}
}
