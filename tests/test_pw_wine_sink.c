/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../wine/ps5/pw_wine_sink.h"
#include "../src/pw_audio_ring.h"
#include "../src/pw_shared_page.h"
#include "../src/pw_frame_page.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int order[4], ran;
static PwFramePage *received_frame;
static unsigned frame_allocations;
static int frame_allocate(size_t bytes)
{
    assert(bytes == PW_FRAME_BYTES);
    ++frame_allocations;
    return pw_shared_page_create(bytes);
}
int pw_ps5_child_frame(int fd)
{
    received_frame = mmap(NULL, PW_FRAME_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    return received_frame == MAP_FAILED ? -1 : 0;
}
static void first(void) { order[ran++] = 1; }
static void second(void) { order[ran++] = 2; }

static volatile int playing, release_grain, grains_played;
static int slow_sink(void *context, const int16_t *frames)
{
    (void)context; (void)frames;
    playing = 1;
    while (!release_grain) usleep(1000);
    grains_played++;
    return 0;
}
/* The title's sink for the mixer test: keeps each grain's first sample. While gate is closed it holds the mixer
 * after the grain it took, so a test can write several rings before the mixer reads any of them. */
static volatile int mixed_count, gate_open = 1;
static int16_t mixed_first[64];
static int capture_sink(void *context, const int16_t *frames)
{
    (void)context;
    if (mixed_count < 64) mixed_first[mixed_count] = frames[0];
    mixed_count++;
    while (!gate_open) usleep(1000);
    usleep(1000);   /* a port takes its time */
    return 0;
}
static void fill(int16_t *frames, int16_t value)
{
    for (int i = 0; i < PW_WINE_AUDIO_GRAIN * 2; i++) frames[i] = value;
}

/* A helper process, started first: it makes its input page (src/pw_shared_page.h), gives it to the title, and reads
 * the DualSense report the title set, as winebus's PS5 bus in a helper would. */
static int helper_socket = -1;
static pid_t dualsense_helper;
static void send_fd(int socket_fd, int fd)
{
    char byte = 1, control[CMSG_SPACE(sizeof(int))];
    memset(control, 0, sizeof(control));
    struct iovec io = {&byte, 1};
    struct msghdr message = {0};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    assert(sendmsg(socket_fd, &message, 0) == 1);
}
static int receive_fd(int socket_fd)
{
    char byte, control[CMSG_SPACE(sizeof(int))];
    struct iovec io = {&byte, 1};
    struct msghdr message = {0};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    if (recvmsg(socket_fd, &message, 0) != 1) return -1;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_type != SCM_RIGHTS) return -1;
    int fd;
    memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    return fd;
}
static void start_dualsense_helper(void)
{
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    dualsense_helper = fork();
    assert(dualsense_helper >= 0);
    if (dualsense_helper) {
        close(pair[1]);
        helper_socket = pair[0];
        return;
    }
    close(pair[0]);
    const int fd = pw_shared_page_create(PW_WINE_INPUT_PAGE_BYTES);
    if (fd < 0) _exit(2);
    send_fd(pair[1], fd);
    char value[16];
    snprintf(value, sizeof(value), "%d", fd);
    setenv("WINE_PS5_INPUT_FD", value, 1);
    uint8_t report[64];
    for (int i = 0; i < 2000; i++) {
        if (pw_wine_dualsense(report) && report[0] == 0x01 && report[1] == 0x7f && report[63] == 0x5a) _exit(0);
        usleep(1000);
    }
    _exit(4);
}

/* A second helper, for the pad: it makes its input page, takes the first look at the pad (a baseline), and then looks
 * every millisecond until it has seen the pad connect and disconnect. */
static int pad_helper_socket = -1;
static pid_t pad_helper;
static void start_pad_helper(void)
{
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    pad_helper = fork();
    assert(pad_helper >= 0);
    if (pad_helper) {
        close(pair[1]);
        pad_helper_socket = pair[0];
        return;
    }
    close(pair[0]);
    const int fd = pw_shared_page_create(PW_WINE_INPUT_PAGE_BYTES);
    if (fd < 0) _exit(2);
    send_fd(pair[1], fd);
    char value[16];
    snprintf(value, sizeof(value), "%d", fd);
    setenv("WINE_PS5_INPUT_FD", value, 1);
    const uint32_t first = pw_wine_pad_changes();
    if (write(pair[1], "B", 1) != 1) _exit(3);
    uint32_t seen = first;
    for (int i = 0; i < 60000 && seen < first + 2; i++) {
        usleep(1000);
        seen = pw_wine_pad_changes();
    }
    _exit(seen == first + 2 ? 0 : 4);
}

