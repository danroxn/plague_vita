#include "services/methods.h"

#include "app/log.h"
#include "compat/jni/runtime.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    COLLECTION_BOXED_INTEGER = 1,
    COLLECTION_MAP,
    COLLECTION_SET,
    COLLECTION_ITERATOR,
    COLLECTION_ENTRY
} CollectionKind;
enum { COLLECTION_ENTRY_CAPACITY = 65536 };
typedef struct {
    jobject key;
    jobject value;
} CollectionPair;
typedef struct {
    CollectionKind kind;
    int boxed_integer_value;
    size_t entry_count;
    size_t iterator_position;
    CollectionPair *entries;
} CollectionState;
static jobject retain_global_ref(jobject reference) {
    return port_jni->NewGlobalRef(&port_jni, reference);
}
static void release_global_ref(jobject reference) {
    port_jni->DeleteGlobalRef(&port_jni, reference);
}
static void destroy_collection_state(void *ptr) {
    CollectionState *collection = ptr;
    for (size_t i = 0; i < collection->entry_count; i++) {
        release_global_ref(collection->entries[i].key);
        release_global_ref(collection->entries[i].value);
    }
    free(collection->entries);
    free(collection);
}
static CollectionState *create_collection_state(jobject receiver, CollectionKind kind) {
    CollectionState *collection = calloc(1, sizeof(*collection));
    if (!collection)
        port_fail("Container allocation failed");
    collection->kind = kind;
    port_jni_set_payload(receiver, collection, destroy_collection_state);
    return collection;
}
static jobject new_collection_instance(const char *name, CollectionKind kind) {
    jobject object = port_jni_new_instance(name);
    create_collection_state(object, kind);
    return object;
}
static int collection_keys_equal(jobject left, jobject right) {
    if (left == right)
        return 1;
    if (!left || !right)
        return 0;
    const char *left_class = port_jni_class_name(left);
    const char *right_class = port_jni_class_name(right);
    if (strcmp(left_class, right_class))
        return 0;
    if (!strcmp(left_class, "java/lang/String")) {
        jsize length = port_jni->GetStringLength(&port_jni, left);
        if (length != port_jni->GetStringLength(&port_jni, right))
            return 0;
        const jchar *left_text = port_jni->GetStringChars(&port_jni, left, NULL);
        const jchar *right_text = port_jni->GetStringChars(&port_jni, right, NULL);
        int same = !memcmp(left_text, right_text, (size_t)length * sizeof(jchar));
        port_jni->ReleaseStringChars(&port_jni, left, left_text);
        port_jni->ReleaseStringChars(&port_jni, right, right_text);
        return same;
    }
    if (strcmp(left_class, "java/lang/Integer"))
        return 0;
    CollectionState *left_value = port_jni_payload(left);
    CollectionState *right_value = port_jni_payload(right);
    return left_value && right_value && left_value->kind == COLLECTION_BOXED_INTEGER &&
           right_value->kind == COLLECTION_BOXED_INTEGER &&
           left_value->boxed_integer_value == right_value->boxed_integer_value;
}
static size_t find_key_index(CollectionState *collection, jobject key) {
    for (size_t i = 0; i < collection->entry_count; i++)
        if (collection_keys_equal(collection->entries[i].key, key))
            return i;
    return collection->entry_count;
}
static void append_pair(CollectionState *collection, jobject key, jobject value) {
    if (collection->entry_count >= COLLECTION_ENTRY_CAPACITY)
        port_fail("Java container limit exceeded");
    CollectionPair *resized_items =
        realloc(collection->entries, (collection->entry_count + 1) * sizeof(*resized_items));
    if (!resized_items)
        port_fail("Java container allocation failed");
    collection->entries = resized_items;
    collection->entries[collection->entry_count++] =
        (CollectionPair){retain_global_ref(key), retain_global_ref(value)};
}
static CollectionState *collection_payload(jobject object) {
    const char *name = port_jni_class_name(object);
    if (!name || (strcmp(name, "java/lang/Integer") && strcmp(name, "java/util/HashMap") &&
                  strcmp(name, "java/util/HashSet") && strcmp(name, "java/util/Iterator") &&
                  strcmp(name, "java/util/Map$Entry")))
        port_fail("Expected a Java collection facade");
    return port_jni_payload(object);
}
static CollectionState *require_collection(const PortJniCall *call) {
    CollectionState *collection = collection_payload(call->receiver);
    if (!collection)
        port_fail("Uninitialized Java container %s.%s", call->method->owner, call->method->name);
    return collection;
}
static jvalue construct_integer(const PortJniCall *call) {
    CollectionState *collection = create_collection_state(call->receiver, COLLECTION_BOXED_INTEGER);
    collection->boxed_integer_value = call->arguments[0].i;
    return (jvalue){0};
}
static jvalue construct_map(const PortJniCall *call) {
    create_collection_state(call->receiver, COLLECTION_MAP);
    return (jvalue){0};
}
static jvalue construct_set(const PortJniCall *call) {
    create_collection_state(call->receiver, COLLECTION_SET);
    return (jvalue){0};
}
static jvalue integer_int_value(const PortJniCall *call) {
    return (jvalue){.i = require_collection(call)->boxed_integer_value};
}
static jvalue collection_size(const PortJniCall *call) {
    return (jvalue){.i = (jint)require_collection(call)->entry_count};
}
static jvalue collection_is_empty(const PortJniCall *call) {
    return (jvalue){.z = !require_collection(call)->entry_count};
}
static jvalue collection_clear(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    for (size_t i = 0; i < collection->entry_count; i++) {
        release_global_ref(collection->entries[i].key);
        release_global_ref(collection->entries[i].value);
    }
    collection->entry_count = 0;
    return (jvalue){0};
}
static jvalue map_put(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    jobject key = call->arguments[0].l, value = call->arguments[1].l;
    size_t index = find_key_index(collection, key);
    jobject previous = NULL;
    if (index < collection->entry_count) {
        previous = port_jni->NewLocalRef(&port_jni, collection->entries[index].value);
        jobject retained_value = retain_global_ref(value);
        release_global_ref(collection->entries[index].value);
        collection->entries[index].value = retained_value;
    } else
        append_pair(collection, key, value);
    return (jvalue){.l = previous};
}
static jvalue map_get(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    size_t index = find_key_index(collection, call->arguments[0].l);
    jobject value = index < collection->entry_count
                        ? port_jni->NewLocalRef(&port_jni, collection->entries[index].value)
                        : NULL;
    return (jvalue){.l = value};
}
static jvalue set_add(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    if (find_key_index(collection, call->arguments[0].l) != collection->entry_count)
        return (jvalue){.z = JNI_FALSE};
    append_pair(collection, call->arguments[0].l, NULL);
    return (jvalue){.z = JNI_TRUE};
}
static jvalue map_entry_set(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    jobject set = new_collection_instance("java/util/HashSet", COLLECTION_SET);
    CollectionState *snapshot = collection_payload(set);
    for (size_t i = 0; i < collection->entry_count; i++) {
        jobject entry = new_collection_instance("java/util/Map$Entry", COLLECTION_ENTRY);
        append_pair(collection_payload(entry), collection->entries[i].key,
                    collection->entries[i].value);
        append_pair(snapshot, entry, NULL);
        release_global_ref(entry);
    }
    return (jvalue){.l = set};
}
static jvalue set_iterator(const PortJniCall *call) {
    CollectionState *collection = require_collection(call);
    jobject iterator = new_collection_instance("java/util/Iterator", COLLECTION_ITERATOR);
    CollectionState *snapshot = collection_payload(iterator);
    for (size_t i = 0; i < collection->entry_count; i++)
        append_pair(snapshot, collection->entries[i].key, NULL);
    return (jvalue){.l = iterator};
}
static jvalue iterator_has_next(const PortJniCall *call) {
    CollectionState *iterator = require_collection(call);
    return (jvalue){.z = iterator->iterator_position < iterator->entry_count};
}
static jvalue iterator_next(const PortJniCall *call) {
    CollectionState *iterator = require_collection(call);
    if (iterator->iterator_position >= iterator->entry_count)
        port_fail("Java iterator exhausted");
    return (jvalue){.l = port_jni->NewLocalRef(
                        &port_jni, iterator->entries[iterator->iterator_position++].key)};
}
static jvalue entry_get_key(const PortJniCall *call) {
    return (jvalue){.l =
                        port_jni->NewLocalRef(&port_jni, require_collection(call)->entries[0].key)};
}
static jvalue entry_get_value(const PortJniCall *call) {
    return (jvalue){
        .l = port_jni->NewLocalRef(&port_jni, require_collection(call)->entries[0].value)};
}
static const PortServiceMethod methods[] = {
    {{"java/lang/Integer", "<init>", "(I)V", 0}, construct_integer},
    {{"java/lang/Integer", "clear", "()V", 0}, collection_clear},
    {{"java/lang/Integer", "intValue", "()I", 0}, integer_int_value},
    {{"java/lang/Integer", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/lang/Integer", "size", "()I", 0}, collection_size},
    {{"java/util/HashMap", "<init>", "()V", 0}, construct_map},
    {{"java/util/HashMap", "<init>", "(I)V", 0}, construct_map},
    {{"java/util/HashMap", "<init>", "(Ljava/util/Map;)V", 0}, construct_map},
    {{"java/util/HashMap", "clear", "()V", 0}, collection_clear},
    {{"java/util/HashMap", "entrySet", "()Ljava/util/Set;", 0}, map_entry_set},
    {{"java/util/HashMap", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", 0}, map_get},
    {{"java/util/HashMap", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", 0},
     map_put},
    {{"java/util/HashMap", "size", "()I", 0}, collection_size},
    {{"java/util/HashSet", "<init>", "()V", 0}, construct_set},
    {{"java/util/HashSet", "<init>", "(I)V", 0}, construct_set},
    {{"java/util/HashSet", "<init>", "(Ljava/util/Collection;)V", 0}, construct_set},
    {{"java/util/HashSet", "add", "(Ljava/lang/Object;)Z", 0}, set_add},
    {{"java/util/HashSet", "clear", "()V", 0}, collection_clear},
    {{"java/util/HashSet", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/HashSet", "iterator", "()Ljava/util/Iterator;", 0}, set_iterator},
    {{"java/util/HashSet", "size", "()I", 0}, collection_size},
    {{"java/util/Iterator", "<init>", "()V", 0}, construct_set},
    {{"java/util/Iterator", "clear", "()V", 0}, collection_clear},
    {{"java/util/Iterator", "hasNext", "()Z", 0}, iterator_has_next},
    {{"java/util/Iterator", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/Iterator", "next", "()Ljava/lang/Object;", 0}, iterator_next},
    {{"java/util/Iterator", "size", "()I", 0}, collection_size},
    {{"java/util/Map", "<init>", "()V", 0}, construct_map},
    {{"java/util/Map", "clear", "()V", 0}, collection_clear},
    {{"java/util/Map", "entrySet", "()Ljava/util/Set;", 0}, map_entry_set},
    {{"java/util/Map", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", 0}, map_get},
    {{"java/util/Map", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/Map", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", 0},
     map_put},
    {{"java/util/Map", "size", "()I", 0}, collection_size},
    {{"java/util/Map$Entry", "<init>", "()V", 0}, construct_map},
    {{"java/util/Map$Entry", "clear", "()V", 0}, collection_clear},
    {{"java/util/Map$Entry", "getKey", "()Ljava/lang/Object;", 0}, entry_get_key},
    {{"java/util/Map$Entry", "getValue", "()Ljava/lang/Object;", 0}, entry_get_value},
    {{"java/util/Map$Entry", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/Map$Entry", "size", "()I", 0}, collection_size},
    {{"java/util/Set", "<init>", "()V", 0}, construct_set},
    {{"java/util/Set", "add", "(Ljava/lang/Object;)Z", 0}, set_add},
    {{"java/util/Set", "clear", "()V", 0}, collection_clear},
    {{"java/util/Set", "isEmpty", "()Z", 0}, collection_is_empty},
    {{"java/util/Set", "iterator", "()Ljava/util/Iterator;", 0}, set_iterator},
    {{"java/util/Set", "size", "()I", 0}, collection_size},
};
const PortServiceMethodTable port_collections_methods = {methods,
                                                         sizeof(methods) / sizeof(methods[0])};
