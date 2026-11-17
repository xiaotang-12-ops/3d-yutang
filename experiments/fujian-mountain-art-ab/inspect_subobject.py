import unreal,json,pathlib
p=unreal.AddNewSubobjectParams()
info={'doc':p.__doc__,'fields':{n:str(getattr(p,n)) for n in dir(p) if not n.startswith('_')},'lib':unreal.SubobjectDataBlueprintFunctionLibrary.get_associated_object.__doc__}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/subobject-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
