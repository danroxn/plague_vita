#include "compat/import_table.h"

#include "app/log.h"
#include "app/options.h"
#include "app/performance.h"
#include "runtime/texture_cache.h"
#include "vita/time.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

static struct {
    const unsigned char *point_sizes;
    GLenum point_type;
    GLsizei point_stride;
    int point_array_enabled;
    GLuint array_buffer;
    GLuint element_buffer;
} graphics_state;
enum { POINT_SIZE_ARRAY_OES = 0x8b9c };
static void compat_bind_buffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER)
        graphics_state.array_buffer = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER)
        graphics_state.element_buffer = buffer;
    glBindBuffer(target, buffer);
}
static void compat_point_size(GLenum type, GLsizei stride, const void *pointer) {
    if (graphics_state.array_buffer)
        port_fail("VBO-backed point size arrays need an adapter");
    if (type != GL_FLOAT && type != GL_FIXED)
        port_fail("Unsupported point size type 0x%x", type);
    graphics_state.point_sizes = pointer;
    graphics_state.point_type = type;
    graphics_state.point_stride = stride ? stride : 4;
}
static void compat_enable_client(GLenum value) {
    if (value == POINT_SIZE_ARRAY_OES)
        graphics_state.point_array_enabled = 1;
    else
        glEnableClientState(value);
}
static void compat_disable_client(GLenum value) {
    if (value == POINT_SIZE_ARRAY_OES)
        graphics_state.point_array_enabled = 0;
    else
        glDisableClientState(value);
}

static float point_value(const void *sizes, size_t stride_bytes, unsigned index,
                         int is_fixed_point) {
    const char *element_address = (const char *)sizes + (size_t)index * stride_bytes;
    if (is_fixed_point) {
        int32_t value;
        memcpy(&value, element_address, 4);
        return value / 65536.0f;
    }
    float value;
    memcpy(&value, element_address, 4);
    return value;
}
static unsigned point_index(const void *indices, size_t index_size_bytes, size_t index) {
    if (!indices)
        return (unsigned)index;
    unsigned value = 0;
    memcpy(&value, (const char *)indices + index * index_size_bytes, index_size_bytes);
    return value;
}

static size_t point_run(const void *sizes, size_t stride_bytes, int is_fixed_point,
                        const void *indices, size_t index_size_bytes, size_t start, size_t count,
                        float *point_size) {
    *point_size = point_value(sizes, stride_bytes, point_index(indices, index_size_bytes, start),
                              is_fixed_point);
    size_t length = 1;
    while (length < count &&
           point_value(sizes, stride_bytes, point_index(indices, index_size_bytes, start + length),
                       is_fixed_point) == *point_size)
        length++;
    return length;
}

