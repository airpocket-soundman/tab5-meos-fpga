#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Fit without changing aspect ratio. Enlargement is explicitly selectable. */
static inline bool image_fit_size(int width,int height,int available_w,int available_h,
                                  bool enlarge,int *out_w,int *out_h)
{
    if(!out_w||!out_h||width<=0||height<=0||available_w<=0||available_h<=0)return false;
    int w=width,h=height;
    if(enlarge||width>available_w||height>available_h) {
        if((int64_t)width*available_h>(int64_t)height*available_w) {
            w=available_w;h=(int)((int64_t)height*w/width);
        } else {
            h=available_h;w=(int)((int64_t)width*h/height);
        }
    }
    *out_w=w>0?w:1;*out_h=h>0?h:1;return true;
}
