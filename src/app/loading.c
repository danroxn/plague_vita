#include "app/application.h"

#include "app/log.h"
#include "app/loading_indicator.h"
#include "app/loading_png.h"
#include "expected.h"
#include "runtime/text_cache.h"
#include "vita/device.h"
#include "vita/time.h"

#include <ctype.h>
#include <pthread.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

// Standard 5x7 font for loading-screen text.
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:/_-+()";
static const unsigned char glyphs[][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},   {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {0, 0, 0, 0, 0, 0, 0},        {0, 0, 0, 0, 0, 12, 12},      {0, 12, 12, 0, 12, 12, 0},
    {1, 2, 2, 4, 8, 8, 16},       {0, 0, 0, 0, 0, 0, 31},       {0, 0, 0, 31, 0, 0, 0},
    {0, 4, 4, 31, 4, 4, 0},       {2, 4, 8, 8, 8, 4, 2},        {8, 4, 2, 2, 2, 4, 8}};

static void draw_text(uint32_t *pixels, unsigned x, unsigned y, unsigned scale, const char *text) {
    for (; *text; text++, x += scale * 6) {
        int character = toupper((unsigned char)*text);
        const char *glyph = strchr(alphabet, character);
        if (!glyph)
            continue;
        unsigned index = glyph - alphabet;
        for (unsigned row = 0; row < 7; row++) {
            for (unsigned column = 0; column < 5; column++) {
                if (!(glyphs[index][row] & (1U << (4 - column))))
                    continue;
                for (unsigned dy = 0; dy < scale; dy++)
                    for (unsigned dx = 0; dx < scale; dx++)
                        pixels[(y + row * scale + dy) * PORT_DISPLAY_WIDTH + x + column * scale +
                               dx] = 0xffe6eeee;
            }
        }
    }
}

enum {
    FRAME_PIXELS = PORT_DISPLAY_WIDTH * PORT_DISPLAY_HEIGHT,
    FRAME_BYTES = FRAME_PIXELS * sizeof(uint32_t),
    FRAME_ALIGNMENT = 4096,
};
_Static_assert(FRAME_BYTES % FRAME_ALIGNMENT == 0, "Loading framebuffer page alignment");
static struct {
    void *allocation;
    uint32_t *frames[2];
    PortLoadingIndicator *indicator;
    pthread_t thread;
    int running;
    int first_frame_presented;
    unsigned back_buffer;
    unsigned notice_buffers;
    uint64_t started_us;
    GLuint dialog_texture;
    GLuint dialog_framebuffer;
    unsigned frame_count, over_budget, late_frames;
    uint64_t render_total_us, render_max_us, interval_max_us;
    uint64_t first_present_us, last_present_us;
} loading;

static void draw_loading_frame(uint32_t *pixels, unsigned buffer, uint64_t frame_start_us) {
    if (!(loading.notice_buffers & (1U << buffer)) && port_text_cache_needs_warmup()) {
        static const char notice[] = "FIRST START TAKES LONGER. PLEASE WAIT.";
        unsigned x = (PORT_DISPLAY_WIDTH - (sizeof(notice) - 1) * 12) / 2;
        // This strip lies below the indicator. Draw only into the presenter's
        // own back buffer; it remains intact through animation and dialogs.
        draw_text(pixels, x, PORT_DISPLAY_HEIGHT - 56, 2, notice);
        loading.notice_buffers |= 1U << buffer;
    }
    port_loading_indicator_draw(loading.indicator, pixels,
                                (frame_start_us - loading.started_us) / 1000000.0f, buffer);
}

static void *present_loading(void *unused) {
    (void)unused;
    while (__atomic_load_n(&loading.running, __ATOMIC_ACQUIRE)) {
        unsigned back = loading.back_buffer;
        uint32_t *pixels = loading.frames[back];
        uint64_t frame_start_us = port_vita_time_us();
        draw_loading_frame(pixels, back, frame_start_us);
        uint64_t render_us = port_vita_time_us() - frame_start_us;
        loading.render_total_us += render_us;
        if (render_us > loading.render_max_us)
            loading.render_max_us = render_us;
        loading.over_budget += render_us > 16667;
        SceDisplayFrameBuf framebuffer = {
            .size = sizeof(framebuffer),
            .base = pixels,
            .pitch = PORT_DISPLAY_WIDTH,
            .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,
            .width = PORT_DISPLAY_WIDTH,
            .height = PORT_DISPLAY_HEIGHT,
        };
        int result = sceDisplaySetFrameBuf(&framebuffer, SCE_DISPLAY_SETBUF_NEXTFRAME);
        if (result < 0)
            port_fail("Loading framebuffer rejected: %#x", (unsigned)result);
        result = sceDisplayWaitSetFrameBuf();
        if (result < 0)
            port_fail("Loading display wait failed: %#x", (unsigned)result);
        uint64_t presented_us = port_vita_time_us();
        if (loading.frame_count) {
            uint64_t interval_us = presented_us - loading.last_present_us;
            if (interval_us > loading.interval_max_us)
                loading.interval_max_us = interval_us;
            loading.late_frames += interval_us > 25000;
        } else
            loading.first_present_us = presented_us;
        loading.last_present_us = presented_us;
        loading.frame_count++;
        loading.back_buffer ^= 1;
        __atomic_store_n(&loading.first_frame_presented, 1, __ATOMIC_RELEASE);
    }
    return NULL;
}

