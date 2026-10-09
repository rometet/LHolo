"""Read existing probe logs; no game calls. Geometry findings are not pixel causality."""
from pathlib import Path
import argparse,json,itertools,math
MARKER='PRAXIS_GEOMETRY_CAPTURE '
TOL=1e-4
def finite(value):
    try:return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)
    except OverflowError:return False
def near(a,b):return all(finite(x) and finite(y) and abs(x-y)<=TOL for x,y in zip(a,b))
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
def nonnegative_int(value):return type(value) is int and value>=0
def quad_uv_valid(q):return all(finite(v) for vertex in q for v in vertex[:5])
def capture_issues(r):
    issues=[]
    if r.get('mode')!=1:issues.append('unsupported_mode')
    if r.get('index_count')!=0:issues.append('indexed_or_missing_index_count')
    if not r.get('aligned'):issues.append('uv_unaligned')
    counts=[r.get(n) for n in ['first','recorded','total']]
    if not all(nonnegative_int(n) for n in counts):issues.append('invalid_counts')
    else:
        first,recorded,total=counts
        if recorded!=total:issues.append('truncated')
        if first%4 or recorded%4 or total%4:issues.append('incomplete_quad')
        if not isinstance(r.get('vertices'),list) or len(r['vertices'])!=recorded:issues.append('vertex_count_mismatch')
    origin=r.get('origin')
    if not isinstance(origin,list) or len(origin)!=3 or not all(finite(v) for v in origin):issues.append('invalid_origin')
    vertices=r.get('vertices')
    if not isinstance(vertices,list) or any(not isinstance(v,list) or len(v)!=6 for v in vertices):issues.append('invalid_vertex_layout')
    elif any(not finite(v) for vertex in vertices for v in vertex[:5]):issues.append('nonfinite_position_or_uv')
    elif 'invalid_origin' not in issues and any(not finite(v[a]+origin[a]) for v in vertices for a in range(3)):issues.append('nonfinite_world_position')
    # Native colors are optional and do not participate in the position/UV query.
    return issues
def final_capture_status(records):
    finals=[r for r in records if r['phase']=='final']
    issues=[]
    if not finals:issues.append('not_captured')
    if len(finals)>1:issues.append('multiple_final_records')
    for r in finals:
        if r.get('role')!='liquid':issues.append('invalid_final_role')
        if r.get('first')!=0:issues.append('final_suffix_only')
        issues.extend(capture_issues(r))
    return {'status':'MISSING' if not finals else 'INCOMPLETE' if issues else 'COMPLETE',
        'records':len(finals),'complete':bool(finals) and not issues,'issues':sorted(set(issues))}
def quads(r):
    # SDK 26.51.5 PrimitiveMode::QuadList == 1, verified by source fixture.
    if r.get('mode')!=1 or r.get('index_count')!=0:return []
    if not all(nonnegative_int(r.get(n)) for n in ['first','recorded']) or r['first']%4 or r['recorded']%4:return []
    result=[];o=r.get('origin')
    if not isinstance(o,list) or len(o)!=3 or not all(finite(v) for v in o):return []
    if not isinstance(r.get('vertices'),list) or len(r['vertices'])!=r['recorded']:return []
    for i in range(0,r['recorded'],4):
        if any(not isinstance(v,list) or len(v)!=6 for v in r['vertices'][i:i+4]):continue
        if not all(finite(c) for v in r['vertices'][i:i+4] for c in v[:3]):continue
        q=[[v[a]+o[a] if a<3 and v[a] is not None else v[a] for a in range(6)] for v in r['vertices'][i:i+4]]
        if all(finite(c) for v in q for c in v[:3]):result.append(q)
    return result
def analyze(text):
    records=[json.loads(line.split(MARKER,1)[1]) for line in text.splitlines() if MARKER in line]
    batches={}
    for r in records:batches.setdefault(r['batch'],[]).append(r)
    findings=[];final_statuses=[]
    for batch,rs in batches.items():
        final_status=final_capture_status(rs);final_statuses.append(dict(batch=batch,**final_status))
        final=[q for r in rs if r['phase']=='final' and r.get('role')=='liquid' and r.get('aligned')
            for q in quads(r) if quad_uv_valid(q)]
        for cell in sorted({r['cell'] for r in rs if r['role']=='body'}):
            body=[(r,q) for r in rs if r['role']=='body' and r['cell']==cell for q in quads(r)]
            water=[(r,q) for r in rs if r['phase']=='atlas' and r['cell']==cell for q in quads(r)]
            matches=[]
            for r,b in body:
                for wr,w in water:
                    area=overlap(b,w)
                    if area>0 or same(b,w):
                        query_valid=bool(wr.get('aligned')) and quad_uv_valid(w)
                        found=query_valid and any(same(w,f,True) for f in final)
                        found_in_final=True if found else False if final_status['complete'] and query_valid else None
                        uv_valid=bool(r.get('aligned')) and quad_uv_valid(b) and query_valid
                        matches.append({'body_layer':r['layer'],'coplanar_rectangle_area':area,'same_positions':same(b,w),
                            'same_positions_uv':same(b,w,True) if uv_valid else None,'liquid_found_in_final':found_in_final,
                            'body_quad':b,'liquid_quad':w})
            cellrs=[r for r in rs if r['cell']==cell and r['phase']!='final']
            findings.append({'batch':batch,'cell':cell,'body':cellrs[0]['body'] if cellrs else '',
                'body_quads':len(body),'liquid_quads':len(water),'matches':matches,
                'final_capture_status':final_status['status'],
                'incomplete':not final_status['complete'] or any(capture_issues(r) for r in cellrs)})
    return {'records':len(records),'batches':len(batches),'findings':findings,
        'scope':'Observed captured CPU streams only; same plane/UV is evidence to investigate, not shader/pixel causality',
        'final_capture_by_batch':final_statuses,
        'final_capture_incomplete':not final_statuses or any(not s['complete'] for s in final_statuses),
        'unknown_encoding':'null means Unknown; false requires a complete final capture and a valid position/UV query'}
def main():
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=analyze(a.log.read_text(encoding='utf-8-sig',errors='replace'));a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Captured records',result['records'],'batches',result['batches'],'coplanar pairs',sum(len(c['matches']) for c in result['findings']))
if __name__=='__main__':main()
