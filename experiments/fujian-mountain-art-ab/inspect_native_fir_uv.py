def inspect_native_fir_uv():
 import unreal,pathlib,json
 m=unreal.load_asset('/Game/ArtAB/Nature/SM_Showcase_Fir_Full')
 s=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(m,0,1)
 uvs=s[3];tris=s[1]
 picks=[]
 for i in range(0,len(tris)-2,max(3,(len(tris)//3000//3)*3)):
  v=[uvs[tris[i+j]] for j in range(3)];picks.append([sum(q.x for q in v)/3,sum(q.y for q in v)/3])
 mat=unreal.load_asset('/Game/ArtAB/Materials/M_Showcase_FirNeedles');ml=unreal.MaterialEditingLibrary
 report={'count':len(uvs),'triangles':len(tris)//3,'sampleUV':picks,'opacityNode':str(ml.get_material_property_input_node(mat,unreal.MaterialProperty.MP_OPACITY_MASK)),'colorNode':str(ml.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR))}
 pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-native-uv.json').write_text(json.dumps(report),encoding='utf-8')
inspect_native_fir_uv()
