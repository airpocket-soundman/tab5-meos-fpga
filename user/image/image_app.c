#include "image_app.h"
#include "image_geometry.h"
#include "../camera/camera_app.h"
#include "../tui/dqwin.h"
#include <m5os/display.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>

int image_file_info(const char *path)
{
    camera_bmp_stats_t stats;
    if(!camera_verify_bmp_file(path,&stats)) {
        kprintf("image: cannot decode BMP (16/24-bit, up to1920x1080); JPEG/PNG not implemented\n");return 1;
    }
    kprintf("{\"format\":\"bmp\",\"width\":%u,\"height\":%u,\"decoded_bytes\":%u,"
            "\"fnv1a\":%u,\"nonuniform\":%s}\n",stats.width,stats.height,stats.pixel_count*2,
            stats.fnv1a,stats.nonuniform?"true":"false");
    return 0;
}
int image_view_file(const char *path)
{
    uint16_t *pixels=NULL;int width=0,height=0;
    if(!camera_load_bmp(path,&pixels,&width,&height)) {
        kprintf("image: cannot decode BMP (JPEG/PNG not implemented)\n");return 1;
    }
    int available_w=DISPLAY_WIDTH-16,available_h=DISPLAY_HEIGHT-64;
    if(available_w<1||available_h<1){kfree(pixels);return 1;}
    uint16_t *row=kmalloc_psram((size_t)available_w*sizeof(uint16_t));
    if(!row)row=kmalloc((size_t)available_w*sizeof(uint16_t));
    if(!row){kfree(pixels);kprintf("image: not enough memory for scaling row\n");return 1;}
    bool enlarge=true;
    int result=0;
    kprintf("image: BMP %dx%d ready; Q/Esc/Enter closes, Z toggles enlargement\n",width,height);
    for(;;) {
        int draw_w,draw_h;
        if(!image_fit_size(width,height,available_w,available_h,enlarge,&draw_w,&draw_h)){result=1;break;}
        int dx=(DISPLAY_WIDTH-draw_w)/2,dy=32+(available_h-draw_h)/2;
        bool batch=display_begin_frame();
        display_clear(COLOR_BLACK);
        display_draw_text(8,0,"BMP IMAGE VIEWER",COLOR_WHITE,COLOR_BLACK,1);
        display_draw_text(8,DISPLAY_HEIGHT-24,enlarge?"Q/Esc:Close Z:Shrink only":"Q/Esc:Close Z:Enlarge to fit",COLOR_WHITE,COLOR_BLACK,1);
        for(int y=0;y<draw_h;y++) {
            const uint16_t *source=pixels+(size_t)((int64_t)y*height/draw_h)*width;
            for(int x=0;x<draw_w;x++)row[x]=source[(int64_t)x*width/draw_w];
            display_draw_bitmap(dx,dy+y,draw_w,1,row);
        }
        if(batch&&!display_end_frame()){kprintf("image: display presentation failed\n");result=1;break;}
        int key;
        do {key=dq_getkey();} while(key!='q'&&key!='Q'&&key!=DQK_ESC&&key!=DQK_ENTER&&key!=3&&key!='z'&&key!='Z'&&key!=' ');
        if(key=='z'||key=='Z'||key==' '){enlarge=!enlarge;continue;}
        break;
    }
    kfree(row);kfree(pixels);kprintf("image: viewer closed\n");return result;
}
