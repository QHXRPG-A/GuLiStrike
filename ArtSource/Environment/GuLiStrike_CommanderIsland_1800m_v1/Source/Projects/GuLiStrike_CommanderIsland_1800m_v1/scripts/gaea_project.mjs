import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const name='GuLiStrike_CommanderIsland_1800m_v1';
const filename=path.join(root,name+'.terrain');
const mcpRoot=path.join(process.env.USERPROFILE,'.codex','mcp','gaea-mcp');
const require=createRequire(path.join(mcpRoot,'package.json'));
const {Client}=await import(pathToFileURL(require.resolve('@modelcontextprotocol/sdk/client/index.js')));
const {StdioClientTransport}=await import(pathToFileURL(require.resolve('@modelcontextprotocol/sdk/client/stdio.js')));
const {readTerrain,writeTerrain}=await import(pathToFileURL(path.join(mcpRoot,'dist','terrain-io.js')));
const stage=process.argv[2]??'draft';
if(!['draft','final'].includes(stage))throw Error('Stage must be draft or final');
const resolution=stage==='draft'?1024:4096;
const destination=path.resolve(root,'..','..','Builds',name,stage);
fs.mkdirSync(destination,{recursive:true});
const transport=new StdioClientTransport({command:process.execPath,args:[path.join(mcpRoot,'dist','server.js')],cwd:mcpRoot,env:{...process.env,GAEA_INSTALL_DIR:path.join(process.env.LOCALAPPDATA,'Programs','Gaea 2.0'),PROJECT_DIR:path.resolve(root,'..'),OUTPUT_DIR:path.resolve(destination,'..'),DOTENV_CONFIG_QUIET:'true'},stderr:'pipe'});
transport.stderr?.on('data',chunk=>process.stderr.write(chunk));
const client=new Client({name:'gulistrike-island-author',version:'1.0.0'});
async function call(tool,args={}){
  const result=await client.callTool({name:tool,arguments:args},undefined,{timeout:300000});
  const text=result.content.filter(c=>c.type==='text').map(c=>c.text).join('\n');
  if(result.isError)throw Error(tool+': '+text);
  const value=JSON.parse(text);
  if(value.status==='error')throw Error(tool+': '+text);
  return value;
}
const nodes={},exports=[];
async function add(key,type,label,column,row,properties={}){
  const node=await call('add_node',{filename,nodeType:type,name:label,x:25500+column*370,y:24600+row*260,properties});
  nodes[key]=node.nodeId;
  return node.nodeId;
}
async function wire(a,b,toPort='In',fromPort='Out'){
  await call('connect_nodes',{filename,fromNodeId:nodes[a],toNodeId:nodes[b],fromPort,toPort});
}
async function file(key,input,label,column,row){return add(key,'File',label,column,row,{FileName:'inputs/'+input+'.png',RelativePath:true});}
async function combine(key,mode,a,b,label,column,row){
  await add(key,'Combine',label,column,row,{Mode:mode,PortCount:2,Ratio:1.0});
  await wire(a,key);await wire(b,key,'Input2');
}
async function output(key,input,label,format,column,row){
  await add(key,'Export',label,column,row,{Format:format,Location:'Explicit',OutputPath:path.join(destination,key).replaceAll('\\','/')});
  await wire(input,key);exports.push(key);
}
try{
  await client.connect(transport);
  if(!fs.existsSync(filename))await call('create_terrain',{name:name+'/'+name});
  let graph=await call('read_terrain_graph',{filename});
  if(graph.nodeCount===0){
    await file('foundation','foundation_height','01 海岸与地形布局 / 1800m',0,3);
    await add('mountain','Mountain','02 西北山脊 / Seed 20261001',0,0,{Seed:20261001,Height:1.0});
    await file('mountain_gain','mountain_gain','西北山地高度与区域遮罩',0,1);
    await combine('mountain_local','Multiply','mountain','mountain_gain','山脊区域化 / 最大增高24m',1,0);
    await add('perlin','Perlin','03 平原与低丘微地形',0,5,{Seed:20261002});
    await file('hills_gain','hills_gain','低丘细节高度遮罩',0,6);
    await combine('hills_local','Multiply','perlin','hills_gain','低丘区域化 / 1.1-4.2m',1,5);
    await add('plates','Plates','04 东侧高台表面',0,8,{Seed:20261003});
    await file('plateau_gain','plateau_gain','高台细节高度遮罩',0,9);
    await combine('plates_local','Multiply','plates','plateau_gain','高台区域化 / 3m',1,8);
    await add('canyon','Canyon','05 东侧峡谷侵蚀纹理',0,11,{Seed:20261004,Valley:0.58,Depth:0.7});
    await file('canyon_gain','canyon_gain','峡谷下切高度遮罩',0,12);
    await combine('canyon_local','Multiply','canyon','canyon_gain','峡谷区域化 / 2.5m',1,11);
    await combine('compose_mountain','Add','foundation','mountain_local','06 山地合成',2,3);
    await combine('compose_hills','Add','compose_mountain','hills_local','07 丘陵合成',3,3);
    await combine('compose_plateau','Add','compose_hills','plates_local','08 高台合成',4,3);
    await combine('compose_canyon','Subtract','compose_plateau','canyon_local','09 峡谷合成',5,3);
    await file('erosion_mask','erosion_allowed','通路与集结区侵蚀保护',5,5);
    await add('erosion','Erosion2','10 局部侵蚀 / 通路保护',6,3,{Version:2,Seed:20261001,Duration:12.0,Downcutting:0.05});
    await wire('compose_canyon','erosion');await wire('erosion_mask','erosion','Mask');
    await file('unprotected','unprotected','可变化区域',6,6);
    await file('protection','protection','稳定通路 / 集结区 / 海岸',6,8);
    await combine('eroded_unprotected','Multiply','erosion','unprotected','11 非保护区域地形',7,3);
    await combine('foundation_protected','Multiply','foundation','protection','12 保留路线与海岸',7,7);
    await combine('final_terrain','Add','eroded_unprotected','foundation_protected','13 最终战斗海岛',8,3);
    await output('height_4096','final_terrain','14 高度图 / PNG16','PNG16',9,2);
    await add('satmap','SatMap','15 地形色彩预览',9,4,{LibraryItem:81,Bias:0.2584,Reverse:true});
    await wire('final_terrain','satmap');
    await output('gaea_color','satmap','16 色彩图 / PNG8','PNG8',10,4);
    // These masks are regenerated from the built heightfield by analyze_exports.py.
    for(const [i,key] of ['flat_ground','hills','plateau','rock','beach','water'].entries()){
      await file('class_'+key,'mask_'+key,'材质分区 / '+key,8,7+i);
      await output('mask_'+key,'class_'+key,'分区导出 / '+key,'PNG16',9,7+i);
    }
    graph=await call('read_terrain_graph',{filename});
    fs.writeFileSync(path.join(root,'graph-manifest.json'),JSON.stringify({nodes,exports,graph},null,2));
  }
  const tf=readTerrain(filename);
  tf.terrain.Width=1800;tf.terrain.Height=140;tf.terrain.Ratio=140/1800;
  tf.terrain.Metadata.Name=name;
  tf.terrain.Metadata.Description='1800m x 1800m; world height = normalized*140-20m; sea level 0m; north +Y; natural asymmetric connected combat island.';
  tf.raw.Metadata.Name=name;tf.raw.Metadata.ModifiedVersion='2.3.0.1';
  tf.asset.BuildDefinition.Resolution=resolution;
  tf.asset.BuildDefinition.BakeResolution=resolution;
  tf.asset.BuildDefinition.Destination=destination.replaceAll('\\','/');
  tf.asset.BuildDefinition.ColorSpace='sRGB';
  tf.asset.State.PreviewResolution=1024;tf.asset.State.HDResolution=4096;
  tf.asset.State.BakeResolution=resolution;
  const index=fs.existsSync(path.join(root,'graph-manifest.json'))?JSON.parse(fs.readFileSync(path.join(root,'graph-manifest.json'),'utf8')):null;
  if(index)tf.asset.State.SelectedNode=index.nodes.final_terrain;
  for(const node of Object.values(tf.nodes)){
    if(node&&typeof node==='object'&&node.$type?.startsWith('QuadSpinner.Gaea.Nodes.Combine,')){
      // Gaea evaluates optional inputs present in the serialized port list.
      // A two-input Multiply must not contain empty Input3/Input4 ports.
      node.Ports.$values=node.Ports.$values.filter(p=>['In','Out','Input2','Mask'].includes(p.Name));
    }
    if(node&&typeof node==='object'&&node.$type?.startsWith('QuadSpinner.Gaea.Nodes.Export,')){
      const old=path.basename(node.OutputPath??'export');
      node.OutputPath=path.join(destination,old).replaceAll('\\','/');
    }
  }
  writeTerrain(tf);
  const summary=await call('read_terrain_graph',{filename});
  if(index){
    index.graph=summary;index.lastConfiguredStage=stage;
    fs.writeFileSync(path.join(root,'graph-manifest.json'),JSON.stringify(index,null,2));
  }
  console.log(JSON.stringify({success:true,stage,resolution,nodeCount:summary.nodeCount,connections:summary.connections.length,filename,destination}));
  if(process.argv.includes('--build')){
    const result=await call('build_terrain',{filename,ignoreCache:true,verbose:true});
    fs.writeFileSync(path.join(destination,'mcp-build-result.json'),JSON.stringify(result,null,2));
    fs.writeFileSync(path.join(destination,'build.log'),result.log??'');
    const report=JSON.parse(fs.readFileSync(path.join(destination,'report.json'),'utf8'));
    if(report.Result!=='Success')throw Error('Build report: '+report.Result);
    console.log(JSON.stringify({success:true,stage,resolution:report.Resolution,buildResult:report.Result,duration:report.Duration}));
  }
}finally{await client.close();}
