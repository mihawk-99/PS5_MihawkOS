/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A Vulkan present loop from a Windows program, in each present mode, against the display's refresh
 * (src/pw_present_pacing.h has why). One full-screen window (game mode's direct route) or, with
 * PS5_PROTON_PRESENT_WINDOWED=1, a 1280x720 window (the composed route). For FIFO, MAILBOX and IMMEDIATE in turn: a
 * swapchain of 3 images, PS5_PROTON_PROBE_FRAMES frames (300) each cleared to a colour and presented with a present id;
 * in FIFO the loop then waits for the previous frame's present, as DXVK's presenter does. One line per mode:
 *   PS5_PROTON_VK_PRESENT mode=FIFO frames= refresh= fps_x10= max8_fps_x10= shown_us= waited= result=PASS|FAIL
 * FIFO passes held to the refresh with no 8 present waits faster than it; MAILBOX and IMMEDIATE pass at 1.5 times the
 * refresh or more. The last line is "PS5_PROTON_VK_PRESENT PASS" or "... FAIL <step>", which tools/run-console.py reads.
 * Built by tools/build-wine-probe.sh with clang, no C runtime. */
#include <windows.h>
#include "wine/vulkan.h"
#include "../src/pw_present_pacing.h"

enum { IMAGES = 3, MAX_FRAMES = 2000, SKIP = 30 };
int _fltused;

static HANDLE out;
static char line[512];
static unsigned used;
static uint64_t presented[MAX_FRAMES], shown[MAX_FRAMES];

static void add(const char *text) { while (*text && used < sizeof(line) - 1) line[used++] = *text++; line[used] = 0; }
static void add_number(unsigned long long value)
{
    char text[24];int n = 0;
    do text[n++] = (char)('0' + value % 10); while (value /= 10);
    while (n) { char c[2] = {text[--n], 0}; add(c); }
}
static void flush_line(void)
{
    DWORD written;add("\r\n");WriteFile(out, line, used, &written, NULL);OutputDebugStringA(line);used = 0;line[0] = 0;
}
static int fail(const char *step, VkResult result)
{
    add("PS5_PROTON_VK_PRESENT FAIL "); add(step); add(" result="); add_number((unsigned)result);
    flush_line();ExitProcess(1);return 1;
}
#define CHECK(step, call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) return fail(step, r_); } while (0)
static unsigned env_number(const char *name, unsigned fallback)
{
    char value[16];const DWORD n = GetEnvironmentVariableA(name, value, sizeof(value));
    if (!n || n >= sizeof(value)) return fallback;
    unsigned v = 0;for (DWORD i = 0; i < n && value[i] >= '0' && value[i] <= '9'; i++) v = v * 10 + (unsigned)(value[i] - '0');
    return v;
}
static uint64_t now_us(void)
{
    static LARGE_INTEGER frequency;LARGE_INTEGER t;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart / frequency.QuadPart * 1000000 + t.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart);
}
static void pump(void) { MSG m;while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m);DispatchMessageW(&m); } }

int mainCRTStartup(void)
{
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    const unsigned frames = env_number("PS5_PROTON_PROBE_FRAMES", 300) < MAX_FRAMES ? env_number("PS5_PROTON_PROBE_FRAMES", 300) : MAX_FRAMES;
    const int windowed = env_number("PS5_PROTON_PRESENT_WINDOWED", 0) == 1;
    DEVMODEW mode = {.dmSize = sizeof(mode)};
    const unsigned refresh = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1 ?
                             mode.dmDisplayFrequency : 60;
    WNDCLASSW window_class = {.lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"vk-present"};
    RegisterClassW(&window_class);
    const int width = windowed ? 1280 : 1920, height = windowed ? 720 : 1080;
    HWND window = CreateWindowExW(0, L"vk-present", L"vk-present", WS_POPUP | WS_VISIBLE, windowed ? 200 : 0, windowed ? 150 : 0,
                                  width, height, NULL, NULL, window_class.hInstance, NULL);
    if (!window) return fail("window", VK_ERROR_INITIALIZATION_FAILED);
    pump();

    HMODULE vulkan = LoadLibraryW(L"vulkan-1.dll");
    PFN_vkGetInstanceProcAddr get_proc = vulkan ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkan, "vkGetInstanceProcAddr") : NULL;
    if (!get_proc) return fail("load", VK_ERROR_INITIALIZATION_FAILED);
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)get_proc(NULL, "vkCreateInstance");
    const char *instance_extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vk-present-probe", 1, NULL, 0, VK_API_VERSION_1_2};
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 2, instance_extensions};
    VkInstance instance;
    CHECK("instance", vkCreateInstance(&instance_info, NULL, &instance));