static void compat_draw_arrays(GLenum mode, GLint first, GLsizei count) {
    if (mode != GL_POINTS || !graphics_state.point_array_enabled) {
        port_perf_draw(count > 0 ? count : 0);
        glDrawArrays(mode, first, count);
        return;
    }
    if (!graphics_state.point_sizes)
        port_fail("Missing enabled point size array");
    if (first < 0 || (count > 0 && first > INT_MAX - (count - 1)))
        port_fail("Invalid point array range");
    for (GLsizei point_offset = 0; point_offset < count;) {
        float size;
        size_t run_length = point_run(graphics_state.point_sizes, graphics_state.point_stride,
                                      graphics_state.point_type == GL_FIXED, NULL, 0,
                                      (size_t)first + point_offset, count - point_offset, &size);
        glPointSize(size);
        port_perf_draw(run_length);
        glDrawArrays(mode, first + point_offset, run_length);
        point_offset += run_length;
    }
}
static void compat_draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (mode != GL_POINTS || !graphics_state.point_array_enabled) {
        port_perf_draw(count > 0 ? count : 0);
        glDrawElements(mode, count, type, indices);
        return;
    }
    if (graphics_state.element_buffer)
        port_fail("VBO-backed point indices need an adapter");
    size_t index_width = type == GL_UNSIGNED_BYTE    ? 1
                         : type == GL_UNSIGNED_SHORT ? 2
                         : type == GL_UNSIGNED_INT   ? 4
                                                     : 0;
    if (!index_width)
        port_fail("Invalid point indices type");
    if (!graphics_state.point_sizes)
        port_fail("Missing enabled point size array");
    for (GLsizei point_offset = 0; point_offset < count;) {
        float size;
        size_t run_length = point_run(graphics_state.point_sizes, graphics_state.point_stride,
                                      graphics_state.point_type == GL_FIXED, indices, index_width,
                                      point_offset, count - point_offset, &size);
        glPointSize(size);
        port_perf_draw(run_length);
        glDrawElements(mode, run_length, type,
                       (const char *)indices + (size_t)point_offset * index_width);
        point_offset += run_length;
    }
}
static void compat_finish(void) {
    uint64_t started_at_us = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    glFinish();
    if (port_log_enabled(PORT_LOG_DEBUG))
        port_perf_finish(port_vita_time_us() - started_at_us);
}
static void check_texture_storage(GLenum target, GLint level, GLsizei width, GLsizei height) {
    if (target != GL_TEXTURE_2D || level != 0 || width <= 0 || height <= 0)
        return;
    void *pixels = vglGetTexDataPointer(target);
    if (!pixels)
        port_fail("GPU texture allocation failed: %dx%d ram_free=%u phycont_free=%u cdram_free=%u",
                  width, height, (unsigned)vglMemFree(VGL_MEM_RAM),
                  (unsigned)vglMemFree(VGL_MEM_PHYCONT), (unsigned)vglMemFree(VGL_MEM_VRAM));
}
static void compat_tex_image(GLenum target, GLint level, GLint internal, GLsizei width,
                             GLsizei height, GLint border, GLenum format, GLenum type,
                             const void *data) {
    int cache_dxt5 = target == GL_TEXTURE_2D && border == 0 &&
        port_app_options.retina_enabled && level == 0 && data && width == 2048 &&
        (height == 1024 || height == 2048) && internal == GL_RGBA && format == GL_RGBA &&
        type == GL_UNSIGNED_BYTE;
    uint64_t started_at_us = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    if (cache_dxt5)
        port_texture_cache_upload(width, height, data);
    else
        glTexImage2D(target, level, internal, width, height, border, format, type, data);
    check_texture_storage(target, level, width, height);
    if (port_log_enabled(PORT_LOG_DEBUG))
        port_perf_upload(port_vita_time_us() - started_at_us);
}
static void compat_compressed_image(GLenum target, GLint level, GLenum internal, GLsizei width,
                                    GLsizei height, GLint border, GLsizei size, const void *data) {
    uint64_t started_at_us = port_log_enabled(PORT_LOG_DEBUG) ? port_vita_time_us() : 0;
    glCompressedTexImage2D(target, level, internal, width, height, border, size, data);
    check_texture_storage(target, level, width, height);
    if (port_log_enabled(PORT_LOG_DEBUG))
        port_perf_upload(port_vita_time_us() - started_at_us);
}

static const PortImportEntry imports[] = {
    {"glViewport", (uintptr_t)(glViewport)},
    {"glFinish", (uintptr_t)(compat_finish)},
    {"glTexImage2D", (uintptr_t)(compat_tex_image)},
    {"glCompressedTexImage2D", (uintptr_t)(compat_compressed_image)},
    {"glBindFramebufferOES", (uintptr_t)(glBindFramebuffer)},
    {"glBindRenderbufferOES", (uintptr_t)(glBindRenderbuffer)},
    {"glCheckFramebufferStatusOES", (uintptr_t)(glCheckFramebufferStatus)},
    {"glDeleteFramebuffersOES", (uintptr_t)(glDeleteFramebuffers)},
    {"glDeleteRenderbuffersOES", (uintptr_t)(glDeleteRenderbuffers)},
    {"glFramebufferRenderbufferOES", (uintptr_t)(glFramebufferRenderbuffer)},
    {"glFramebufferTexture2DOES", (uintptr_t)(glFramebufferTexture2D)},
    {"glGenFramebuffersOES", (uintptr_t)(glGenFramebuffers)},
    {"glGenRenderbuffersOES", (uintptr_t)(glGenRenderbuffers)},
    {"glGenerateMipmapOES", (uintptr_t)(glGenerateMipmap)},
    {"glRenderbufferStorageOES", (uintptr_t)(glRenderbufferStorage)},
    {"glPointSizePointerOES", (uintptr_t)(compat_point_size)},
    {"glBindBuffer", (uintptr_t)(compat_bind_buffer)},
    {"glEnableClientState", (uintptr_t)(compat_enable_client)},
    {"glDisableClientState", (uintptr_t)(compat_disable_client)},
    {"glDrawArrays", (uintptr_t)(compat_draw_arrays)},
    {"glDrawElements", (uintptr_t)(compat_draw_elements)},
};
const PortImportTable port_graphics_imports = {imports, sizeof(imports) / sizeof(imports[0])};
