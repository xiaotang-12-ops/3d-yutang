import sys,json,base64,pathlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from mcp_client import request
r=request('tools/call',{'name':'call_tool','arguments':{'toolset_name':'EditorToolset.EditorAppToolset','tool_name':'CaptureViewport','arguments':{'captureTransform':None,'annotations':None,'bShowUI':False}}})
p=json.loads(r['content'][0]['text'])
f=pathlib.Path(r'D:\3d-research\artifacts\fujian-mountain-art-ab')/sys.argv[1]
f.write_bytes(base64.b64decode(p['returnValue']['image']['data']))
print(str(f))
