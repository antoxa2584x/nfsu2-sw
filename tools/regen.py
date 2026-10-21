"""Regenerate a region's translation using the shared upstream toolkit."""
from pathlib import Path
import argparse,hashlib,json,os,re,struct,subprocess,sys
from eu_codegen_fixes import fix_audio_flags
ROOT=Path(__file__).resolve().parents[1]
EU_SHA='34e2a0dbc722302d12b05b12fbaf8d7c3ddd024183f3e33b96fff09156b29604'
def detect(data):
    if len(data)<0x178 or data[:4]!=b'XBEH':raise ValueError('Invalid XBE header')
    entry=struct.unpack_from('<I',data,0x128)[0]^0xA8FC57AB
    if entry==0x21B77E and hashlib.sha256(data).hexdigest()==EU_SHA:return 'EU'
    if entry==0x21B1CE:return 'US'
    raise ValueError('Unsupported XBE revision')
def eu_analysis(source, output, data):
    """Apply verified callback boundaries to a private analysis copy."""
    callbacks=json.loads((ROOT/'config/seed_functions_eu.json').read_text(encoding='utf-8'))
    base=struct.unpack_from('<I',data,0x104)[0]
    count=struct.unpack_from('<I',data,0x11C)[0]
    table=struct.unpack_from('<I',data,0x120)[0]-base
    sections=[]
    for i in range(count):
        row=table+56*i
        _,va,_,raw,size,name=struct.unpack_from('<6I',data,row)
        name=data[name-base:name-base+32].split(b'\0',1)[0].decode('ascii')
        sections.append((va,raw,size,name))
    def body(start,end):
        for va,raw,size,name in sections:
            if va<=start<end<=va+size:return data[raw+start-va:raw+end-va],name
        raise ValueError('Callback range is outside the executable sections')
    files={name:json.loads((source/name).read_text(encoding='utf-8')) for name in
        ['disasm/functions.json','disasm/labels.json','disasm/summary.json',
         'func_id/identified_functions.json','abi/abi_functions.json']}
    funcs={f['start']:f for f in files['disasm/functions.json']}
    abi={f['address']:f for f in files['abi/abi_functions.json']}
    for c in callbacks:
        if 'end' not in c:continue
        start,end=int(c['start'],16),int(c['end'],16)
        code,section=body(start,end)
        if hashlib.sha256(code).hexdigest()!=c['original_sha256']:
            raise ValueError('Verified EU callback bytes differ: '+c['start'])
        prologue=code.startswith(b'\x55\x8b\xec')
        f=dict(funcs.get(c['start'],{}))
        f.update(start=c['start'],end=c['end'],size=end-start,name='sub_'+c['start'][2:],
                 section=section,confidence=1.0,detection_method='verified_eu_callback',
                 num_instructions=c.get('num_instructions',0),has_prologue=prologue,
                 calling_convention=c['calling_convention'])
        funcs[c['start']]=f
        abi[c['start']]=dict(address=c['start'],calling_convention=c['calling_convention'],
             estimated_params=c.get('stack_params',0),return_hint='int_or_void',
             frame_type='standard_frame' if prologue else 'fpo_leaf',heuristic_confidence=1.0)
    files['disasm/functions.json']=list(funcs.values())
    files['abi/abi_functions.json']=list(abi.values())
    target=output/'region-analysis';target.mkdir()
    for name,value in files.items():
        p=target/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(json.dumps(value),encoding='utf-8')
    return target
