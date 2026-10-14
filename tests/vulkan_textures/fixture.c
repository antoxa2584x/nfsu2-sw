#ifndef NV2A_SOURCE
#define NV2A_SOURCE "nv2a_vk.c"
#endif
#include NV2A_SOURCE

#include <assert.h>
static unsigned char guest_memory[8u<<20];
ptrdiff_t xbox_GetMemoryOffset(void){return (ptrdiff_t)guest_memory;}
void nv2a_backend_register(const Nv2aBackend *b){}
int xbox_video_widescreen(void){return 1;}
int nv2a_backend_decode_texture(const Nv2aTexture *t,uint32_t *p){return 0;}
uint8_t *nv2a_pb_reg_dirty(void){static uint8_t dirty[128];return dirty;}
void xbox_vblank_report(double d,char *s,size_t n){if(n)*s=0;}

#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 7;}}while(0)
static VkTex *by_view(VkImageView v){for(unsigned i=0;i<VK_MAX_TEX;i++)if(s_tex[i].view==v)return &s_tex[i];return NULL;}
static void read_image(VkImage image,unsigned w,unsigned h,unsigned level,void *out,unsigned bytes){
 VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
 VkBuffer buf;VkMemoryRequirements rq;VkDeviceMemory mem;void *ptr;
 vk_require(vkCreateBuffer(s_dev,&ci,0,&buf),"read create");vkGetBufferMemoryRequirements(s_dev,buf,&rq);
 mem=alloc_mem(rq,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);assert(mem);
 vk_require(vkBindBufferMemory(s_dev,buf,mem,0),"read bind");end_rendering();barrier_all();
 VkBufferImageCopy copy={0};copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,level,0,1};
 copy.imageExtent=(VkExtent3D){w,h,1};vkCmdCopyImageToBuffer(s_cb,image,VK_IMAGE_LAYOUT_GENERAL,buf,1,&copy);
 flush_frame_wait();vk_require(vkMapMemory(s_dev,mem,0,bytes,0,&ptr),"read map");memcpy(out,ptr,bytes);
 vkUnmapMemory(s_dev,mem);vkDestroyBuffer(s_dev,buf,0);vkFreeMemory(s_dev,mem,0);
}
int main(int argc,char **argv){
 putenv("RECOMP_PROG_CACHE=0");putenv("RECOMP_VK_HEADLESS=1");putenv("RECOMP_GL_SCALE=1");
 CHECK(argc==2 && ready());const char *which=argv[1];
 if(!strcmp(which,"pitch")){
  uint32_t *src=(uint32_t*)(guest_memory+0x10000),pixels[4];
  for(unsigned i=0;i<8;i++)src[i]=0xff000000u|(i+1)*0x10101;
  VkImageView a=tex_get(0x10000,0x12,2,2,8,0,1),b=tex_get(0x10000,0x12,2,2,16,0,1);
  CHECK(a!=b && by_view(b)->pitch==16);read_image(by_view(b)->image,2,2,0,pixels,sizeof pixels);
  CHECK(pixels[0]==src[0] && pixels[1]==src[1] && pixels[2]==src[4] && pixels[3]==src[5]);
  read_image(by_view(a)->image,2,2,0,pixels,sizeof pixels);CHECK(pixels[2]==src[2] && pixels[3]==src[3]);
 }else if(!strcmp(which,"levels")){
  memset(guest_memory+0x20000,0x55,16);
  VkImageView a=tex_get(0x20000,0x0c,4,4,0,0,1),b=tex_get(0x20000,0x0c,4,4,0,0,2);
  CHECK(s_have_bc && a!=b && by_view(b)->levels==2);
  unsigned char bytes[8];read_image(by_view(b)->image,2,2,1,bytes,8);CHECK(!memcmp(bytes,guest_memory+0x20008,8));
 }else if(!strcmp(which,"mip-change")){
  memset(guest_memory+0x30000,0x55,16);VkImageView a=tex_get(0x30000,0x0c,4,4,0,0,2);
  unsigned char bytes[8];read_image(by_view(a)->image,2,2,1,bytes,8);CHECK(!memcmp(bytes,guest_memory+0x30008,8));
  memset(guest_memory+0x30008,0xaa,8);s_frame++;VkImageView b=tex_get(0x30000,0x0c,4,4,0,0,2);
  read_image(by_view(b)->image,2,2,1,bytes,8);CHECK(!memcmp(bytes,guest_memory+0x30008,8));
 }else if(!strcmp(which,"palette")){
  uint32_t pixels[4];memset(guest_memory+0x40000,0,4);s_palette_reg=0x50000;s_palette_va=0x50000;
  *(uint32_t*)(guest_memory+0x50000)=0xffff0000;*(uint32_t*)(guest_memory+0x51000)=0xff00ff00;
  VkImageView a=tex_get(0x40000,0x0b,2,2,0,0,1);s_palette_va=0x51000;s_palette_reg=0x51000;
  VkImageView b=tex_get(0x40000,0x0b,2,2,0,0,1);CHECK(a!=b);
  read_image(by_view(b)->image,2,2,0,pixels,16);for(unsigned i=0;i<4;i++)CHECK(pixels[i]==0xff00ff00);
  read_image(by_view(a)->image,2,2,0,pixels,16);for(unsigned i=0;i<4;i++)CHECK(pixels[i]==0xffff0000);
 }else if(!strcmp(which,"depth")){
  s_scale=s_scale_x=1.5;s_square=1;
  VkSurf *a=surf_get(0x60000,1280,480,2,1),*b=surf_get(0x70000,1280,480,1,1);
  CHECK(a && b && a->pw==b->pw && a->ph==b->ph);
  CHECK(depth_get(0x80000,a->pw,a->ph)==depth_get(0x80000,b->pw,b->ph));
 }else if(!strcmp(which,"surface-aa")){
  s_scale=s_scale_x=1.5;s_square=1;
  VkSurf *a=surf_get(0x60000,1280,480,2,1);CHECK(a);VkImage original=a->image;
  clear_image_color(a->image,1,0,0,1);VkSurf *b=surf_get(0x60000,1280,480,1,1);
  CHECK(b->image==original);uint32_t pixel;read_image(b->image,1,1,0,&pixel,4);CHECK(pixel==0xffff0000);
 }else if(!strcmp(which,"runtime-mips")){
#ifdef TEST_RUNTIME_MIPS
  uint32_t r[2048]={0};Nv2aRawBatch batch={0};float scale[2];
  VkDescriptorImageInfo desc;VkSurf *sampled;
  batch.tex_va[0]=0x20000;s_batch=&batch;s_mips=1;
  memset(guest_memory+0x20000,0x55,16);
  r[0x1b00/4]=0x20000;r[0x1b04/4]=(0x0c<<8)|(2<<16)|(2<<20)|(2<<24);
  r[0x1b0c/4]=0x40000000|(1<<6);
  nv2a_vk_mips=0;bind_stage(r,0,scale,&desc,&sampled);
  CHECK(by_view(desc.imageView)->levels==1);
  nv2a_vk_mips=1;bind_stage(r,0,scale,&desc,&sampled);
  CHECK(by_view(desc.imageView)->levels==2);
  unsigned char bytes[8];read_image(by_view(desc.imageView)->image,2,2,1,bytes,8);
  CHECK(!memcmp(bytes,guest_memory+0x20008,8));
  nv2a_vk_mips=0;bind_stage(r,0,scale,&desc,&sampled);
  CHECK(by_view(desc.imageView)->levels==1);
  VkSurf *faces[6]={surf_get(0x60000,4,4,1,1)};
  clear_image_color(faces[0]->image,1,0,0,1);
  for(unsigned mode=0;mode<3;mode++){
   nv2a_vk_mips=mode==1;
   VkImageView view=cube_from_surfaces(0x60000,4,4,64,faces,3);
   VkCube *cube=NULL;for(unsigned i=0;i<VK_MAX_CUBE;i++)if(s_cube[i].view==view)cube=&s_cube[i];
   CHECK(cube && cube->levels==(mode==1?3:1));
   uint32_t pixel;read_image(cube->image,1,1,mode==1?2:0,&pixel,4);
   CHECK(pixel==0xffff0000);
  }
  nv2a_vk_mips=1;
  VkImageView view=cube_from_surfaces(0x60000,4,4,64,faces,1);
  VkCube *cube=NULL;for(unsigned i=0;i<VK_MAX_CUBE;i++)if(s_cube[i].view==view)cube=&s_cube[i];
  CHECK(cube && cube->levels==1);
#else
  return 8;
#endif
 }else if(!strcmp(which,"lod-clamp")){
#ifdef TEST_RUNTIME_MIPS
  uint32_t r[2048]={0};Nv2aRawBatch batch={0};float scale[2];
  VkDescriptorImageInfo a,b;VkSurf *sampled;
  batch.tex_va[0]=0x20000;s_batch=&batch;s_mips=1;nv2a_vk_mips=1;
  memset(guest_memory+0x20000,0x55,16);
  r[0x1b00/4]=0x20000;r[0x1b04/4]=(0x0c<<8)|(2<<16)|(2<<20)|(2<<24);
  r[0x1b0c/4]=0x40000000;r[0x1b14/4]=(6<<16)|(2<<24);
  bind_stage(r,0,scale,&a,&sampled);CHECK(by_view(a.imageView)->levels==1);
  r[0x1b0c/4]=0x40000000|(1<<6);
  bind_stage(r,0,scale,&a,&sampled);CHECK(by_view(a.imageView)->levels==2);
  r[0x1b0c/4]|=1<<18;bind_stage(r,0,scale,&b,&sampled);
  CHECK(a.imageView==b.imageView && a.sampler!=b.sampler);
  r[0x1b0c/4]=0x40000000|(0x3ff<<6)|(0x3ff<<18);
  bind_stage(r,0,scale,&a,&sampled);
  CHECK(a.sampler==b.sampler); /* Both clamps stop at the last real level. */
#else
  return 8;
#endif
 }else return 8;
 puts("PASS: actual GPU texture bytes");return 0;
}