static void start_presenter(void) {
    __atomic_store_n(&loading.first_frame_presented, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&loading.running, 1, __ATOMIC_RELEASE);
    int result = pthread_create(&loading.thread, NULL, present_loading, NULL);
    if (result)
        port_fail("Cannot start loading presenter: %d", result);
    while (!__atomic_load_n(&loading.first_frame_presented, __ATOMIC_ACQUIRE))
        sceKernelDelayThread(1000);
}

static void stop_presenter(void) {
    __atomic_store_n(&loading.running, 0, __ATOMIC_RELEASE);
    int result = pthread_join(loading.thread, NULL);
    if (result)
        port_fail("Cannot join loading presenter: %d", result);
}

void port_app_loading_start(void) {
    loading.allocation = vglAlloc(2 * FRAME_BYTES + FRAME_ALIGNMENT - 1, VGL_MEM_VRAM);
    if (!loading.allocation)
        port_fail("Cannot allocate loading framebuffers");
    uintptr_t aligned =
        ((uintptr_t)loading.allocation + FRAME_ALIGNMENT - 1) & ~(uintptr_t)(FRAME_ALIGNMENT - 1);
    loading.frames[0] = (uint32_t *)aligned;
    loading.frames[1] = loading.frames[0] + FRAME_PIXELS;
    const char *error = port_loading_png_read("app0:/sce_sys/pic0.png", loading.frames[0],
                                              PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT);
    if (error)
        port_fail("Cannot load loading background: %s", error);
    const char *build_label = !strcmp(PORT_GIT_COMMIT, "DIRTY") || strstr(PORT_GIT_COMMIT, "-dirty")
                                 ? PORT_BUILD_ID
                                 : PORT_GIT_COMMIT;
    draw_text(loading.frames[0], 24, PORT_DISPLAY_HEIGHT - 28, 2, build_label);
    memcpy(loading.frames[1], loading.frames[0], FRAME_BYTES);
    loading.indicator = port_loading_indicator_create(loading.frames[0]);
    if (!loading.indicator)
        port_fail("Cannot allocate loading rasterizer");
    loading.started_us = port_vita_time_us();
    start_presenter();
}

void port_app_loading_pause(void) {
    stop_presenter();
    GLint previous_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGenTextures(1, &loading.dialog_texture);
    if (!loading.dialog_texture)
        port_fail("Cannot create loading dialog texture");
    glBindTexture(GL_TEXTURE_2D, loading.dialog_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, loading.frames[loading.back_buffer ^ 1]);
    if (!vglGetTexDataPointer(GL_TEXTURE_2D))
        port_fail("Cannot allocate loading dialog texture");
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
    glGenFramebuffers(1, &loading.dialog_framebuffer);
    if (!loading.dialog_framebuffer)
        port_fail("Cannot create loading dialog framebuffer");
    glNamedFramebufferTexture(loading.dialog_framebuffer, GL_COLOR_ATTACHMENT0,
                              loading.dialog_texture, 0);
    if (glCheckNamedFramebufferStatus(loading.dialog_framebuffer, GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
        port_fail("Incomplete loading dialog framebuffer");
}

void port_app_loading_draw_dialog(void) {
    uint32_t *pixels = loading.frames[loading.back_buffer];
    draw_loading_frame(pixels, loading.back_buffer, port_vita_time_us());
    glFinish();
    glTextureSubImage2D(loading.dialog_texture, 0, 0, 0, PORT_DISPLAY_WIDTH,
                        PORT_DISPLAY_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBlitNamedFramebuffer(loading.dialog_framebuffer, 0, 0, PORT_DISPLAY_HEIGHT,
                           PORT_DISPLAY_WIDTH, 0, 0, 0, PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

static void wait_for_display_handover(const char *queue_stage, const char *scanout_stage) {
    int result = sceGxmDisplayQueueFinish();
    if (result < 0)
        port_fail("Loading %s handover failed: %#x", queue_stage, (unsigned)result);
    result = sceDisplayWaitSetFrameBuf();
    if (result < 0)
        port_fail("Loading %s handover failed: %#x", scanout_stage, (unsigned)result);
}

void port_app_loading_resume(void) {
    wait_for_display_handover("dialog queue", "dialog scanout");
    glDeleteFramebuffers(1, &loading.dialog_framebuffer);
    glDeleteTextures(1, &loading.dialog_texture);
    loading.dialog_framebuffer = 0;
    loading.dialog_texture = 0;
    start_presenter();
}

void port_app_loading_handover(int dialog_active) {
    stop_presenter();
    vglSwapBuffers(dialog_active ? GL_TRUE : GL_FALSE);
    wait_for_display_handover("display", "scanout");
    vglFree(loading.allocation);
    loading.allocation = NULL;
    port_loading_indicator_destroy(loading.indicator);
    loading.indicator = NULL;
    uint64_t span_us = loading.last_present_us - loading.first_present_us;
    port_log_debug(
        "PERF loading frames=%u fps=%.2f render_avg_us=%llu render_max_us=%llu "
        "over_budget=%u present_max_us=%llu present_late=%u",
        loading.frame_count, span_us ? (loading.frame_count - 1) * 1000000.0 / span_us : 0.0,
        (unsigned long long)(loading.frame_count ? loading.render_total_us / loading.frame_count
                                                 : 0),
        (unsigned long long)loading.render_max_us, loading.over_budget,
        (unsigned long long)loading.interval_max_us, loading.late_frames);
}