def rewrite_eu(output):
    # Only the previously verified EU instruction sites receive bounded HLE.
    sites={
      0x2F221C:('in al, dx','SET_LO8(eax, nfsu2_eu_port_read8((uint16_t)edx, 0x002F221Cu))'),
      0x2F241D:('in al, dx','SET_LO8(eax, nfsu2_eu_port_read8((uint16_t)edx, 0x002F241Du))'),
      0x2F3330:('out dx, al','nfsu2_eu_port_write8((uint16_t)edx, (uint8_t)eax, 0x002F3330u)'),
      0x2EED14:('wbinvd','nfsu2_eu_wbinvd(0x002EED14u)'),
      0x21CBDD:('cli','nfsu2_eu_cli(0x0021CBDDu)'),
      0x21CBE4:('sti','nfsu2_eu_sti(0x0021CBE4u)'),
      0x33D78D:('cli','nfsu2_eu_cli(0x0033D78Du)'),
      0x33D79E:('sti','nfsu2_eu_sti(0x0033D79Eu)'),
    }
    counts={pc:0 for pc in sites}
    shadows={0x21CBE4:('PUSH32(esp, ebx);',0x21CBE5),0x33D79E:('esi = eax;',0x33D79F)}
    for path in output.glob('recomp_*.c'):
        text=path.read_text(encoding='utf-8');old=text
        for pc,(instruction,call) in sites.items():
            pattern=r'RECOMP_UNIMPL\("'+re.escape(instruction)+r'",\s*0x'+f'{pc:08X}'+r'u\);[^\n]*'
            text,n=re.subn(pattern,call+'; /* verified region instruction */',text)
            counts[pc]+=n
            if n and pc in shadows:
                next_statement,retired=shadows[pc]
                at=text.index(call+';')
                following=text.find(next_statement,at)
                if following<0 or following-at>400:raise ValueError('STI shadow differs at '+hex(pc))
                end=following+len(next_statement)
                text=text[:end]+f'\n    nfsu2_eu_sti_shadow_end(0x{retired:08X}u);'+text[end:]
        if text!=old:
            text=text.replace('#include "recomp_funcs.h"','#include "recomp_funcs.h"\n#include "eu_port_io.h"\n#include "eu_cache.h"\n#include "eu_interrupt.h"')
            path.write_text(text,encoding='utf-8',newline='\n')
    if any(n!=1 for n in counts.values()):raise ValueError('Verified instruction sites were not generated exactly once: '+repr(counts))
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--xbe',type=Path,required=True)
    ap.add_argument('--region',choices=['auto','US','EU'],default='auto')
    ap.add_argument('--gen-dir',type=Path,required=True)
    ap.add_argument('--analysis-dir',type=Path,help='Read existing complete analysis instead of reanalysing the disc')
    ap.add_argument('--toolkit',type=Path,default=ROOT/'xboxrecomp')
    args=ap.parse_args();args.xbe=args.xbe.resolve();data=args.xbe.read_bytes();region=detect(data)
    if args.region!='auto' and args.region!=region:raise ValueError('Requested region does not match the supplied XBE')
    output=args.gen_dir.resolve()
    if output==ROOT or ROOT.is_relative_to(output):raise ValueError('Generation output must not contain the source repository')
    if output.exists() and any(output.iterdir()):raise ValueError('Generation directory must be empty; existing translations are never overwritten')
    output.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['PYTHONUTF8']='1'
    if region=='EU':env.update(RECOMP_REG_LOCALS='0',RECOMP_X87_LOCALS='0',RECOMP_LEAF_LOCALS='0')
    toolkit=args.toolkit.resolve()
    analysis=args.analysis_dir.resolve() if args.analysis_dir else output/'analysis'
    if args.analysis_dir:
        if hashlib.sha256((analysis/'input/default.xbe').read_bytes()).digest()!=hashlib.sha256(data).digest():
            raise ValueError('Analysis belongs to a different executable')
    else:
        analysis.mkdir(parents=True,exist_ok=True)
        (analysis/'input').mkdir();(analysis/'input/default.xbe').write_bytes(data)
        seed=ROOT/'config'/('seed_functions_eu.json' if region=='EU' else 'seed_functions.json')
        stages=[
          ('xbe_parser',[args.xbe,'--json',analysis/'input/default_analysis.json']),
          ('disasm',[args.xbe,'--analysis-json',analysis/'input/default_analysis.json','--seed-functions',seed,'--output',analysis/'disasm','--force']),
          ('func_id',[args.xbe,'--functions',analysis/'disasm/functions.json','--strings',analysis/'disasm/strings.json','--xrefs',analysis/'disasm/xrefs.json','--output',analysis/'func_id']),
          ('abi_analysis',[args.xbe,'--disasm-dir',analysis/'disasm','--func-id-dir',analysis/'func_id','--output-dir',analysis/'abi']),
        ]
        for module,params in stages:subprocess.run([sys.executable,'-m','tools.'+module,*map(str,params)],cwd=toolkit,env=env,check=True)
    manual=ROOT/'src/recomp_manual.c'
    if region=='EU':
        analysis=eu_analysis(analysis,output,data)
        manual=output/'manual-scan.c'
        paths=sorted((ROOT/'src/regions/eu').glob('*.c'))
        manual.write_text('\n'.join(p.read_text(encoding='utf-8') for p in paths if p.name!='eu_generation_bridge.c'),encoding='utf-8')
    params=[sys.executable,'-m','tools.recomp',str(args.xbe.resolve()),'--game-name','NFS Underground 2 '+region,'--all','--split','100',
      '--gen-dir',str(output),'--disasm-dir',str(analysis/'disasm'),'--func-id-dir',str(analysis/'func_id'),
      '--abi-dir',str(analysis/'abi'),'--exclude-manual',str(manual),'--mmio-sections','DSOUND,XPP']
    subprocess.run(params,cwd=toolkit,env=env,check=True)
    if region=='EU':
        rewrite_eu(output)
        fix_audio_flags(output, data)
    # The combined manual source is for scanning, never a compiled game chunk.
    if region=='EU':manual.rename(output/'manual-scan.txt')
    manifest={'region':region,'xbe_sha256':hashlib.sha256(data).hexdigest(),'entry':f'0x{(0x21B77E if region=="EU" else 0x21B1CE):08X}'}
    (output/'nfsu2_region.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('PASS: generated',region,'translation with the shared toolkit; build with -DNFSU2_REGION='+region)
if __name__=='__main__':main()