#define INSTANCE_FN(name) PFN_##name name = (PFN_##name)get_proc(instance, #name)
    INSTANCE_FN(vkEnumeratePhysicalDevices); INSTANCE_FN(vkGetPhysicalDeviceQueueFamilyProperties);
    INSTANCE_FN(vkCreateDevice); INSTANCE_FN(vkGetDeviceProcAddr); INSTANCE_FN(vkCreateWin32SurfaceKHR);
    INSTANCE_FN(vkGetPhysicalDeviceSurfaceSupportKHR); INSTANCE_FN(vkGetPhysicalDeviceSurfacePresentModesKHR);
    uint32_t count = 1;VkPhysicalDevice physical;
    VkResult enumerated = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE) || !count) return fail("devices", enumerated);
    VkWin32SurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, NULL, 0, window_class.hInstance, window};
    VkSurfaceKHR surface;
    CHECK("surface", vkCreateWin32SurfaceKHR(instance, &surface_info, NULL, &surface));
    VkQueueFamilyProperties families[16];uint32_t family_count = 16, family = ~0u;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
    for (uint32_t i = 0; i < family_count && family == ~0u; i++) {
        VkBool32 supported = VK_FALSE;
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &supported) == VK_SUCCESS && supported) family = i;
    }
    if (family == ~0u) return fail("queue_family", VK_ERROR_FEATURE_NOT_PRESENT);
    VkPresentModeKHR offered[8];uint32_t offered_count = 8;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &offered_count, offered);
    add("vk-present refresh="); add_number(refresh); add(" windowed="); add_number(windowed); add(" offered=");
    for (uint32_t i = 0; i < offered_count; i++) { add_number(offered[i]); add(i + 1 < offered_count ? "," : ""); }
    flush_line();

    VkPhysicalDevicePresentWaitFeaturesKHR wait_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, NULL, VK_TRUE};
    VkPhysicalDevicePresentIdFeaturesKHR id_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &wait_features, VK_TRUE};
    const char *device_extensions[] = {"VK_KHR_swapchain", "VK_KHR_present_id", "VK_KHR_present_wait"};
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, family, 1, &priority};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &id_features, 0, 1, &queue_info, 0, NULL, 3, device_extensions, NULL};
    VkDevice device;
    CHECK("device", vkCreateDevice(physical, &device_info, NULL, &device));
