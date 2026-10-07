import unreal,json
texture=unreal.load_asset('/Game/GuLiStrike/Commander/LODReview_20261005/BiZhiMao/Textures/T_BiZhiMao_VertexPosition_LOD0')
names=[n for n in dir(texture) if any(w in n for w in ['size','force','compile','update','source'])]
result=dict(methods={n:str(getattr(texture,n).__doc__) for n in names},classes=[n for n in dir(unreal) if 'Compil' in n or 'TextureService' in n])
unreal.MCPythonHelper.submit_result(json.dumps(result))
