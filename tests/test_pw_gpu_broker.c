/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_gpu_broker.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    _Static_assert(sizeof(PwGpuMessage)==64,"bounded RPC frame");
    assert(pw_gpu_contains(0x200000000,16384,0x200000000,16384));
    assert(pw_gpu_contains(0x200000000,16384,0x200003ffc,4));
    assert(!pw_gpu_contains(0x200000000,16384,0x200003ffc,8));
    assert(!pw_gpu_contains(0x200000000,16384,0x1ffffffff,4));
    assert(!pw_gpu_contains(0x200000000,16384,0x200004000,1));
    assert(!pw_gpu_contains(0x200000000,16384,0x200000000,UINT64_MAX));
    assert(!pw_gpu_contains(UINT64_MAX-7,16,UINT64_MAX-7,4));
    assert(!pw_gpu_contains(0,0,0,1));
    assert(!pw_gpu_contains(0,16384,0,0));
 assert(pw_gpu_client_pid(0,42,1));
 assert(pw_gpu_client_pid(42,42,1));
 assert(!pw_gpu_client_pid(42,43,1));
 /* The owner's own pid: only in the slot the owner opened for itself (graphics patch 0031). */
 assert(pw_gpu_client_pid(42,42,42) && !pw_gpu_client_pid(0,42,42) && !pw_gpu_client_pid(43,42,42));
 assert(!pw_gpu_client_pid(0,0,1));
 assert(!pw_gpu_client_pid(-1,42,1));
 assert(!pw_gpu_client_pid(0,UINT64_MAX,1));
 PwGpuMessage m={.address=0x240000000ull,.bytes=640*360*4,
 .argument={640ull|(360ull<<32),2560ull|(0xffee1234ull<<32),0x80000000ffffffffull,1}};
 assert(pw_gpu_window_image(&m));
 assert((uint32_t)(m.argument[1]>>32)==0xffee1234u && m.argument[2]==0x80000000ffffffffull);
 m.argument[1]++;assert(!pw_gpu_window_image(&m));m.argument[1]--;
 m.bytes++;assert(!pw_gpu_window_image(&m));m.bytes--;
 m.address++;assert(!pw_gpu_window_image(&m));m.address--;
 m.address=0;assert(!pw_gpu_window_image(&m));m.address=0x240000000ull;
 m.argument[0]=640ull|(0x10000000ull<<32);assert(!pw_gpu_window_image(&m));
 m.argument[0]=640ull|(360ull<<32);m.argument[2]=0;assert(!pw_gpu_window_image(&m));
 m.argument[2]=1;m.argument[1]=2560;assert(!pw_gpu_window_image(&m));
 m.argument[1]=2560ull|(2ull<<32);m.argument[3]=4;assert(!pw_gpu_window_image(&m));
 /* PW_GPU_PRESENT_SHOWN beside BGRA: a FIFO present answered once shown (PS5_Vulkan display-pacing). */
 m.argument[3]=PW_GPU_PRESENT_SHOWN|1;assert(pw_gpu_window_image(&m));m.argument[3]=PW_GPU_PRESENT_SHOWN|4;assert(!pw_gpu_window_image(&m));
 /* A window larger than the screen (Steam's restored 1922x1081, rows padded to 256 bytes), up to 2176x1408. */
 m.argument[3]=1;m.argument[0]=1922ull|(1081ull<<32);m.argument[1]=7936ull|(2ull<<32);m.bytes=7936ull*1081;
 assert(pw_gpu_window_image(&m));
 m.argument[0]=2176ull|(1408ull<<32);m.argument[1]=8704ull|(2ull<<32);m.bytes=8704ull*1408;assert(pw_gpu_window_image(&m));
 m.argument[0]=2177ull|(1408ull<<32);m.argument[1]=8708ull|(2ull<<32);m.bytes=8708ull*1408;assert(!pw_gpu_window_image(&m));
 m.argument[0]=2176ull|(1409ull<<32);m.argument[1]=8704ull|(2ull<<32);m.bytes=8704ull*1409;assert(!pw_gpu_window_image(&m));
 m.argument[0]=1922ull|(1081ull<<32);m.argument[1]=8708ull|(2ull<<32);m.bytes=8708ull*1081;assert(!pw_gpu_window_image(&m));

 /* A display swapchain larger than the screen (graphics patch 0039): The Binding of Isaac's borderless fullscreen
  * window, 1921x1080, was refused (VK_ERROR_INITIALIZATION_FAILED) and Zink then wrote to NULL. It is shown clipped. */
 PwGpuMessage d={.operation=PW_GPU_SWAPCHAIN,.address=0x240000000ull,.bytes=7684ull*1080,.argument={1921,1080,7684,1}};
 unsigned shown_w=0,shown_h=0;
 assert(pw_gpu_display_image(&d));pw_gpu_display_shown(&d,&shown_w,&shown_h);assert(shown_w==1920 && shown_h==1080);
 d.argument[0]=960;d.argument[1]=540;d.argument[2]=3840;d.bytes=3840ull*540;   /* smaller: whole, the display scales it */
 assert(pw_gpu_display_image(&d));pw_gpu_display_shown(&d,&shown_w,&shown_h);assert(shown_w==960 && shown_h==540);
 d.argument[0]=2176;d.argument[1]=1408;d.argument[2]=8704;d.bytes=8704ull*1408;assert(pw_gpu_display_image(&d));
 pw_gpu_display_shown(&d,&shown_w,&shown_h);assert(shown_w==1920 && shown_h==1080);
 d.argument[0]=2177;d.argument[2]=8708;d.bytes=8708ull*1408;assert(!pw_gpu_display_image(&d));
 d.argument[0]=2176;d.argument[1]=1409;d.argument[2]=8704;d.bytes=8704ull*1409;assert(!pw_gpu_display_image(&d));
 d.argument[1]=1080;d.bytes=8704ull*1080;assert(pw_gpu_display_image(&d));
 d.argument[2]=8700;d.bytes=8700ull*1080;assert(!pw_gpu_display_image(&d));   /* a row shorter than the image */
 d.argument[2]=8706;d.bytes=8706ull*1080;assert(!pw_gpu_display_image(&d));   /* a row not of whole pixels */
 d.argument[2]=8704;d.bytes=8704ull*1080+1;assert(!pw_gpu_display_image(&d));d.bytes--;
 d.argument[3]=4;assert(!pw_gpu_display_image(&d));d.argument[3]=PW_GPU_PRESENT_SHOWN;assert(pw_gpu_display_image(&d));d.argument[3]=0;
 d.address=0;assert(!pw_gpu_display_image(&d));d.address=0x240000002ull;assert(!pw_gpu_display_image(&d));
 d.address=0x240000000ull;d.argument[0]=0;assert(!pw_gpu_display_image(&d));

    puts("GPU broker bounds and owner PID/window packet contracts passed");
}
