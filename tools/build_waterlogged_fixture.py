"""Extract exact captured palette permutations; never invent registry states."""
from pathlib import Path
import argparse,copy,hashlib,json,struct
class NbtList(list):
    def __init__(self,subtype,values):super().__init__(values);self.subtype=subtype
class Reader:
    def __init__(self,data):self.data=data;self.i=0
    def take(self,n):
        assert 0<=n<=len(self.data)-self.i;b=self.data[self.i:self.i+n];self.i+=n;return b
    def number(self,f):return struct.unpack('<'+f,self.take(struct.calcsize('<'+f)))[0]
    def name(self):return self.take(self.number('H')).decode('utf-8')
    def payload(self,t):
        if t in [1,2,3,4,5,6]:return self.number({1:'b',2:'h',3:'i',4:'q',5:'f',6:'d'}[t])
        if t==8:return self.name()
        if t in [7,11,12]:return [self.number({7:'b',11:'i',12:'q'}[t]) for _ in range(self.number('i'))]
        if t==9:
            sub=self.number('B');return NbtList(sub,[(sub,self.payload(sub)) for _ in range(self.number('i'))])
        if t==10:
            d={}
            while(t:=self.number('B')):
                name=self.name();d[name]=(t,self.payload(t))
            return d
        raise ValueError(t)
    def root(self):assert self.number('B')==10;name=self.name();d=self.payload(10);assert self.i==len(self.data);return name,d
def value(d,n):return d[n][1]
def pname(s):b=s.encode('utf-8');return struct.pack('<H',len(b))+b
def encode(t,v):
    if t in [1,2,3,4,5,6]:return struct.pack('<'+{1:'b',2:'h',3:'i',4:'q',5:'f',6:'d'}[t],v)
    if t==8:return pname(v)
    if t in [7,11,12]:return struct.pack('<i',len(v))+b''.join(encode({7:1,11:3,12:4}[t],x) for x in v)
    if t==9:
        sub=v.subtype if isinstance(v,NbtList) else v[0][0] if v else 10;assert all(x[0]==sub for x in v)
        return bytes([sub])+struct.pack('<i',len(v))+b''.join(encode(t,x) for t,x in v)
    if t==10:return b''.join(bytes([t])+pname(n)+encode(t,x) for n,(t,x) in v.items())+b'\0'
    raise ValueError(t)
def plain(v):
    if isinstance(v,dict):return {n:{'tag':t,'value':plain(x)} for n,(t,x) in v.items()}
    if isinstance(v,list):return [plain(x) for x in v]
    return v
def build(source,out):
    data=source.read_bytes();root_name,root=Reader(data).root();assert bytes([10])+pname(root_name)+encode(10,root)==data,'Typed NBT roundtrip differs'
    st=value(root,'structure');palette=value(value(value(st,'palette'),'default'),'block_palette');blocks=value(st,'block_indices')
    indices=[[x[1] if isinstance(x,tuple) else x for x in layer] for _,layer in blocks];assert len(indices)==2
    names=[value(v,'name') for _,v in palette];samples={}
    for i,(a,b) in enumerate(zip(*indices)):
        if a>=0 and b>=0 and names[a]=='minecraft:quartz_stairs' and names[b]=='minecraft:water':samples.setdefault((a,b),i)
    assert samples,'No exact captured waterlogged quartz stairs'
    samples=list(samples.items())[:4];out.mkdir(parents=True,exist_ok=True);files={}
    for boundary in [False,True]:
        size=[32 if boundary else 12,4,10];volume=size[0]*size[1]*size[2];first=[-1]*volume;second=[-1]*volume;native=[];cells=[]
        def index(x,y,z):return x*size[1]*size[2]+y*size[2]+z
        for i,((a,b),source_index) in enumerate(samples):
            native.extend([copy.deepcopy(palette[a]),copy.deepcopy(palette[b])]);wetX,dryX=(15,16) if boundary else (2,8);z=2+i*2
            for kind,x in [('wet',wetX),('dry',dryX)]:
                at=index(x,1,z);first[at]=i*2
                if kind=='wet':second[at]=i*2+1
                cells.append({'kind':kind,'position':[x,1,z],'index':at,'body_palette':i*2,'liquid_palette':i*2+1 if kind=='wet' else -1,'source_index':source_index,'source_palette':[a,b]})
        # Preserve the captured list/array representation of each index layer.
        layer_type=blocks[0][0]
        def typed_indices(v):return [(3,x) for x in v] if layer_type==9 else v
        result={
            'format_version':copy.deepcopy(root['format_version']),
            'size':(9,[(3,x) for x in size]),
            'structure_world_origin':(9,[(3,0),(3,0),(3,0)]),
            'structure':(10,{'block_indices':(9,[(layer_type,typed_indices(first)),(layer_type,typed_indices(second))]),
                'entities':(9,[]),'palette':(10,{'default':(10,{'block_palette':(9,native),'block_position_data':(10,{})})})})}
        output=bytes([10])+pname(root_name)+encode(10,result);path=out/('quartz-stairs-wet-dry-'+('boundary' if boundary else 'single')+'.mcstructure');path.write_bytes(output)
        decoded=Reader(output).root()[1];assert decoded==result
        assert all(native[i*2][1]==palette[a][1] and native[i*2+1][1]==palette[b][1] for i,((a,b),_) in enumerate(samples))
        files[path.name]={'sha256':hashlib.sha256(output).hexdigest().upper(),'bytes':len(output),'size':size,'cells':cells,'native_registry_and_render':'NOT_RUN'}
    manifest={'source':str(source),'source_sha256':hashlib.sha256(data).hexdigest().upper(),'source_unchanged':source.read_bytes()==data,'source_size':[x for _,x in value(root,'size')],
        'source_cells':len(indices[0]),'native_palette_samples':[{'source_palette':[a,b],'first_source_cell':i,'body':plain(palette[a][1]),'liquid':plain(palette[b][1])} for (a,b),i in samples],
        'missing_native_families':['slab','trapdoor','fence','pane'],'copied_body_and_liquid_palette_byte_values':True,'typed_nbt_full_source_roundtrip':True,'outputs':files,'actual_native_geometry':'NOT_RUN'}
    (out/'FIXTURE_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Exact native palette fixtures created',list(files),'samples',len(samples));return manifest
def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();build(a.source,a.out)
if __name__=='__main__':main()
