#include "runtime/patches.h"

#include "app/log.h"
#include "runtime/module.h"
#include "runtime/module_internal.h"
#include "runtime/objc_layout.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void (*MenuMethod)(void *self, void *selector);
typedef void *(*SettingsInitializer)(void *self, void *selector, void *menu, int type);
typedef void (*CleanupMethod)(void *self, void *selector, signed char cleanup);

typedef void *(*RegisterSelector)(const char *name);
typedef CleanupMethod (*LookupCleanupMethod)(void *object, void *selector);
static const uintptr_t removed_calls[] = {
    0x28a81c, // settings Restore Progress insertion
    0x28a850, // settings Restore Purchases creation
    0x28aebc, // info trailer flag insertion
    0x28b99c, // info Rate App insertion
    0x28be70, // info Contact Us insertion
    0x28bfc8, // info Twitter insertion
    0x28c0f8, // info Facebook insertion
    0x28de98, // highscores game-services controller insertion
    0x28e3ac, // achievements game-services controller insertion
};

static MenuMethod original_main, original_flags, original_scores_tabs;
static SettingsInitializer original_settings;
static size_t controller_offset, flags_offset, facebook_offset, translation_offset;
static size_t global_scores_offset, facebook_scores_offset;
static RegisterSelector register_selector;
static LookupCleanupMethod lookup_method;
static void *remove_selector;

static void remove_node(void *self, size_t offset) {
    if (!self)
        return;
    void *node;
    void *empty = NULL;
    memcpy(&node, (char *)self + offset, sizeof(node));
    if (!node)
        return;
    if (!remove_selector)
        remove_selector = register_selector("removeFromParentAndCleanup:");
    CleanupMethod remove = lookup_method(node, remove_selector);
    if (!remove)
        port_fail("Missing native menu cleanup method");
    memcpy((char *)self + offset, &empty, sizeof(empty));
    remove(node, remove_selector, 1);
}

static void create_main(void *self, void *selector) {
    original_main(self, selector);
    remove_node(self, controller_offset);
}

static void setup_flags(void *self, void *selector) {
    original_flags(self, selector);
    remove_node(self, flags_offset);
}

static void *initialize_settings(void *self, void *selector, void *menu, int type) {
    void *settings = original_settings(self, selector, menu, type);
    remove_node(settings, facebook_offset);
    remove_node(settings, translation_offset);
    return settings;
}

static void create_scores_tabs(void *self, void *selector) {
    original_scores_tabs(self, selector);
    remove_node(self, global_scores_offset);
    remove_node(self, facebook_scores_offset);
}

static size_t menu_field_offset(uintptr_t record_offset) {
    const NativeObjcIvar *ivar = (void *)(port_module_image()->load_addr + record_offset);
    return ivar->offset;
}
static uintptr_t replace_builder(uintptr_t record_offset, uintptr_t replacement) {
    NativeObjcMethod *method = (void *)(port_module_image()->load_addr + record_offset);
    uintptr_t previous = method->implementation;
    method->implementation = replacement;
    return previous;
}

void port_patch_main_menu(void) {
    register_selector = (RegisterSelector)port_module_require_symbol("sel_registerName");
    lookup_method =
        (LookupCleanupMethod)port_module_require_symbol("object_getMethodImplementation");
    controller_offset = menu_field_offset(0xdbd080);
    flags_offset = menu_field_offset(0xdbd0ec);
    facebook_offset = menu_field_offset(0xde067c);
    translation_offset = menu_field_offset(0xde0688);
    global_scores_offset = menu_field_offset(0xdea9a0);
    facebook_scores_offset = menu_field_offset(0xdea9ac);
    original_main = (MenuMethod)replace_builder(0xdbbf10, (uintptr_t)create_main);
    original_flags = (MenuMethod)replace_builder(0xdbcd2c, (uintptr_t)setup_flags);
    original_settings =
        (SettingsInitializer)replace_builder(0xde0508, (uintptr_t)initialize_settings);
    original_scores_tabs =
        (MenuMethod)replace_builder(0xdea7c8, (uintptr_t)create_scores_tabs);
    for (size_t i = 0; i < sizeof(removed_calls) / sizeof(removed_calls[0]); i++) {
        uint32_t *instruction =
            (void *)(port_module_image()->load_addr + removed_calls[i]);
        *instruction = 0xe1a00000;
    }
}