static void *play(void *unused)
{
    static int16_t grain[PW_WINE_AUDIO_GRAIN * 2];
    (void)unused;
    pw_wine_audio_output(grain);
    return NULL;
}

/* The launcher that switches chapters: its own mixer keeps playing in the title while the game it started plays
 * through a helper's ring. */
static void *launcher_plays(void *count)
{
    int16_t frames[PW_WINE_AUDIO_GRAIN * 2];
    fill(frames, 100);
    for (int i = 0; i < *(int *)count; i++) pw_wine_audio_output(frames);
    return NULL;
}
int main(void)
{
    start_dualsense_helper();
    start_pad_helper();
    /* the title's close and terminate requests travel like input */
    PwWineInput event = {.type = PW_WINE_INPUT_CLOSE}, out;
    assert(!pw_wine_post_input(&event));
    event.type = PW_WINE_INPUT_TERMINATE;
    assert(!pw_wine_post_input(&event));
    event.type = PW_WINE_INPUT_END_PROCESS + 1;
    assert(pw_wine_post_input(&event) == -1);
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_CLOSE);
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_TERMINATE);
    /* Typed text arrives as UTF-16 units (patch 2424): a press and a release carry the unit; no unit, a unit past 16 bits
     * and a state past 1 are refused */
    PwWineInput unit = {PW_WINE_INPUT_CHAR, 0xe9, 0, 0, 1};
    assert(!pw_wine_post_input(&unit));
    unit.down = 0;
    assert(!pw_wine_post_input(&unit));
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_CHAR && out.code == 0xe9 && out.down == 1);
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_CHAR && out.code == 0xe9 && out.down == 0);
    unit.code = 0;
    assert(pw_wine_post_input(&unit) == -1);
    unit.code = 0x10000;
    assert(pw_wine_post_input(&unit) == -1);
    unit.code = 'a';
    unit.down = 2;
    assert(pw_wine_post_input(&unit) == -1);
    /* Keys carry a scan code and the extended flag (a USB keyboard's Right arrow); a key without a virtual key, a
     * button past X2 and a code past the flag are refused. */
    PwWineInput key = {PW_WINE_INPUT_KEY, 0x27 | 0x4d << PW_WINE_KEY_SCAN_SHIFT | PW_WINE_KEY_EXTENDED, 0, 0, 1};
    assert(!pw_wine_post_input(&key));
    key.code = 0x4d << PW_WINE_KEY_SCAN_SHIFT;
    assert(pw_wine_post_input(&key) == -1);
    key.code = 0x27 | PW_WINE_KEY_EXTENDED << 1;
    assert(pw_wine_post_input(&key) == -1);
    PwWineInput button = {PW_WINE_INPUT_MOUSE_BUTTON, 4, 0, 0, 1}, wheel = {PW_WINE_INPUT_MOUSE_WHEEL, 0, 0, -240, 0};
    assert(!pw_wine_post_input(&button) && !pw_wine_post_input(&wheel));
    button.code = 5;
    assert(pw_wine_post_input(&button) == -1);
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_KEY && out.code == (0x27 | 0x4d << 8 | 0x10000));
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_MOUSE_BUTTON && out.code == 4);
    assert(pw_wine_next_input(&out) && out.type == PW_WINE_INPUT_MOUSE_WHEEL && out.y == -240);
    assert(!pw_wine_next_input(&out));

    /* exit hooks: newest first, once, none added after they ran */
    assert(!pw_wine_add_exit_hook(first) && !pw_wine_add_exit_hook(second));
    assert(pw_wine_add_exit_hook(NULL) == -1);
    pw_wine_run_exit_hooks();
    pw_wine_run_exit_hooks();
    assert(ran == 2 && order[0] == 2 && order[1] == 1);
    assert(pw_wine_add_exit_hook(first) == -1);

    /* thread-exit hooks: run on every thread that ends, newest first, as often as threads end */
    ran = 0;
    pw_wine_run_thread_exit_hooks(); /* none registered: nothing runs */
    assert(ran == 0);
    assert(!pw_wine_add_thread_exit_hook(first) && !pw_wine_add_thread_exit_hook(second));
    assert(pw_wine_add_thread_exit_hook(NULL) == -1);
    pw_wine_run_thread_exit_hooks();
    pw_wine_run_thread_exit_hooks();
    assert(ran == 4 && order[0] == 2 && order[1] == 1);
    for (int i = 2; i < PW_WINE_THREAD_EXIT_HOOKS; i++) assert(!pw_wine_add_thread_exit_hook(first));
    assert(pw_wine_add_thread_exit_hook(first) == -1);

    /* stopping audio waits for a grain being played, and times out on one
     * that does not finish */
    pthread_t player;
    static int16_t grain[PW_WINE_AUDIO_GRAIN * 2];
    pw_wine_set_audio_sink(slow_sink, NULL);
    pthread_create(&player, NULL, play, NULL);
    while (!playing) usleep(1000);
    assert(pw_wine_stop_audio(50) == -1);
    release_grain = 1;
    pthread_join(player, NULL);
    assert(grains_played == 1);
    assert(!pw_wine_stop_audio(50));
    assert(!pw_wine_audio_available());
    assert(pw_wine_audio_output(grain) == -1);    /* no sink: dropped, nothing plays */
    assert(grains_played == 1);
    /* A helper's input page, adopted by the title: written at once with an even sequence, rewritten when the pad
     * changes. */
    {
        struct { uint32_t sequence, reserved; PwWinePad pad; uint32_t dualsense_connected; uint8_t dualsense[64]; } *page;
        const PwWinePad held = {1, 0, 0x1000, 10, 20, 100, -100, 200, -200};
        const int fd = receive_fd(helper_socket);
        assert(fd >= 0);
        page = pw_wine_input_page_adopt(fd);
        close(fd);
        assert(page && page->sequence == 2);
        const uint32_t before = page->sequence;
        pw_wine_set_pad(&held);
        assert(page->sequence == before + 2 && page->pad.connected && page->pad.buttons == 0x1000 &&
               page->pad.thumb_ry == -200 && page->pad.packet != 0);
        PwWinePad read;
        assert(pw_wine_pad(&read) && read.buttons == 0x1000);   /* the title reads its own state, not the page */
        pw_wine_set_pad(NULL);
        assert(!page->pad.connected && !(page->sequence & 1));
        /* The DualSense report rides on the page too: the helper started first reads it from there. */
        uint8_t report[64] = {0};
        report[0] = 0x01; report[1] = 0x7f; report[63] = 0x5a;
        pw_wine_set_dualsense(report);
        assert(page->dualsense_connected == 1 && page->dualsense[1] == 0x7f && !(page->sequence & 1));
        int status = 0;
        assert(waitpid(dualsense_helper, &status, 0) == dualsense_helper && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        pw_wine_set_dualsense(NULL);
        assert(page->dualsense_connected == 0);
        pw_wine_input_page_release(page);
        assert(pw_wine_input_page_adopt(-1) == NULL);
    }
    /* A helper (started first, before this process set a pad) saw the pad through its input page: its first look was the
     * baseline, and each later look that finds the pad connected, then gone, counts. */
    {
        const int fd = receive_fd(pad_helper_socket);
        assert(fd >= 0);
        void *page = pw_wine_input_page_adopt(fd);
        assert(page);
        char ready;
        assert(read(pad_helper_socket, &ready, 1) == 1 && ready == 'B');
        PwWinePad pad = {.connected = 1};
        pw_wine_set_pad(&pad);
        usleep(20000);   /* the helper looks every millisecond: it has seen the pad */
        pw_wine_set_pad(NULL);
        int status = 0;
        assert(waitpid(pad_helper, &status, 0) == pad_helper && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        pw_wine_input_page_release(page);
        close(pad_helper_socket);
    }
    /* The pad connecting or disconnecting is counted, and wakes the driver, which tells the game's windows
     * (WM_DEVICECHANGE); the same state again, a changed button or a second disconnect is not a change. */
    {
        const uint32_t before = pw_wine_pad_changes();
        PwWinePad pad = {.connected = 1, .buttons = 1};
        char byte[8];
        while (read(pw_wine_input_fd(), byte, sizeof(byte)) > 0) {}   /* the fd is non-blocking: empty it */
        pw_wine_set_pad(NULL);   /* disconnected already, or the first look: no change */
        const uint32_t base = pw_wine_pad_changes();
        assert(base == before || base == before + 1);
        pw_wine_set_pad(&pad);
        assert(pw_wine_pad_changes() == base + 1);
        assert(read(pw_wine_input_fd(), byte, sizeof(byte)) == 1);   /* woken once */
        pad.buttons = 2;
        pw_wine_set_pad(&pad);
        pw_wine_set_pad(&pad);
        assert(pw_wine_pad_changes() == base + 1 && read(pw_wine_input_fd(), byte, sizeof(byte)) < 0);
        pw_wine_set_pad(NULL);
        pw_wine_set_pad(NULL);
        assert(pw_wine_pad_changes() == base + 2 && read(pw_wine_input_fd(), byte, sizeof(byte)) == 1);
        pw_wine_set_pad(&pad);
        assert(pw_wine_pad_changes() == base + 3);
        pw_wine_set_pad(NULL);
        assert(pw_wine_pad_changes() == base + 4);
        while (read(pw_wine_input_fd(), byte, sizeof(byte)) > 0) {}
    }
    /* A helper's audio ring, adopted by the title: the helper plays into it, and the title's mixer plays each grain
     * through the sink, in order. (With no sink the mixer takes grains anyway and drops them, so that a helper's audio
     * clock keeps going: the sink is set first.) */
    {
        const int fd = pw_shared_page_create(PW_WINE_AUDIO_RING_BYTES);   /* the helper's loader makes it */
        assert(fd >= 0);
        mixed_count = 0;
        pw_wine_set_audio_sink(capture_sink, NULL);
        void *title_side = pw_wine_audio_ring_adopt(fd);
        assert(title_side);
        const pid_t helper = fork();
        assert(helper >= 0);
        if (!helper) {
            char value[16];
            int16_t frames[PW_WINE_AUDIO_GRAIN * 2];
            pw_wine_set_audio_sink(NULL, NULL);   /* a helper has no sink of its own: it plays into its ring */
            snprintf(value, sizeof(value), "%d", fd);
            setenv("WINE_PS5_AUDIO_FD", value, 1);
            if (!pw_wine_audio_available()) _exit(2);
            for (int16_t v = 1; v <= 3; v++) {
                fill(frames, v);
                if (pw_wine_audio_output(frames)) _exit(3);
            }
            _exit(0);
        }
        int status = 0;
        assert(waitpid(helper, &status, 0) == helper && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        for (int i = 0; i < 2000 && mixed_count < 3; i++) usleep(1000);
        assert(mixed_count == 3 && mixed_first[0] == 1 && mixed_first[1] == 2 && mixed_first[2] == 3);
        /* Two rings play at once: their grains are summed, and clamped. The mixer is held in the sink after a first
         * grain while both rings are written, so it reads them together. */
        const int second_fd = pw_shared_page_create(PW_WINE_AUDIO_RING_BYTES);
        assert(second_fd >= 0);
        void *second = pw_wine_audio_ring_adopt(second_fd);
        assert(second);
        int16_t frames[PW_WINE_AUDIO_GRAIN * 2];
        mixed_count = 0;
        gate_open = 0;
        fill(frames, 7);
        assert(pw_audio_ring_write(title_side, frames, 0, NULL, NULL) == PW_OK);
        for (int i = 0; i < 2000 && mixed_count < 1; i++) usleep(1000);
        assert(mixed_count == 1 && mixed_first[0] == 7);   /* the mixer now waits in the sink */
        fill(frames, 1000);
        assert(pw_audio_ring_write(title_side, frames, 0, NULL, NULL) == PW_OK);
        fill(frames, 2000);
        assert(pw_audio_ring_write(second, frames, 0, NULL, NULL) == PW_OK);
        fill(frames, 32000);
        assert(pw_audio_ring_write(title_side, frames, 0, NULL, NULL) == PW_OK);
        assert(pw_audio_ring_write(second, frames, 0, NULL, NULL) == PW_OK);
        gate_open = 1;
        for (int i = 0; i < 2000 && mixed_count < 3; i++) usleep(1000);
        assert(mixed_count == 3 && mixed_first[1] == 3000 && mixed_first[2] == 32767);
        pw_wine_audio_ring_release(title_side);
        pw_wine_audio_ring_release(second);
        close(fd);
        close(second_fd);
        assert(!pw_wine_stop_audio(500));
    }
    /* Open, close, reopen in one process, with two players at once (Deltarune's game_change): the title's own mixer and
     * a helper's ring play together. The sink must see each grain once, the two summed, not the title's and the
     * helper's alternating, which halves the helper's rate and plays every second grain as silence or the other's. */
    {
        const int fd = pw_shared_page_create(PW_WINE_AUDIO_RING_BYTES);
        assert(fd >= 0);
        pw_wine_set_audio_sink(capture_sink, NULL);
        for (int round = 0; round < 2; round++) {   /* open, close, reopen */
            mixed_count = 0;
            void *ring = pw_wine_audio_ring_adopt(fd);
            assert(ring);
            int launcher_grains = 40;
            pthread_t launcher;
            int16_t frames[PW_WINE_AUDIO_GRAIN * 2];
            fill(frames, 1000);
            for (int i = 0; i < 8; i++) assert(pw_audio_ring_write(ring, frames, 0, NULL, NULL) == PW_OK);
            assert(!pthread_create(&launcher, NULL, launcher_plays, &launcher_grains));
            for (int i = 0; i < 32; i++) {   /* the helper's writer, in step with the port */
                for (int w = 0; w < 2000 && pw_audio_ring_write(ring, frames, 0, NULL, NULL) != PW_OK; w++) usleep(500);
            }
            pthread_join(launcher, NULL);
            for (int i = 0; i < 2000 && pw_audio_ring_fill(ring) > 0; i++) usleep(1000);
            usleep(5000);
            int both = 0;
            for (int i = 0; i < mixed_count && i < 64; i++) {
                both += mixed_first[i] == 1100;
            }
            /* before the fix: 40 launcher-only grains and 40 ring-only ones, none summed */
            assert(both >= 20);
            assert(mixed_count < 60);
            /* closed while one of the title's own grains still waits: it and those after it reach the sink in order */
            gate_open = 0;
            mixed_count = 0;
            fill(frames, 5);
            assert(!pw_wine_audio_output(frames));   /* through the mixer: the ring is still adopted */
            pw_wine_audio_ring_release(ring);
            gate_open = 1;
            for (int16_t v = 6; v <= 8; v++) { fill(frames, v); assert(!pw_wine_audio_output(frames)); }
            for (int i = 0; i < 2000 && mixed_count < 4; i++) usleep(1000);
            assert(mixed_count == 4 && mixed_first[0] == 5 && mixed_first[1] == 6 && mixed_first[2] == 7 && mixed_first[3] == 8);
        }
        close(fd);
        assert(!pw_wine_stop_audio(500));
    }
    /* Frame allocation is native-owned, transferred once, and retained across
     * frames. The receiver's alias sees the payload's pixels after fd close. */
    pw_wine_set_present_sink(NULL, NULL);
    pw_wine_set_frame_allocator(frame_allocate);
    assert(!setenv("WINE_PS5_CHILD", "1", 1));
    const uint32_t pixels[2] = {0xff125062u, 0xff625012u};
    assert(!pw_wine_present(pixels, 2, 1, sizeof(pixels)));
    assert(frame_allocations == 1 && received_frame && received_frame != MAP_FAILED);
    assert(received_frame->magic == PW_FRAME_MAGIC && received_frame->width == 2 && received_frame->height == 1);
    assert(!memcmp(pw_frame_pixels(received_frame), pixels, sizeof(pixels)));
    assert(!pw_wine_present(pixels, 2, 1, sizeof(pixels)) && frame_allocations == 1 && received_frame->frames == 2);
    assert(!munmap(received_frame, PW_FRAME_BYTES));
    puts("pw_wine_sink: ok");
    return 0;
}
