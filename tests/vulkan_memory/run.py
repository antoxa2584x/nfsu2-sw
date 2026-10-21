"""Run the renderer's actual allocator against mocked Vulkan memory types."""
from pathlib import Path
import argparse,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang');args=ap.parse_args()
    text=(ROOT/'xboxrecomp/src/nv2a_vk/nv2a_vk.c').read_text(encoding='utf-8')
    start=text.index('static uint32_t mem_type(');end=text.index('/*',text.index('\n}\n',text.index('static VkDeviceMemory alloc_mem(',start))+3)
    actual=text[start:end]
    prefix=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t VkMemoryPropertyFlags;
typedef uintptr_t VkDeviceMemory;
typedef struct { uint64_t size; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint32_t sType; uint64_t allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 1
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 2
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 4
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT 8
static struct { uint32_t memoryTypeCount; struct { uint32_t propertyFlags; } memoryTypes[32]; } s_memprops;
static uintptr_t s_dev;
static unsigned selected, called, fail;
static int vkAllocateMemory(uintptr_t device, const VkMemoryAllocateInfo *ai, void *callbacks, VkDeviceMemory *out)
{
    (void)device;(void)callbacks;
    assert(ai->memoryTypeIndex < s_memprops.memoryTypeCount);
    selected=ai->memoryTypeIndex;called++;
    if(fail)return -1;
    *out=42;return 0;
}
'''
    suffix=r'''
int main(void)
{
    VkMemoryRequirements rq={4096,1};
    s_memprops.memoryTypeCount=2;
    s_memprops.memoryTypes[0].propertyFlags=2|4;
    s_memprops.memoryTypes[1].propertyFlags=1;
    assert(alloc_mem(rq,1)==42 && selected==0); /* unified NVK, locality preferred */
    rq.memoryTypeBits=3;
    assert(alloc_mem(rq,1)==42 && selected==1); /* prefer actual local memory */
    assert(alloc_mem(rq,2|4|8)==42 && selected==0); /* cache optional, host access required */
    unsigned before=called;
    rq.memoryTypeBits=2;
    assert(alloc_mem(rq,2|4)==0 && called==before); /* never relax host/coherence */
    rq.memoryTypeBits=0;
    assert(alloc_mem(rq,1)==0 && called==before); /* obey allowed memory bits */
    rq.memoryTypeBits=1;fail=1;
    assert(alloc_mem(rq,2|4)==0); /* allocation failure propagated */
    puts("PASS: 6 Vulkan allocation cases using the actual renderer code");
}
'''
    work=ROOT/'build-eu';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vk-memory-',dir=work) as name:
        directory=Path(name).resolve();assert directory.is_relative_to(work.resolve())
        src=directory/'memory.c';exe=directory/'memory.exe'
        src.write_text(prefix+actual+suffix,encoding='utf-8')
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
