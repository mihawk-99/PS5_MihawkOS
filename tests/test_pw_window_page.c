/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_window_page.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    size_t bytes=pw_window_page_bytes(3,2);assert(bytes==16384);
    assert(pw_window_page_bytes(2560,1440)>0 && pw_window_page_bytes(3840,2160)>0 && pw_window_page_bytes(4096,2432)>0);
    assert(!pw_window_page_bytes(4097,2));assert(!pw_window_page_bytes(2,2433));
    PwWindowPage *p=calloc(1,bytes),h;assert(p);
    assert(!pw_window_page_init(p,bytes,4,8,3,2,-16,5));
    assert(!pw_window_page_header(p,bytes,&h));assert(h.offset_x==-16 && h.generation==8);
    uint32_t in[]={1,2,3,99,4,5,6,99},out[6]={0},seq=0;
    assert(pw_window_page_read(p,&h,out,sizeof(out),0,&seq)==0);
    assert(!pw_window_page_write(p,in,16));
    assert(pw_window_page_read(p,&h,out,sizeof(out),0,&seq)==1 && seq==2);
    assert(out[0]==1 && out[3]==4 && out[5]==6);
    assert(pw_window_page_read(p,&h,out,sizeof(out),seq,&seq)==0);
    p->sequence=3;out[0]=99;
    assert(pw_window_page_read(p,&h,out,sizeof(out),2,&seq)==0 && out[0]==99);
    p->sequence=0xfffffffe;assert(!pw_window_page_write(p,in,16) && p->sequence==2);
    /* Malformed header is rejected; a previously validated reader never reads
     * live dimensions even if producer metadata changes. */
    p->width=0xffffffff;
    assert(pw_window_page_header(p,bytes,&h)<0 && errno==EPROTO);
    h.width=3;assert(pw_window_page_read(p,&h,out,sizeof(out),0,&seq)==1);
    p->width=3;assert(pw_window_page_header(p,bytes-1,&h)<0);
    assert(pw_window_page_read(p,&h,out,4,0,&seq)<0);
    p->reserved[2]=1;assert(pw_window_page_header(p,bytes,&h)<0);
    free(p);puts("window-page scratch host checks: PASS");
}
