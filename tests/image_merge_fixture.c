/* Run the actual shared BMP decoder and merged viewer PPM loader. */
#include <assert.h>
#define main original_bmp_main
#include "camera/test_bmp.c"
#undef main
#undef RGB565
#include "../user/viewer/imgview.c"
int vfs_stat(const char*p,vfs_info_t*s){(void)p;memset(s,0,sizeof*s);s->type=FS_TYPE_FILE;s->size=file_len;return 0;}
static void bytes(const void*p,size_t n){assert(n<=sizeof file);memcpy(file,p,n);file_len=n;pos=0;close_fail=false;}
static bool fails(void){uint16_t*p=(void*)1;int w=9,h=9;bool ok=imgview_load("/test",&p,&w,&h);if(ok)free(p);return !ok&&!p&&!w&&!h;}
static unsigned scaled_calls;
void display_draw_bitmap_resized(int x,int y,int sw,int sh,const uint16_t *src,int dw,int dh){
 assert(x==3&&y==4&&sw==2&&sh==2&&dw==20&&dh==10&&src);scaled_calls++;
}
int main(void){
 assert(original_bmp_main()==0);close_fail=false;
 uint16_t*p;int w,h;
 const uint8_t ppm[]={ 'P','6','\n','2',' ','1','\n','2','5','5','\n',255,0,0,0,255,0 };
 bytes(ppm,sizeof ppm);assert(imgview_load("/x",&p,&w,&h)&&w==2&&h==1&&p[0]==0xf800&&p[1]==0x07e0);free(p);
 const uint8_t pgm[]={ 'P','5','\r','\n','#','x','\n','1',' ','1','\n','2','5','5','\r','\n',10 };
 bytes(pgm,sizeof pgm);assert(imgview_load("/x",&p,&w,&h)&&w==1&&h==1&&p[0]==RGB565(10,10,10));free(p);
 const char*bad[]={"P6\n2147483648 1\n255\nabc","P6\n2147483647 2147483647\n255\nabc","P6\n1 1\n255Xabc","P6\n1 1\n255\nab","P5\n1 1\n256\nx","P6\n-1 1\n255\nabc","P5\n4194305 1\n255\nx"};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++){bytes(bad[i],strlen(bad[i]));assert(fails());}
 uint16_t source[]={0xf800,0x07e0,0x001f,0xffff};imgview_draw_fit(3,4,20,10,source,2,2);assert(scaled_calls==1);assert(camera_image_save_rgb565("x",source,2,2));assert(imgview_load("x",&p,&w,&h)&&w==2&&h==2&&!memcmp(p,source,sizeof source));free(p);
 put_le32(file+22,0x80000000u);assert(fails());
 base24(2,2);put_le32(file+14,0xffffffffu);assert(fails());
 base24(2,2);put_le32(file+18,0x7fffffffu);assert(fails());
 base24(2,2);put_le16(file+28,8);assert(fails()); /* unsupported palette path never parsed twice */
 assert(imgview_write_bmp("x",source,INT_MAX,1)==ERR_INVAL);
 assert(imgview_write_bmp("x",source,-1,2)==ERR_INVAL);
 assert(imgview_write_bmp("x",NULL,2,2)==ERR_INVAL);
 puts("PASS merged viewer: shared BMP delegation, valid P6/P5+CRLF/comments, decimal/dimension/truncation/separator rejection and writer bounds");return 0;
}

void display_draw_bitmap(int x,int y,int w,int h,const uint16_t*p){(void)x;(void)y;(void)w;(void)h;(void)p;assert(0);}
void display_fill_rect(int x,int y,int w,int h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;assert(0);}
void display_draw_rect(int x,int y,int w,int h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;assert(0);}
int ksnprintf(char*p,size_t n,const char*f,...){(void)p;(void)n;(void)f;assert(0);return 0;}
int dq_cols(void){assert(0);return 0;}int dq_rows(void){assert(0);return 0;}int dq_getkey(void){assert(0);return 0;}
void dq_message(const char*a,const char*b){(void)a;(void)b;assert(0);}
void dq_text(int x,int y,const char*s){(void)x;(void)y;(void)s;assert(0);}
void dq_window(int x,int y,int w,int h){(void)x;(void)y;(void)w;(void)h;assert(0);}
