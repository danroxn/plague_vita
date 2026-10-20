#include "app/loading_png.h"

#include <psp2/kernel/clib.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vitaGL/samples/skybox_env_map/stb_image.h"

const char *port_loading_png_read(const char *path, uint32_t *pixels, int width, int height) {
    FILE *background = fopen(path, "rb");
    if (!background)
        return "PNG missing from VPK";

    int image_width, image_height;
    if (!stbi_info_from_file(background, &image_width, &image_height, NULL)) {
        fclose(background);
        return stbi_failure_reason();
    }
    if (image_width != width || image_height != height) {
        fclose(background);
        return "Unexpected PNG dimensions";
    }

    stbi_uc *decoded =
        stbi_load_from_file(background, &image_width, &image_height, NULL, STBI_rgb_alpha);
    int read_error = ferror(background);
    fclose(background);
    if (!decoded)
        return stbi_failure_reason();
    if (read_error || image_width != width || image_height != height) {
        stbi_image_free(decoded);
        return "Invalid PNG loading background";
    }

    memcpy(pixels, decoded, (size_t)width * height * sizeof(*pixels));
    stbi_image_free(decoded);
    return NULL;
}
