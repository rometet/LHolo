"""Synthetic parser inputs only; no DLL build/load, game calls, or native pixels."""
from pathlib import Path
import copy,json,sys,unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from analyze_geometry_probe import MARKER,analyze

def record(role,phase,batch=1):
    return {'batch':batch,'generation':9,'section':0,'cell':0,'role':role,'phase':phase,
        'body':'fixture:stairs','liquid':'minecraft:water','layer':5 if role=='body' else 3,
        'mode':1,'index_count':0,'first':0,'total':4,'recorded':4,'aligned':1,'color_aligned':1,
        'origin':[0,0,0],'vertices':[[10,2,10,.1,.2,0],[11,2,10,.2,.2,0],
            [11,2,11,.2,.3,0],[10,2,11,.1,.3,0]]}
def inputs(batch=1):return [record('body','native',batch),record('liquid','atlas',batch)]
def report(records):return analyze('\n'.join(MARKER+json.dumps(r) for r in records))
def found(result,cell=0,batch=1):
    return next(c for c in result['findings'] if c['cell']==cell and c['batch']==batch)['matches'][0]['liquid_found_in_final']
def absent_final():
    final=record('liquid','final')
    for v in final['vertices']:v[1]+=1
    return final

class CompletenessTests(unittest.TestCase):
    def test_missing_final_is_unknown(self):
        result=report(inputs());self.assertIsNone(found(result));self.assertTrue(result['final_capture_incomplete'])
        self.assertEqual(result['final_capture_by_batch'][0]['status'],'MISSING')
        self.assertTrue(result['findings'][0]['incomplete'])

    def test_empty_log_is_unknown(self):
        result=analyze('No probe capture records');self.assertTrue(result['final_capture_incomplete'])
        self.assertEqual(result['final_capture_by_batch'],[])

    def test_complete_negative_and_positive(self):
        for positive in [False,True]:
            with self.subTest(positive=positive):
                result=report(inputs()+[record('liquid','final') if positive else absent_final()])
                self.assertIs(found(result),positive);self.assertFalse(result['final_capture_incomplete'])
                self.assertEqual(result['final_capture_by_batch'][0]['status'],'COMPLETE')

    def test_batch_local_final_presence(self):
        result=report(inputs(1)+[absent_final()]+inputs(2))
        self.assertIs(found(result,batch=1),False);self.assertIsNone(found(result,batch=2))
        self.assertTrue(result['final_capture_incomplete'])
        self.assertEqual([s['status'] for s in result['final_capture_by_batch']],['COMPLETE','MISSING'])

    def test_incomplete_final_negatives_are_unknown(self):
        mutations={
            'truncated':lambda r:r.update(total=8),
            'uv_unaligned':lambda r:r.update(aligned=0),
            'null_uv':lambda r:r['vertices'][0].__setitem__(3,None),
            'nan_uv':lambda r:r['vertices'][0].__setitem__(3,float('nan')),
            'inf_uv':lambda r:r['vertices'][0].__setitem__(4,float('inf')),
            'null_position':lambda r:r['vertices'][0].__setitem__(0,None),
            'nan_position':lambda r:r['vertices'][0].__setitem__(1,float('nan')),
            'inf_position':lambda r:r['vertices'][0].__setitem__(2,float('inf')),
            'invalid_origin':lambda r:r.update(origin=[None,0,0]),
            'nan_origin':lambda r:r.update(origin=[float('nan'),0,0]),
            'short_origin':lambda r:r.update(origin=[0,0]),
            'indices':lambda r:r.update(index_count=4),
            'triangle_mode':lambda r:r.update(mode=2),
            'first_unaligned':lambda r:r.update(first=1),
            'final_suffix':lambda r:r.update(first=4),
            'incomplete_quad':lambda r:r.update(total=5,recorded=5,vertices=r['vertices']+[r['vertices'][0]]),
            'vertex_count':lambda r:r['vertices'].pop(),
            'short_vertex':lambda r:r['vertices'][0].pop(),
            'negative_count':lambda r:r.update(recorded=-4),
            'noninteger_count':lambda r:r.update(recorded=4.0),
            'missing_total':lambda r:r.pop('total'),
            'wrong_role':lambda r:r.update(role='body'),
            'bad_numeric_position':lambda r:r['vertices'][0].__setitem__(0,'invalid'),
            'world_position_overflow':lambda r:(r.update(origin=[1e308,0,0]),r['vertices'][0].__setitem__(0,1e308)),
        }
        for name,mutate in mutations.items():
            with self.subTest(name=name):
                final=absent_final();mutate(final);result=report(inputs()+[final])
                self.assertIsNone(found(result));self.assertTrue(result['final_capture_incomplete'])
                self.assertEqual(result['final_capture_by_batch'][0]['status'],'INCOMPLETE')

    def test_valid_positive_survives_partial_capture(self):
        final=record('liquid','final');final['total']=8
        result=report(inputs()+[final]);self.assertIs(found(result),True);self.assertTrue(result['final_capture_incomplete'])
        final=record('liquid','final');final.update(total=8,recorded=8,vertices=final['vertices']+copy.deepcopy(final['vertices']))
        final['vertices'][4][3]=None
        result=report(inputs()+[final]);self.assertIs(found(result),True);self.assertTrue(result['final_capture_incomplete'])

    def test_invalid_uv_cannot_prove_positive(self):
        final=record('liquid','final');final['aligned']=0
        self.assertIsNone(found(report(inputs()+[final])))

    def test_invalid_query_is_unknown_even_with_complete_final(self):
        for mutation in ['unaligned','null_uv','nan_uv','inf_uv']:
            with self.subTest(mutation=mutation):
                records=inputs()
                if mutation=='unaligned':records[-1]['aligned']=0
                else:records[-1]['vertices'][0][3]={'null_uv':None,'nan_uv':float('nan'),'inf_uv':float('inf')}[mutation]
                result=report(records+[absent_final()]);self.assertIsNone(found(result))
                self.assertIsNone(result['findings'][0]['matches'][0]['same_positions_uv'])
                self.assertTrue(result['findings'][0]['incomplete']);self.assertFalse(result['final_capture_incomplete'])

    def test_optional_native_colors_do_not_affect_position_uv_query(self):
        final=record('liquid','final');final['color_aligned']=0
        for v in final['vertices']:v[5]=None
        result=report(inputs()+[final]);self.assertIs(found(result),True);self.assertFalse(result['final_capture_incomplete'])

    def test_complete_empty_final_proves_absence(self):
        final=record('liquid','final');final.update(total=0,recorded=0,vertices=[])
        result=report(inputs()+[final]);self.assertIs(found(result),False);self.assertFalse(result['final_capture_incomplete'])

    def test_multiple_final_records_cannot_prove_negative(self):
        result=report(inputs()+[absent_final(),absent_final()])
        self.assertIsNone(found(result));self.assertTrue(result['final_capture_incomplete'])
        self.assertIn('multiple_final_records',result['final_capture_by_batch'][0]['issues'])

    def test_existing_captured_cpu_fixture_positive_is_preserved(self):
        # Sealed output from the actual C++ observer fixture; input vertices
        # remain synthetic and are not a native engine success claim.
        root=Path(__file__).resolve().parents[3]
        log=root/'candidate/LHolo-geometry-probe-b81-5c3eb9c/evidence/geometry-probe-tests/run.log'
        if not log.exists():self.skipTest('Sealed C++ fixture output is not available')
        result=analyze(log.read_text());self.assertIs(found(result),True)
        self.assertFalse(result['final_capture_incomplete'])

if __name__=='__main__':unittest.main(verbosity=2)
