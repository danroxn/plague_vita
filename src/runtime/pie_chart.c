#include "runtime/patches.h"

#include "runtime/game_api.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/objc_layout.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void (*GraphMethod)(void *node, void *selector);
typedef void (*SetPieData)(void *node, void *selector, void *plot, void *country);
typedef void (*SetPieIcons)(void *node, void *selector, const char *healthy, const char *infected,
                            const char *dead, const char *zombie);
typedef uintptr_t (*LookupMethod)(void *object, void *selector);
typedef void *(*RegisterSelector)(const char *name);

typedef struct {
    void *plot;
    int type;
    float rectangle[4];
    void *country;
    void *texture;
} GraphFields;
typedef struct {
    float values[4];
    void *icons[4];
    uint32_t colors[4];
} PieInputs;
_Static_assert(sizeof(GraphFields) == 32 && offsetof(GraphFields, texture) == 28,
               "Android GraphNode layout");
_Static_assert(sizeof(PieInputs) == 48, "Android NKPieChart input layout");
enum {
    GRAPH_FIELDS_OFFSET = 0x1b8,
    PIE_INPUTS_OFFSET = 8,
    PIE_GRAPH_TYPE = 3,
    PIE_CACHE_ENTRIES = 16,
    WORLD_POINTER_OFFSET = 0xc54,
    APE_VIEW_OFFSET = 0x10c8ba,
    ALTERNATE_POPULATION_OFFSET = 0x10c95d,
};
typedef struct {
    GraphFields graph;
    void *world;
    float values[4];
    uint32_t colors[4];
    void *icons[4];
    unsigned char ape_view;
    unsigned char alternate_population;
    float content_scale;
    float point_scale;
} PieSnapshot;
typedef struct {
    void *owner;
    PieSnapshot snapshot;
} PieCacheEntry;

static PieCacheEntry cache[PIE_CACHE_ENTRIES];
static GraphMethod original_draw, original_dealloc;
static SetPieIcons original_set_icons;
static LookupMethod lookup_method;
static RegisterSelector register_selector;
static void *pie_data_selector;
static void **game_state_root;

static PieCacheEntry *cache_entry(void *node) {
    return &cache[((uintptr_t)node >> 4) % PIE_CACHE_ENTRIES];
}
static GraphFields graph_fields(void *node) {
    GraphFields fields;
    memcpy(&fields, (char *)node + GRAPH_FIELDS_OFFSET, sizeof(fields));
    return fields;
}
static int take_snapshot(const GraphFields *graph, PieSnapshot *snapshot) {
    if (graph->type != PIE_GRAPH_TYPE || !graph->plot || !graph->texture || !*game_state_root)
        return 0;
    void *world;
    memcpy(&world, (char *)*game_state_root + WORLD_POINTER_OFFSET, sizeof(world));
    if (!world)
        return 0;
    PieInputs inputs;
    memcpy(&inputs, (char *)graph->plot + PIE_INPUTS_OFFSET, sizeof(inputs));
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->graph = *graph;
    snapshot->world = world;
    memcpy(snapshot->values, inputs.values, sizeof(inputs.values));
    memcpy(snapshot->colors, inputs.colors, sizeof(inputs.colors));
    memcpy(snapshot->icons, inputs.icons, sizeof(inputs.icons));
    snapshot->ape_view = *((unsigned char *)world + APE_VIEW_OFFSET);
    snapshot->alternate_population = *((unsigned char *)world + ALTERNATE_POPULATION_OFFSET);
    const GameApi *game = port_game_api();
    snapshot->content_scale = game->content_scale ? *game->content_scale : 1;
    snapshot->point_scale = game->point_scale ? *game->point_scale : 1;
    return 1;
}
static void draw_pie_if_changed(void *node, void *selector) {
    PieCacheEntry *entry = cache_entry(node);
    GraphFields graph = graph_fields(node);
    if (graph.type == PIE_GRAPH_TYPE && graph.plot && graph.texture) {
        if (!pie_data_selector)
            pie_data_selector = register_selector("setPieData:country:");
        SetPieData set_data = (SetPieData)lookup_method(node, pie_data_selector);
        set_data(node, pie_data_selector, graph.plot, graph.country);
        graph = graph_fields(node);
        PieSnapshot current;
        if (take_snapshot(&graph, &current) && entry->owner == node &&
            !memcmp(&entry->snapshot, &current, sizeof(current)))
            return;
    }
    original_draw(node, selector);
    graph = graph_fields(node);
    if (take_snapshot(&graph, &entry->snapshot))
        entry->owner = node;
    else
        entry->owner = NULL;
}
static void deallocate_graph(void *node, void *selector) {
    PieCacheEntry *entry = cache_entry(node);
    if (entry->owner == node)
        entry->owner = NULL;
    original_dealloc(node, selector);
}
static void replace_pie_icons(void *node, void *selector, const char *healthy, const char *infected,
                              const char *dead, const char *zombie) {
    PieCacheEntry *entry = cache_entry(node);
    if (entry->owner == node)
        entry->owner = NULL;
    original_set_icons(node, selector, healthy, infected, dead, zombie);
}
void port_patch_pie_chart(void) {
    const uintptr_t base = port_module_image()->load_addr;
    NativeObjcMethod *draw = (void *)(base + 0xdd5454);
    NativeObjcMethod *dealloc = (void *)(base + 0xdd54d8);
    NativeObjcMethod *icons = (void *)(base + 0xdd54c0);
    original_draw = (GraphMethod)draw->implementation;
    original_dealloc = (GraphMethod)dealloc->implementation;
    original_set_icons = (SetPieIcons)icons->implementation;
    lookup_method = (LookupMethod)port_module_require_symbol("object_getMethodImplementation");
    register_selector = (RegisterSelector)port_module_require_symbol("sel_registerName");
    game_state_root = (void **)(base + 0xea8408);
    draw->implementation = (uintptr_t)draw_pie_if_changed;
    dealloc->implementation = (uintptr_t)deallocate_graph;
    icons->implementation = (uintptr_t)replace_pie_icons;
}
