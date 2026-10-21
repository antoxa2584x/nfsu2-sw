"""Check EU option wording against the retail-selected language, without assets."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang');args=ap.parse_args()
    options=(ROOT/'src/regions/eu/eu_options.c').read_text(encoding='utf-8')
    getter=re.search(r'int nfsu2_text_language_is_german\(void\)\s*\{.*?\n\}',options,re.S).group()
    prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
static uint32_t selected_language;
static uint32_t *language_ptr(uint32_t address) { assert(address==0x003A0E30u); return &selected_language; }
#define MEM32(address) (*language_ptr(address))
void (*xbox_file_read_hook)(void *,size_t,int64_t);
'''+getter+'\n#include "'+(ROOT/'src/text_patch.c').as_posix()+'"\n'
    test=r'''
static void fixture(unsigned char *buffer,int english) {
    memset(buffer,0,512);wr32(buffer,LANG_CHUNK_ID);wr32(buffer+4,504);
    unsigned char *base=buffer+8;wr32(base,0x10);wr32(base+4,4);wr32(base+8,16);wr32(base+12,48);
    const uint32_t labels[]={HASH_ENGLISH,0x8FE9288Eu,0x4AC50BCFu,0x12345678u};
    const char *values[]={english?TEXT_ENGLISH:"Starttaste","original reflection label","original resolution label","unchanged label"};
    unsigned used=0;for(unsigned i=0;i<4;i++){wr32(base+16+i*8,labels[i]);wr32(base+20+i*8,used);strcpy((char*)base+48+used,values[i]);used+=(unsigned)strlen(values[i])+1;}
}
static const char *text(unsigned char *buffer,unsigned index) {
    const uint32_t labels[]={HASH_ENGLISH,0x8FE9288Eu,0x4AC50BCFu,0x12345678u};
    unsigned char *base=buffer+8;
    for(unsigned i=0;i<rd32(base+4);i++)if(rd32(base+16+i*8)==labels[index])
        return (const char*)base+rd32(base+12)+rd32(base+20+i*8);
    assert(0);return NULL;
}
int main(void) {
    unsigned char buffer[512],saved[512];char small[4];
    selected_language=2;fixture(buffer,0);on_read(buffer,sizeof buffer,0);
#if defined(NFSU2_BUILD_EU)
    assert(!strcmp(text(buffer,1),"Reflexionen"));assert(!strcmp(text(buffer,2),"Aufl\xF6sung"));
    assert(strlen(text(buffer,2))==9 && (unsigned char)text(buffer,2)[4]==0xF6);
    assert(rd32(buffer+12)==6);
    for(unsigned i=1;i<6;i++)assert(rd32(buffer+24+(i-1)*8)<rd32(buffer+24+i*8));
    unsigned char once[512];memcpy(once,buffer,sizeof once);
    on_read(buffer,sizeof buffer,0);assert(!memcmp(once,buffer,sizeof once));
#else
    assert(!strcmp(text(buffer,1),"Car Reflections"));assert(!strcmp(text(buffer,2),"Resolution Scale"));
#endif
    assert(!strcmp(text(buffer,3),"unchanged label"));assert(selected_language==2);
    fixture(buffer,1);on_read(buffer,sizeof buffer,0);
    assert(!strcmp(text(buffer,1),"Car Reflections"));assert(!strcmp(text(buffer,2),"Resolution Scale"));
    selected_language=0;fixture(buffer,0);on_read(buffer,sizeof buffer,0);assert(!strcmp(text(buffer,1),"Car Reflections"));
    selected_language=3;fixture(buffer,0);on_read(buffer,sizeof buffer,0);assert(!strcmp(text(buffer,2),"Resolution Scale"));
    selected_language=2;fixture(buffer,0);wr32(buffer+8+20+3*8,9999);memcpy(saved,buffer,sizeof buffer);
    on_read(buffer,sizeof buffer,0);assert(!memcmp(saved,buffer,sizeof buffer));
    fixture(buffer,0);memcpy(saved,buffer,sizeof buffer);patch_chunk(buffer,200);assert(!memcmp(saved,buffer,sizeof buffer));
    assert(rewrite(0x4AC50BCFu,"original",small,sizeof small,0)==-1);
    char label[32];
    assert(rewrite(0xA9A76BBAu,"original",label,sizeof label,0)>0);
#if defined(NFSU2_BUILD_EU)
    assert(!strcmp(label,"FPS-Anzeige"));
    assert(rewrite(0x4CD775ACu,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Kantengl\xE4ttung"));
    assert((unsigned char)label[8]==0xE4);
    assert(rewrite(0x4E57D355u,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Texturfilter"));
    assert(rewrite(0xA9C27DFDu,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Quadratische Pixel"));
    char message[256];
    assert(rewrite(NFSU2_VIDEO_RECOMMENDED_TITLE,"",message,sizeof message,0)>0);
    assert(!strcmp(message,"Video: 1,5x empfohlen"));
    assert(rewrite(NFSU2_VIDEO_WARNING_TITLE,"",message,sizeof message,0)>0);
    assert(!strcmp(message,"Video: FPS-Einbruch m\xF6glich"));
    assert(rewrite(NFSU2_VIDEO_RECOMMENDED_HELP,"",message,sizeof message,0)>0);
    assert(strstr(message,"Reflexionen an")&&strstr(message,"Pixel aus"));
    assert(rewrite(NFSU2_VIDEO_WARNING_HELP,"",message,sizeof message,1)>0);
    assert(strstr(message,"FPS drops")&&strstr(message,"1.5x"));
#else
    assert(!strcmp(label,"FPS Display"));
    assert(rewrite(0x4CD775ACu,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Anti-Aliasing"));
    assert(rewrite(0x4E57D355u,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Anisotropic"));
    assert(rewrite(0xA9C27DFDu,"original",label,sizeof label,0)>0);
    assert(!strcmp(label,"Square Pixels"));
#endif
    puts("PASS: selected language, US wording, unrelated labels and malformed-pool preservation");return 0;
}
'''
    work=ROOT/'build-eu';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='text-labels-',dir=work) as name:
        work=Path(name);source=work/'test.c';source.write_text(prefix+test,encoding='utf-8',newline='\n')
        for region in ['US','EU']:
            exe=work/(region+'.exe')
            subprocess.run([args.cc,'-O2','-DNFSU2_BUILD_'+region+'=1',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
