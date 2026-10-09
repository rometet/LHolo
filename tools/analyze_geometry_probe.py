"""Read existing probe logs; no game calls. Geometry findings are not pixel causality."""
from pathlib import Path
import argparse,json,itertools,math
MARKER='PRAXIS_GEOMETRY_CAPTURE '
TOL=1e-4
def near(a,b):return all(x is not None and y is not None and abs(x-y)<=TOL for x,y in zip(a,b))
def same(a,b,uv=False):
    n=5 if uv else 3
    return any(all(near(a[i][:n],b[j][:n]) for i,j in enumerate(order)) for order in itertools.permutations(range(4)))
def face_rect(q):
    if len(q)!=4:return None
    for axis in range(3):
        plane=q[0][axis]
        if not all(abs(v[axis]-plane)<=TOL for v in q):continue
        axes=[a for a in range(3) if a!=axis]
        lo=[min(v[a] for v in q) for a in axes];hi=[max(v[a] for v in q) for a in axes]
        if any(h-l<=TOL for l,h in zip(lo,hi)):continue
        corners=[(x,y) for x in (lo[0],hi[0]) for y in (lo[1],hi[1])]
        if not all(any(near((v[axes[0]],v[axes[1]]),c) for v in q) for c in corners):continue
        return axis,plane,lo,hi
    return None
def overlap(a,b):
    x,y=face_rect(a),face_rect(b)
    if not x or not y or x[0]!=y[0] or abs(x[1]-y[1])>TOL:return 0
    lengths=[min(x[3][i],y[3][i])-max(x[2][i],y[2][i]) for i in range(2)]
    return math.prod(lengths) if all(d>TOL for d in lengths) else 0
def quads(r):
    # SDK 26.51.5 PrimitiveMode::QuadList == 1, verified by source fixture.
    if r['mode']!=1 or r['index_count'] or r['first']%4 or r['recorded']%4:return []
    result=[];o=r['origin']
    for i in range(0,r['recorded'],4):
        q=[[v[a]+o[a] if a<3 and v[a] is not None else v[a] for a in range(6)] for v in r['vertices'][i:i+4]]
        if all(c is not None and math.isfinite(c) for v in q for c in v[:3]):result.append(q)
    return result
def analyze(text):
    records=[json.loads(line.split(MARKER,1)[1]) for line in text.splitlines() if MARKER in line]
    batches={}
    for r in records:batches.setdefault(r['batch'],[]).append(r)
    findings=[]
    for batch,rs in batches.items():
        final=[q for r in rs if r['phase']=='final' for q in quads(r)]
        for cell in sorted({r['cell'] for r in rs if r['role']=='body'}):
            body=[(r,q) for r in rs if r['role']=='body' and r['cell']==cell for q in quads(r)]
            water=[q for r in rs if r['phase']=='atlas' and r['cell']==cell for q in quads(r)]
            matches=[]
            for r,b in body:
                for w in water:
                    area=overlap(b,w)
                    if area>0 or same(b,w):
                        matches.append({'body_layer':r['layer'],'coplanar_rectangle_area':area,'same_positions':same(b,w),
                            'same_positions_uv':same(b,w,True),'liquid_found_in_final':any(same(w,f,True) for f in final),
                            'body_quad':b,'liquid_quad':w})
            cellrs=[r for r in rs if r['cell']==cell and r['phase']!='final']
            findings.append({'batch':batch,'cell':cell,'body':cellrs[0]['body'] if cellrs else '',
                'body_quads':len(body),'liquid_quads':len(water),'matches':matches,
                'incomplete':any(r['recorded']!=r['total'] or not r['aligned'] or r['index_count'] or r['mode']!=1
                    or any(v is None for vertex in r['vertices'] for v in vertex[:5]) for r in cellrs)})
    return {'records':len(records),'batches':len(batches),'findings':findings,
        'scope':'Observed captured CPU streams only; same plane/UV is evidence to investigate, not shader/pixel causality',
        'final_capture_incomplete':any(r['recorded']!=r['total'] or r['index_count'] or r['mode']!=1 for r in records if r['phase']=='final')}
def main():
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=analyze(a.log.read_text(encoding='utf-8-sig',errors='replace'));a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Captured records',result['records'],'batches',result['batches'],'coplanar pairs',sum(len(c['matches']) for c in result['findings']))
if __name__=='__main__':main()