#define DEVICE_FN(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(device, #name)
    DEVICE_FN(vkGetDeviceQueue); DEVICE_FN(vkCreateSwapchainKHR); DEVICE_FN(vkDestroySwapchainKHR);
    DEVICE_FN(vkGetSwapchainImagesKHR); DEVICE_FN(vkAcquireNextImageKHR); DEVICE_FN(vkQueuePresentKHR);
    DEVICE_FN(vkWaitForPresentKHR); DEVICE_FN(vkCreateCommandPool); DEVICE_FN(vkAllocateCommandBuffers);
    DEVICE_FN(vkBeginCommandBuffer); DEVICE_FN(vkEndCommandBuffer); DEVICE_FN(vkCmdPipelineBarrier);
    DEVICE_FN(vkCmdClearColorImage); DEVICE_FN(vkQueueSubmit); DEVICE_FN(vkCreateFence); DEVICE_FN(vkWaitForFences);
    DEVICE_FN(vkResetFences); DEVICE_FN(vkCreateSemaphore); DEVICE_FN(vkDeviceWaitIdle);
    VkQueue queue;vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
                                         VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, family};
    VkCommandPool pool;
    CHECK("pool", vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo command_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
                                                VK_COMMAND_BUFFER_LEVEL_PRIMARY, IMAGES};
    VkCommandBuffer commands[IMAGES];
    CHECK("commands", vkAllocateCommandBuffers(device, &command_info, commands));
    VkFence fences[IMAGES];VkSemaphore acquired[IMAGES], done[IMAGES];
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};

    const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    const char *const names[] = {"FIFO", "MAILBOX", "IMMEDIATE"};
    unsigned failed = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint64_t next_id = 0;
    for (unsigned m = 0; m < 3; m++) {
        for (unsigned i = 0; i < IMAGES; i++) {   /* fresh each mode: nothing of the last chain is waited on again */
            CHECK("fence", vkCreateFence(device, &fence_info, NULL, &fences[i]));
            CHECK("semaphore", vkCreateSemaphore(device, &semaphore_info, NULL, &acquired[i]));
            CHECK("semaphore", vkCreateSemaphore(device, &semaphore_info, NULL, &done[i]));
        }
        VkSwapchainCreateInfoKHR chain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, surface, IMAGES,
            VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, {(uint32_t)width, (uint32_t)height}, 1,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, modes[m], VK_TRUE, swapchain};
        VkSwapchainKHR old = swapchain;
        CHECK("swapchain", vkCreateSwapchainKHR(device, &chain_info, NULL, &swapchain));
        if (old) vkDestroySwapchainKHR(device, old, NULL);
        VkImage images[8];uint32_t image_count = 8;
        CHECK("images", vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));
        const int holds = modes[m] == VK_PRESENT_MODE_FIFO_KHR;
        for (unsigned f = 0; f < frames; f++) {
            const unsigned slot = f % IMAGES;
            CHECK("frame_fence", vkWaitForFences(device, 1, &fences[slot], VK_TRUE, 5000000000ull));
            CHECK("reset", vkResetFences(device, 1, &fences[slot]));
            uint32_t index;
            CHECK("acquire", vkAcquireNextImageKHR(device, swapchain, 5000000000ull, acquired[slot], VK_NULL_HANDLE, &index));
            VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
            CHECK("begin", vkBeginCommandBuffer(commands[slot], &begin));
            VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageMemoryBarrier to_clear = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                images[index], range};
            vkCmdPipelineBarrier(commands[slot], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_clear);
            VkClearColorValue colour = {.float32 = {(float)(f % 60) / 60.0f, 0.3f, (float)m / 3.0f, 1.0f}};
            vkCmdClearColorImage(commands[slot], images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
            VkImageMemoryBarrier to_present = to_clear;
            to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;to_present.dstAccessMask = 0;
            to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(commands[slot], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &to_present);
            CHECK("end", vkEndCommandBuffer(commands[slot]));
            const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &acquired[slot], &stage, 1, &commands[slot], 1, &done[index]};
            CHECK("submit", vkQueueSubmit(queue, 1, &submit, fences[slot]));
            const uint64_t id = ++next_id;
            VkPresentIdKHR present_id = {VK_STRUCTURE_TYPE_PRESENT_ID_KHR, NULL, 1, &id};
            VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, &present_id, 1, &done[index], 1, &swapchain, &index, NULL};
            CHECK("present", vkQueuePresentKHR(queue, &present));
            presented[f] = now_us();shown[f] = 0;
            if (holds && f) {   /* the previous frame's present, as DXVK's presenter waits for each frame */
                CHECK("present_wait", vkWaitForPresentKHR(device, swapchain, id - 1, 1000000000ull));
                shown[f - 1] = now_us();
            }
            if (!(f & 15)) pump();
        }
        CHECK("idle", vkDeviceWaitIdle(device));
        const PwPresentPacing pacing = pw_present_pacing(presented, shown, frames, SKIP);
        const int ok = pw_present_pacing_ok(&pacing, holds, refresh);
        failed += !ok;
        add("PS5_PROTON_VK_PRESENT mode="); add(names[m]); add(" frames="); add_number(frames);
        add(" refresh="); add_number(refresh); add(" fps_x10="); add_number(pacing.fps_x10);
        add(" max8_fps_x10="); add_number(pacing.max8_fps_x10); add(" shown_us="); add_number(pacing.shown_us);
        add(" waited="); add_number(pacing.waited); add(" result="); add(ok ? "PASS" : "FAIL");
        flush_line();
    }
    vkDestroySwapchainKHR(device, swapchain, NULL);
    if (failed) return fail("pacing", VK_ERROR_UNKNOWN);
    add("PS5_PROTON_VK_PRESENT PASS: FIFO held to the refresh, MAILBOX and IMMEDIATE free");
    flush_line();
    ExitProcess(0);
    return 0;
}
