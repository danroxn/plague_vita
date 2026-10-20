#include "app/loading_indicator.h"

#include "vita/device.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    PATCH_WIDTH = 400,
    PATCH_HEIGHT = 400,
    PATCH_X = 280,
    PATCH_Y = 76,
    GRID_STEP = 4,
    GRID_COLUMNS = PATCH_WIDTH / GRID_STEP + 1,
    GRID_ROWS = PATCH_HEIGHT / GRID_STEP + 1,
    DISTANCE_SCALE = 16384,
    DISTANCE_BIAS = DISTANCE_SCALE,
    INDICATOR_OPACITY = 128,
};

static const float center_x = 480.25f, center_y = 275.25f;
static const float pixel_size = 1.4f / 341.5f;
static const float half_stroke = 0.7f * 43.5f / 341.5f;
static const uint32_t indicator_color = 0xff9fc6dcU;

typedef struct {
    float sine, cosine;
    float endpoint_x, endpoint_y;
} IndicatorArc;

typedef uint32_t PixelQuad __attribute__((vector_size(16), aligned(4), may_alias));
_Static_assert(GRID_STEP == sizeof(PixelQuad) / sizeof(uint32_t), "Four pixels per SIMD cell");

struct PortLoadingIndicator {
    uint32_t background[PATCH_HEIGHT][PATCH_WIDTH];
    uint32_t rows[GRID_STEP][PATCH_WIDTH];
    float rings[GRID_ROWS][GRID_COLUMNS];
    uint16_t field[GRID_ROWS][GRID_COLUMNS];
    uint16_t displayed[2][GRID_ROWS][GRID_COLUMNS];
    unsigned initialized;
    unsigned edge_limit;
    unsigned edge_scale;
};

