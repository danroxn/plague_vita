#include "runtime/patches.h"

#include "runtime/game_api.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/objc_layout.h"

#include <stdint.h>
#include <string.h>

typedef void (*UpdateButton)(void *button, void *selector);
typedef void *(*InitializeButton)(void *button, void *selector, void *tech, unsigned index,
                                  void *target, void *action);
typedef uintptr_t (*LookupMethod)(void *object, void *selector);
typedef void *(*RegisterSelector)(const char *name);
typedef void *(*GetString)(void *object, void *selector);
typedef unsigned (*StringLength)(void *object, void *selector);
typedef struct {
    unsigned location, length;
} StringRange;
typedef void (*ReadCharacters)(void *object, void *selector, uint16_t *output, StringRange range);
typedef struct {
    unsigned length, capacity;
    const char *bytes;
} NativeString;
typedef struct {
    void *tech;
    void *graphics[9];
} ButtonFields;
_Static_assert(sizeof(NativeString) == 12 && sizeof(ButtonFields) == 40,
               "Android technology-button layouts");
_Static_assert(sizeof(StringRange) == 8, "Android NSRange layout");

enum {
    BUTTON_FIELDS_OFFSET = 0xf8,
    TECH_IMAGE_OFFSET = 0x94,
    TECH_TYPE_OFFSET = 0x220,
    TECH_RESEARCHED_OFFSET = 13,
    IMAGE_BYTES = 128,
    TYPE_BYTES = 32,
    PREFIX_UNITS = 64,
    CHILDREN_LIMIT = 32,
    BUTTON_CACHE_ENTRIES = 256,
};
typedef struct {
    ButtonFields button;
    float scale[2], size[2], anchor[2];
    float content_scale, point_scale;
    unsigned image_length, type_length, prefix_length, child_count;
    unsigned char researched;
    char image[IMAGE_BYTES], type[TYPE_BYTES];
    uint16_t prefix[PREFIX_UNITS];
    void *children[CHILDREN_LIMIT];
} ButtonSnapshot;
typedef struct {
    uintptr_t owner;
    ButtonSnapshot snapshot;
} ButtonCacheEntry;

static ButtonCacheEntry cache[BUTTON_CACHE_ENTRIES];
static unsigned next_entry;
static UpdateButton original_update;
static InitializeButton original_initialize;
static LookupMethod lookup_method;
static RegisterSelector register_selector;
static void *prefix_selector, *length_selector, *characters_selector;

static ButtonCacheEntry *find_entry(void *button) {
    for (unsigned i = 0; i < BUTTON_CACHE_ENTRIES; i++)
        if (cache[i].owner == (uintptr_t)button)
            return &cache[i];
    return NULL;
}
static void invalidate(void *button) {
    ButtonCacheEntry *entry = find_entry(button);
    if (entry)
        entry->owner = 0;
}
static int copy_native_string(void *tech, unsigned offset, char *output, unsigned capacity,
                              unsigned *length) {
    NativeString string;
    memcpy(&string, (char *)tech + offset, sizeof(string));
    if (string.length >= capacity || (string.length && !string.bytes))
        return 0;
    *length = string.length;
    if (string.length)
        memcpy(output, string.bytes, string.length);
    return 1;
}
static int take_snapshot(void *button, ButtonSnapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    memcpy(&snapshot->button, (char *)button + BUTTON_FIELDS_OFFSET, sizeof(ButtonFields));
    if (!snapshot->button.tech || !snapshot->button.graphics[0] || !snapshot->button.graphics[1])
        return 0;
    void *tech = snapshot->button.tech;
    if (!copy_native_string(tech, TECH_IMAGE_OFFSET, snapshot->image, IMAGE_BYTES,
                            &snapshot->image_length) ||
        !copy_native_string(tech, TECH_TYPE_OFFSET, snapshot->type, TYPE_BYTES,
                            &snapshot->type_length))
        return 0;
    snapshot->researched = *((unsigned char *)tech + TECH_RESEARCHED_OFFSET);
    memcpy(snapshot->scale, (char *)button + 8, sizeof(snapshot->scale));
    memcpy(snapshot->anchor, (char *)button + 0x34, sizeof(snapshot->anchor));
    memcpy(snapshot->size, (char *)button + 0x40, sizeof(snapshot->size));

    void *children, *array;
    memcpy(&children, (char *)button + 0xd0, sizeof(children));
    if (!children)
        return 0;
    memcpy(&array, (char *)children + 4, sizeof(array));
    if (!array)
        return 0;
    memcpy(&snapshot->child_count, array, sizeof(snapshot->child_count));
    if (snapshot->child_count > CHILDREN_LIMIT)
        return 0;
    void **items;
    memcpy(&items, (char *)array + 8, sizeof(items));
    if (snapshot->child_count && !items)
        return 0;
    if (snapshot->child_count)
        memcpy(snapshot->children, items, snapshot->child_count * sizeof(*items));

    if (!prefix_selector) {
        prefix_selector = register_selector("prefix");
        length_selector = register_selector("length");
        characters_selector = register_selector("getCharacters:range:");
    }
    GetString get_prefix = (GetString)lookup_method(button, prefix_selector);
    void *prefix = get_prefix(button, prefix_selector);
    if (!prefix)
        return 0;
    StringLength length = (StringLength)lookup_method(prefix, length_selector);
    snapshot->prefix_length = length(prefix, length_selector);
    if (snapshot->prefix_length > PREFIX_UNITS)
        return 0;
    if (snapshot->prefix_length) {
        ReadCharacters read = (ReadCharacters)lookup_method(prefix, characters_selector);
        read(prefix, characters_selector, snapshot->prefix,
             (StringRange){0, snapshot->prefix_length});
    }
    const GameApi *game = port_game_api();
    snapshot->content_scale = game->content_scale ? *game->content_scale : 1;
    snapshot->point_scale = game->point_scale ? *game->point_scale : 1;
    return 1;
}
static void update_if_changed(void *button, void *selector) {
    ButtonSnapshot current;
    ButtonCacheEntry *entry = find_entry(button);
    if (entry && take_snapshot(button, &current) &&
        !memcmp(&entry->snapshot, &current, sizeof(current)))
        return;
    original_update(button, selector);
    if (!entry)
        entry = &cache[next_entry++ % BUTTON_CACHE_ENTRIES];
    if (take_snapshot(button, &entry->snapshot))
        entry->owner = (uintptr_t)button;
    else
        entry->owner = 0;
}
static void *initialize_button(void *button, void *selector, void *tech, unsigned index,
                               void *target, void *action) {
    invalidate(button);
    void *result = original_initialize(button, selector, tech, index, target, action);
    invalidate(button);
    if (result)
        invalidate(result);
    return result;
}
void port_patch_tech_buttons(void) {
    const uintptr_t base = port_module_image()->load_addr;
    NativeObjcMethod *update = (void *)(base + 0xdd4090);
    NativeObjcMethod *initialize = (void *)(base + 0xdd406c);
    original_update = (UpdateButton)update->implementation;
    original_initialize = (InitializeButton)initialize->implementation;
    lookup_method = (LookupMethod)port_module_require_symbol("object_getMethodImplementation");
    register_selector = (RegisterSelector)port_module_require_symbol("sel_registerName");
    update->implementation = (uintptr_t)update_if_changed;
    initialize->implementation = (uintptr_t)initialize_button;
}
