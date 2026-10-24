import { Brush, Marker, Mode, Project, Vec3 } from './types';
import { heightAt, outline, polygonError, worldPoint } from './model';

export class MapCanvas {
  readonly canvas=document.createElement('canvas');readonly ctx:CanvasRenderingContext2D;
  zoom=1;panX=0;panY=0;project:Project|null=null;selected='';region='';visibility:Record<string,boolean>={};mode:Mode='select';brush!:Brush;layerKey='BlueOre';
  mouse:[number,number]|null=null;draft:Vec3[]=[];private image:HTMLCanvasElement|null=null;private min=0;private max=1;private w=0;private h=0;
  selection:[number,number,number,number]|null=null;
  constructor(readonly host:HTMLElement){host.append(this.canvas);this.ctx=this.canvas.getContext('2d')!;new ResizeObserver(()=>this.resize()).observe(host);}
  resize(){const box=this.host.getBoundingClientRect();this.w=box.width;this.h=box.height;this.canvas.width=Math.max(1,Math.round(box.width*devicePixelRatio));this.canvas.height=Math.max(1,Math.round(box.height*devicePixelRatio));this.draw();}
  toScreen(x:number,y:number):[number,number]{return [this.w/2+this.panX+x*this.zoom,this.h/2+this.panY-y*this.zoom];}
  toWorld(x:number,y:number):[number,number]{return [(x-this.w/2-this.panX)/this.zoom,-(y-this.h/2-this.panY)/this.zoom];}
  eventWorld(event:PointerEvent|WheelEvent):[number,number]{const r=this.canvas.getBoundingClientRect();return this.toWorld(event.clientX-r.left,event.clientY-r.top);}
  fit(){if(!this.project)return;const t=this.project.terrain;this.zoom=Math.max(.00001,Math.min((this.w-100)/(t.cells_x*300),(this.h-100)/(t.cells_y*300)));this.panX=-(t.origin_cm[0]+t.cells_x*150)*this.zoom;this.panY=(t.origin_cm[1]+t.cells_y*150)*this.zoom;this.draw();}
  terrainChanged(){const p=this.project;if(!p)return;const t=p.terrain,w=t.cells_x,h=t.cells_y,scale=Math.max(1,Math.ceil(Math.max(w,h)/1600)),iw=Math.ceil(w/scale),ih=Math.ceil(h/scale);
    this.image=document.createElement('canvas');this.image.width=iw;this.image.height=ih;const c=this.image.getContext('2d')!,img=c.createImageData(iw,ih);this.min=Infinity;this.max=-Infinity;
    for(const v of t.heights_cm){this.min=Math.min(this.min,v);this.max=Math.max(this.max,v);}const range=Math.max(100,this.max-this.min);
    const low=[39,78,97],mid=[85,123,120],high=[213,192,156];
    for(let y=0;y<ih;y++)for(let x=0;x<iw;x++){const xx=Math.min(w-1,x*scale),yy=Math.min(h-1,(ih-1-y)*scale),i=yy*(w+1)+xx,z=(t.heights_cm[i]+t.heights_cm[i+w+2])/2,v=(z-this.min)/range,f=v<.55?v/.55:(v-.55)/.45,a=v<.55?low:mid,b=v<.55?mid:high,k=(y*iw+x)*4;
      for(let n=0;n<3;n++)img.data[k+n]=Math.round(a[n]+(b[n]-a[n])*f);img.data[k+3]=255;}
    c.putImageData(img,0,0);this.draw();
  }
  private path(points:Vec3[]){const c=this.ctx;c.beginPath();points.forEach((v,i)=>{const s=this.toScreen(v[0],v[1]);i?c.lineTo(...s):c.moveTo(...s);});c.closePath();}
  draw(){const c=this.ctx,dpr=devicePixelRatio;c.setTransform(dpr,0,0,dpr,0,0);c.clearRect(0,0,this.w,this.h);c.fillStyle='#021716';c.fillRect(0,0,this.w,this.h);if(!this.project)return;
    const p=this.project,t=p.terrain,[left,top]=this.toScreen(t.origin_cm[0],t.origin_cm[1]+t.cells_y*300),width=t.cells_x*300*this.zoom,height=t.cells_y*300*this.zoom;
    if(this.visibility.terrain!==false&&this.image){c.imageSmoothingEnabled=false;c.drawImage(this.image,left,top,width,height);}else{c.fillStyle='#183531';c.fillRect(left,top,width,height);}
    const [minX,maxY]=this.toWorld(0,0),[maxX,minY]=this.toWorld(this.w,this.h);
    if(p.density)for(const layer of p.density.layers){if(this.visibility[layer.layer_key]===false)continue;const size=p.density.cell_size_cm,col=layer.color_rgb.map(v=>Math.round(255*(v<=.0031308?v*12.92:1.055*v**(1/2.4)-.055)));
      for(const cell of layer.cells){const x=cell.cell_x*size,y=cell.cell_y*size;if(x+size<minX||x>maxX||y+size<minY||y>maxY)continue;const [sx,sy]=this.toScreen(x,y+size);c.fillStyle=`rgba(${col.join(',')},${.07+.65*cell.density_u8/255})`;c.fillRect(sx,sy,Math.max(.8,size*this.zoom),Math.max(.8,size*this.zoom));}
    }
    if(this.visibility.grid!==false){const spacing=300*this.zoom,step=spacing>=7?1:Math.max(1,Math.ceil(20/spacing));c.lineWidth=1;c.strokeStyle='rgba(254,228,217,.13)';c.beginPath();
      const x0=Math.max(0,Math.floor((minX-t.origin_cm[0])/300/step)*step),x1=Math.min(t.cells_x,Math.ceil((maxX-t.origin_cm[0])/300));
      const y0=Math.max(0,Math.floor((minY-t.origin_cm[1])/300/step)*step),y1=Math.min(t.cells_y,Math.ceil((maxY-t.origin_cm[1])/300));
      for(let x=x0;x<=x1;x+=step){const sx=this.toScreen(t.origin_cm[0]+x*300,0)[0];c.moveTo(sx,top);c.lineTo(sx,top+height);}for(let y=y0;y<=y1;y+=step){const sy=this.toScreen(0,t.origin_cm[1]+y*300)[1];c.moveTo(left,sy);c.lineTo(left+width,sy);}c.stroke();
    }
    c.strokeStyle='#D5C09C';c.lineWidth=1;c.strokeRect(left,top,width,height);
    if(this.visibility.regions!==false)for(const m of p.layout.markers)for(const r of m.regions){if(!r.enabled||!m.enabled)continue;const points=outline(r).map(v=>worldPoint(m,r,v));this.path(points);const selected=m.marker_id===this.selected;
      const invalid=selected&&r.shape_type==='PolygonPrism'&&polygonError(r.geometry.vertices!)!==null;
      c.fillStyle=selected?'rgba(238,157,88,.06)':'rgba(106,164,190,.025)';if(!invalid)c.fill();c.strokeStyle=invalid?'#A34053':selected?'#EE9D58':'rgba(106,164,190,.5)';c.lineWidth=selected?1.5:1;c.stroke();
      if(selected&&r.region_id===this.region&&r.shape_type==='PolygonPrism')for(const v of points){const [x,y]=this.toScreen(v[0],v[1]);c.fillStyle='#FEE4D9';c.fillRect(x-3,y-3,6,6);}
    }
    if(this.visibility.markers!==false){c.font='11px "Segoe UI","Microsoft YaHei",sans-serif';c.textBaseline='middle';
      for(const m of p.layout.markers){const [x,y]=this.toScreen(...m.world_transform.position.slice(0,2) as [number,number]);if(x< -180||x>this.w+20||y< -20||y>this.h+20)continue;
        c.globalAlpha=m.enabled?1:.35;c.fillStyle=m.marker_id===this.selected?'#EE9D58':'#FEE4D9';c.beginPath();c.moveTo(x,y-5);c.lineTo(x+5,y);c.lineTo(x,y+5);c.lineTo(x-5,y);c.closePath();c.fill();
        if(m.marker_id===this.selected||this.zoom>.003){c.fillStyle='#021716';c.fillRect(x+8,y-9,c.measureText(m.display_name||m.marker_key).width+8,18);c.fillStyle='#FEE4D9';c.fillText(m.display_name||m.marker_key,x+12,y);}c.globalAlpha=1;
      }
    }
    if(this.selection){const [x0,y0,x1,y1]=this.selection,[sx,sy]=this.toScreen(t.origin_cm[0]+x0*300,t.origin_cm[1]+(y1+1)*300);c.fillStyle='rgba(238,157,88,.12)';c.strokeStyle='#EE9D58';c.lineWidth=2;c.setLineDash([6,4]);c.fillRect(sx,sy,(x1-x0+1)*300*this.zoom,(y1-y0+1)*300*this.zoom);c.strokeRect(sx,sy,(x1-x0+1)*300*this.zoom,(y1-y0+1)*300*this.zoom);c.setLineDash([]);}
    if(this.mouse&&this.brush&&!['select','place','polygon','area'].includes(this.mode)){const [x,y]=this.mouse,resource=this.mode==='density'||this.mode==='erase',cell=resource?p.density?.cell_size_cm??300:300,n=this.brush.size;
      const cx=resource?Math.floor(x/cell):Math.floor((x-t.origin_cm[0])/300),cy=resource?Math.floor(y/cell):Math.floor((y-t.origin_cm[1])/300);
      const ox=resource?0:t.origin_cm[0],oy=resource?0:t.origin_cm[1],x0=(cx-Math.floor(n/2))*cell+ox,y0=(cy-Math.floor(n/2))*cell+oy,[sx,sy]=this.toScreen(x0,y0+n*cell),pixels=n*cell*this.zoom;
      c.strokeStyle='#FEE4D9';c.lineWidth=1.5;c.fillStyle='rgba(254,228,217,.10)';c.beginPath();if(resource&&this.brush.shape==='circle')c.arc(sx+pixels/2,sy+pixels/2,pixels/2,0,Math.PI*2);else c.rect(sx,sy,pixels,pixels);c.fill();c.stroke();
    }
    if(this.draft.length){this.path(this.draft);c.strokeStyle='#EE9D58';c.lineWidth=2;c.stroke();for(const v of this.draft){const [x,y]=this.toScreen(v[0],v[1]);c.fillStyle='#FEE4D9';c.fillRect(x-3,y-3,6,6);}}
    c.fillStyle='rgba(2,23,22,.85)';c.fillRect(18,this.h-40,180,24);c.fillStyle='#D5C09C';c.font='11px "Segoe UI",sans-serif';const meters=100/this.zoom/100;c.fillText(`比例尺 100px ≈ ${meters.toFixed(meters<10?1:0)}m`,28,this.h-24);
    c.fillStyle='rgba(2,23,22,.85)';c.fillRect(this.w-135,15,118,46);c.fillStyle='#D5C09C';c.fillText(`高 ${(this.max/100).toFixed(2)}m`,this.w-124,33);c.fillText(`低 ${(this.min/100).toFixed(2)}m`,this.w-124,49);
  }
  pickMarker(wx:number,wy:number):Marker|undefined {if(!this.project)return;return [...this.project.layout.markers].reverse().find(m=>Math.hypot(m.world_transform.position[0]-wx,m.world_transform.position[1]-wy)*this.zoom<12);}
  pickVertex(wx:number,wy:number):{marker:Marker;regionId:string;index:number}|null {const m=this.project?.layout.markers.find(m=>m.marker_id===this.selected);if(!m)return null;
    for(const r of m.regions){if(r.region_id!==this.region||r.shape_type!=='PolygonPrism')continue;for(let i=0;i<(r.geometry.vertices?.length??0);i++){const v=r.geometry.vertices![i],w=worldPoint(m,r,[...v,0]);if(Math.hypot(w[0]-wx,w[1]-wy)*this.zoom<9)return {marker:m,regionId:r.region_id,index:i};}}return null;}
}