static float clamp_unit(float value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

static float smoothstep(float value) {
    value = clamp_unit(value);
    return value * value * (3 - 2 * value);
}

static IndicatorArc make_arc(float turns, int pellet) {
    float angle = turns * 3.1415926535f;
    return (IndicatorArc){
        .sine = sinf(angle),
        .cosine = cosf(angle),
        .endpoint_x = 0.7f * (pellet ? 0.1950903220f : 0.9807852804f),
        .endpoint_y = 0.7f * (pellet ? 0.9807852804f : 0.1950903220f),
    };
}

static float arc_distance(float x, float y, float ring, const IndicatorArc *curve) {
    float rotated_x = fabsf(x * curve->cosine - y * curve->sine);
    float rotated_y = x * curve->sine + y * curve->cosine;
    if (curve->endpoint_y * rotated_x <= curve->endpoint_x * rotated_y)
        return ring;
    float dx = rotated_x - curve->endpoint_x;
    float dy = rotated_y - curve->endpoint_y;
    return sqrtf(dx * dx + dy * dy) - half_stroke;
}

static float indicator_distance(float x, float y, float ring, float limit,
                                const IndicatorArc arcs[3]) {
    if (ring >= limit)
        return limit;
    float body = arc_distance(x, y, ring, &arcs[0]);
    if (body <= ring)
        return ring;
    float pellet_a = arc_distance(x, y, ring, &arcs[1]);
    float pellet_b = arc_distance(x, y, ring, &arcs[2]);
    float pellets = pellet_a < pellet_b ? pellet_a : pellet_b;
    float smoothing_weight = clamp_unit((0.2f - fabsf(body - pellets)) * 5);
    float merged = (body < pellets ? body : pellets) -
                   smoothing_weight * smoothing_weight * smoothing_weight * (0.2f / 6);
    return merged > ring ? merged : ring;
}

static uint16_t encode_distance(float value) {
    return (uint16_t)(value * DISTANCE_SCALE + DISTANCE_BIAS + 0.5f);
}

static void sample_field(PortLoadingIndicator *indicator, float seconds) {
    float time = fmodf(seconds * 3, 16);
    IndicatorArc arcs[3] = {make_arc(1 - 0.125f * floorf(time), 0)};
    for (unsigned i = 0; i < 2; i++) {
        float phase = (time + i) * 0.5f;
        phase -= floorf(phase);
        arcs[i + 1] = make_arc(-0.5f + 1.125f * smoothstep(phase) - 0.125f * time, 1);
    }
    float padding = GRID_STEP * 1.414214f * pixel_size;
    for (unsigned row = 0; row < GRID_ROWS; row++) {
        float y = (center_y - (PATCH_Y + row * GRID_STEP + 0.5f)) * pixel_size;
        for (unsigned column = 0; column < GRID_COLUMNS; column++) {
            float x = (PATCH_X + column * GRID_STEP + 0.5f - center_x) * pixel_size;
            indicator->field[row][column] = encode_distance(indicator_distance(
                x, y, indicator->rings[row][column], pixel_size + padding, arcs));
        }
    }
}

static PixelQuad pixel_coverage(const PortLoadingIndicator *indicator, PixelQuad shape) {
    PixelQuad delta = shape - DISTANCE_BIAS;
    delta &= shape > DISTANCE_BIAS;
    PixelQuad outside = delta >= indicator->edge_limit - DISTANCE_BIAS;
    PixelQuad coverage = 256 - ((delta * indicator->edge_scale) >> 16);
    coverage &= ~outside;
    return (coverage * coverage * (768 - 2 * coverage) * 255 + 8388608) >> 24;
}

static PixelQuad blend_pixels(PixelQuad background, PixelQuad color, PixelQuad coverage) {
    PixelQuad alpha = (coverage * INDICATOR_OPACITY + 128) >> 8;
    PixelQuad inverse = 255 - alpha;
    PixelQuad red_blue =
        (background & 0x00ff00ffU) * inverse + (color & 0x00ff00ffU) * alpha + 0x00800080U;
    red_blue = (red_blue + ((red_blue >> 8) & 0x00ff00ffU)) >> 8;
    PixelQuad green = ((background >> 8) & 255) * inverse + ((color >> 8) & 255) * alpha + 128;
    green = (green + (green >> 8)) >> 8;
    return 0xff000000U | (red_blue & 0x00ff00ffU) | (green << 8);
}

PortLoadingIndicator *port_loading_indicator_create(const uint32_t *background) {
    PortLoadingIndicator *indicator = malloc(sizeof(*indicator));
    if (!indicator)
        return NULL;
    for (unsigned row = 0; row < PATCH_HEIGHT; row++)
        memcpy(indicator->background[row],
               background + (PATCH_Y + row) * PORT_DISPLAY_WIDTH + PATCH_X,
               sizeof(indicator->background[row]));
    for (unsigned row = 0; row < GRID_ROWS; row++) {
        float y = (center_y - (PATCH_Y + row * GRID_STEP + 0.5f)) * pixel_size;
        for (unsigned column = 0; column < GRID_COLUMNS; column++) {
            float x = (PATCH_X + column * GRID_STEP + 0.5f - center_x) * pixel_size;
            indicator->rings[row][column] = fabsf(sqrtf(x * x + y * y) - 0.7f) - half_stroke;
        }
    }
    indicator->initialized = 0;
    indicator->edge_limit = encode_distance(pixel_size) + 1;
    indicator->edge_scale = (unsigned)(256 * 65536.0f / (DISTANCE_SCALE * pixel_size) + 0.5f);
    return indicator;
}

static void copy_run(const PortLoadingIndicator *indicator, uint32_t *pixels, unsigned row,
                     unsigned first, unsigned end) {
    for (unsigned y = 0; y < GRID_STEP; y++)
        memcpy(pixels + (PATCH_Y + row + y) * PORT_DISPLAY_WIDTH + PATCH_X + first,
               indicator->rows[y] + first, (end - first) * sizeof(uint32_t));
}

static void render_cell(PortLoadingIndicator *indicator, unsigned grid_row, unsigned cell) {
    const uint16_t *top = indicator->field[grid_row];
    const uint16_t *bottom = indicator->field[grid_row + 1];
    const PixelQuad left_weights = {4, 3, 2, 1}, right_weights = {0, 1, 2, 3};
    unsigned first_row = grid_row * GRID_STEP, column = cell * GRID_STEP;
    unsigned minimum = top[cell], maximum = minimum;
    const uint16_t corners[] = {top[cell + 1], bottom[cell], bottom[cell + 1]};
    for (unsigned i = 0; i < 3; i++) {
        if (corners[i] < minimum)
            minimum = corners[i];
        if (corners[i] > maximum)
            maximum = corners[i];
    }
    int empty = minimum >= indicator->edge_limit;
    int solid = maximum <= DISTANCE_BIAS;
    const PixelQuad color = {indicator_color, indicator_color, indicator_color, indicator_color};
    const PixelQuad full_coverage = {255, 255, 255, 255};
    if (empty || solid) {
        for (unsigned y = 0; y < GRID_STEP; y++) {
            unsigned row = first_row + y;
            PixelQuad base = *(const PixelQuad *)(indicator->background[row] + column);
            PixelQuad result = solid ? blend_pixels(base, color, full_coverage) : base;
            *(PixelQuad *)(indicator->rows[y] + column) = result;
        }
        return;
    }
    PixelQuad shape = GRID_STEP * (top[cell] * left_weights + top[cell + 1] * right_weights);
    PixelQuad shape_step = (bottom[cell] - top[cell]) * left_weights +
                           (bottom[cell + 1] - top[cell + 1]) * right_weights;
    for (unsigned y = 0; y < GRID_STEP; y++) {
        unsigned row = first_row + y;
        PixelQuad base = *(const PixelQuad *)(indicator->background[row] + column);
        PixelQuad alpha = pixel_coverage(indicator, shape >> 4);
        *(PixelQuad *)(indicator->rows[y] + column) = blend_pixels(base, color, alpha);
        shape += shape_step;
    }
}

void port_loading_indicator_draw(PortLoadingIndicator *indicator, uint32_t *pixels, float seconds,
                                 unsigned buffer) {
    assert(buffer < 2);
    sample_field(indicator, seconds);
    for (unsigned grid_row = 0; grid_row < GRID_ROWS - 1; grid_row++) {
        const uint16_t *top = indicator->field[grid_row];
        const uint16_t *bottom = indicator->field[grid_row + 1];
        const uint16_t *old_top = indicator->displayed[buffer][grid_row];
        const uint16_t *old_bottom = indicator->displayed[buffer][grid_row + 1];
        unsigned first_row = grid_row * GRID_STEP;
        unsigned run_start = PATCH_WIDTH;
        for (unsigned cell = 0; cell < GRID_COLUMNS - 1; cell++) {
            unsigned column = cell * GRID_STEP;
            if ((indicator->initialized & (1U << buffer)) && top[cell] == old_top[cell] &&
                top[cell + 1] == old_top[cell + 1] && bottom[cell] == old_bottom[cell] &&
                bottom[cell + 1] == old_bottom[cell + 1]) {
                if (run_start != PATCH_WIDTH) {
                    copy_run(indicator, pixels, first_row, run_start, column);
                    run_start = PATCH_WIDTH;
                }
                continue;
            }
            if (run_start == PATCH_WIDTH)
                run_start = column;
            render_cell(indicator, grid_row, cell);
        }
        if (run_start != PATCH_WIDTH)
            copy_run(indicator, pixels, first_row, run_start, PATCH_WIDTH);
    }
    memcpy(indicator->displayed[buffer], indicator->field, sizeof(indicator->field));
    indicator->initialized |= 1U << buffer;
}

void port_loading_indicator_destroy(PortLoadingIndicator *indicator) { free(indicator); }
