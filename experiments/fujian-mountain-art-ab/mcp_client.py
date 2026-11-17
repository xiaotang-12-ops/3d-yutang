"""仅连接本实验8002端口；不触及校园工程8000或用户级MCP配置。-codex，2026-10-23"""
import json
import pathlib
import sys
import requests

ROOT = pathlib.Path(__file__).resolve().parent
EVIDENCE = ROOT.parents[1] / 'artifacts/fujian-mountain-art-ab'

def request(method, params):
    session = requests.Session()
    session.trust_env = False
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    url = 'http://127.0.0.1:8002/mcp'
    init = session.post(url, headers=headers, json={'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'FujianAB','version':'1'}}}, timeout=30)
    init.raise_for_status()
    headers['Mcp-Session-Id'] = init.headers['Mcp-Session-Id']
    response = session.post(url, headers=headers, json={'jsonrpc':'2.0','id':2,'method':method,'params':params}, timeout=180)
    response.raise_for_status()
    result = response.json()
    if 'error' in result:
        raise RuntimeError(result['error'])
    return result['result']

if __name__ == '__main__':
    method = sys.argv[1]
    params = json.loads(sys.argv[2]) if len(sys.argv)>2 else {}
    result = request(method, params)
    (EVIDENCE/'last-mcp.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False)[:18000])
