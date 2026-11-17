"""校验隔离副本、台账求和和实际帧时间；不修改原工程。"""
import csv, hashlib, json
from pathlib import Path
import numpy as np

root = Path(__file__).resolve().parents[2]
out = root / 'artifacts/fujian-dashboard-motion'
project = Path(__file__).resolve().parent
source = root / 'unreal/FujianSandbox58'
baseline = json.loads((out/'source-baseline.json').read_text(encoding='utf-8-sig'))
changed = [name for name, sha in baseline.items()
           if not (source/name).is_file() or hashlib.sha256((source/name).read_bytes()).hexdigest() != sha]
data = project/'Content/Data'
maps = json.loads((data/'map-data.json').read_text(encoding='utf-8-sig'))
ops = json.loads((data/'pond-operations.json').read_text(encoding='utf-8-sig'))
records = ops['sites']
ids = [s['id'] for c in maps['cities'] for s in c['sites']]
assert len(ids) == len(set(ids)) == len(records) == 152
assert set(ids) == {r['id'] for r in records}
assert len(maps['cities']) == 9 and ops['demo'] is True
assert all(len(r['monthly']) == 12 for r in records)
assert all(sum(m['harvestKg'] for m in r['monthly']) == r['annualHarvestKg'] for r in records)
assert all(sum(m['outputValueCents'] for m in r['monthly']) == r['annualOutputValueCents'] for r in records)
assert all((data/n).read_bytes() == (source/'Content/Data'/n).read_bytes()
           for n in ['map-data.json','pond-operations.json'])
result = {'source_baseline_files': len(baseline), 'source_changes': changed,
          'cities': 9, 'ponds': len(ids),
          'harvest_kg': sum(r['annualHarvestKg'] for r in records),
          'output_value_cents': sum(r['annualOutputValueCents'] for r in records),
          'area_mu': sum(r['areaMu'] for r in records),
          'data_copies_identical': True, 'monthly_totals_match': True}
assert not changed, changed
(out/'data-and-boundary-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))

if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        samples = np.array([float(r['wall_frame_ms']) for r in csv.DictReader(Path(sys.argv[1]).open(encoding='utf-8-sig'))])
        perf = {'samples':len(samples),'mean_ms':float(samples.mean()),
                'p50_ms':float(np.percentile(samples,50)), 'p95_ms':float(np.percentile(samples,95)),
                'p99_ms':float(np.percentile(samples,99)), 'max_ms':float(samples.max()),
                'average_fps':float(1000/samples.mean()),'above_16_67_ms':int((samples>16.67).sum()),
                'above_33_33_ms':int((samples>33.33).sum()), 'measurement':'Widget Tick wall interval, uncapped; no recording segment'}
        (out/'performance-audit.json').write_text(json.dumps(perf,indent=2),encoding='utf-8')
        print(json.dumps(perf))
