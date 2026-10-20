#include "compat/import_table.h"

#include "app/log.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"

#include <dirent.h>
#include <errno.h>
#include <jni.h>
#include <psp2/io/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    long length;
} AndroidAsset;
typedef struct {
    char **names;
    size_t count;
    size_t index;
} AndroidAssetDirectory;
static int asset_manager_token;
static void *compat_AAssetManager_fromJava(JNIEnv *environment, jobject object) {
    (void)environment;
    (void)object;
    return &asset_manager_token;
}
static void *compat_AAssetManager_open(void *manager_handle, const char *name, int mode) {
    (void)manager_handle;
    (void)mode;
    char path[1024];
    if (!port_path_build_resource(name, path, sizeof(path), PORT_NDK_ASSET_PATH))
        return NULL;
    FILE *file = port_resource_fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    AndroidAsset *asset = malloc(sizeof(*asset));
    if (!asset) {
        fclose(file);
        return NULL;
    }
    asset->file = file;
    asset->length = length;
    return asset;
}
static int compat_AAsset_read(AndroidAsset *asset, void *buffer, size_t count) {
    if (!asset || count > 0x7fffffffU) {
        errno = EINVAL;
        return -1;
    }
    size_t bytes_read = fread(buffer, 1, count, asset->file);
    return ferror(asset->file) ? -1 : (int)bytes_read;
}
static long compat_AAsset_seek(AndroidAsset *asset, long offset, int whence) {
    if (!asset || fseek(asset->file, offset, whence))
        return -1;
    return ftell(asset->file);
}
static long compat_AAsset_getLength(AndroidAsset *asset) {
    return asset ? asset->length : 0;
}
static void compat_AAsset_close(AndroidAsset *asset) {
    if (asset) {
        fclose(asset->file);
        free(asset);
    }
}
static int compare_names(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}
static AndroidAssetDirectory *compat_AAssetManager_openDir(void *manager_handle, const char *name) {
    (void)manager_handle;
    char path[1024];
    if (!port_path_build_resource(name, path, sizeof(path), PORT_NDK_ASSET_PATH))
        return NULL;
    PortResourceDirectory *archive = NULL;
    DIR *directory = NULL;
    if (port_resource_is_path(path))
        archive = port_resource_directory_open(path);
    else
        directory = opendir(path);
    if (!archive && !directory)
        return NULL;
    AndroidAssetDirectory *result = calloc(1, sizeof(*result));
    if (!result) {
        if (archive)
            port_resource_directory_close(archive);
        else
            closedir(directory);
        return NULL;
    }
    for (;;) {
        const char *name;
        int regular;
        if (archive) {
            struct stat status;
            int read_result = port_resource_directory_read(archive, &name, &status);
            if (read_result != 1) {
                if (read_result < 0) {
                    int saved_errno = errno;
                    for (size_t i = 0; i < result->count; i++)
                        free(result->names[i]);
                    free(result->names);
                    free(result);
                    port_resource_directory_close(archive);
                    errno = saved_errno;
                    return NULL;
                }
                break;
            }
            regular = S_ISREG(status.st_mode);
        } else {
            struct dirent *directory_entry = readdir(directory);
            if (!directory_entry)
                break;
            name = directory_entry->d_name;
            regular = SCE_S_ISREG(directory_entry->d_stat.st_mode);
        }
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        if (!regular)
            continue;
        char **resized_names = realloc(result->names, (result->count + 1) * sizeof(char *));
        if (!resized_names)
            port_fail("Asset directory allocation failed");
        result->names = resized_names;
        result->names[result->count] = strdup(name);
        if (!result->names[result->count])
            port_fail("Asset name allocation failed");
        result->count++;
    }
    if (archive)
        port_resource_directory_close(archive);
    else
        closedir(directory);
    qsort(result->names, result->count, sizeof(char *), compare_names);
    return result;
}
static const char *compat_AAssetDir_getNextFileName(AndroidAssetDirectory *directory) {
    return directory && directory->index < directory->count ? directory->names[directory->index++]
                                                            : NULL;
}
static void compat_AAssetDir_close(AndroidAssetDirectory *directory) {
    if (!directory)
        return;
    for (size_t i = 0; i < directory->count; i++)
        free(directory->names[i]);
    free(directory->names);
    free(directory);
}

static const PortImportEntry imports[] = {
    {"AAssetManager_fromJava", (uintptr_t)(compat_AAssetManager_fromJava)},
    {"AAssetManager_open", (uintptr_t)(compat_AAssetManager_open)},
    {"AAssetManager_openDir", (uintptr_t)(compat_AAssetManager_openDir)},
    {"AAsset_close", (uintptr_t)(compat_AAsset_close)},
    {"AAsset_read", (uintptr_t)(compat_AAsset_read)},
    {"AAsset_seek", (uintptr_t)(compat_AAsset_seek)},
    {"AAsset_getLength", (uintptr_t)(compat_AAsset_getLength)},
    {"AAssetDir_close", (uintptr_t)(compat_AAssetDir_close)},
    {"AAssetDir_getNextFileName", (uintptr_t)(compat_AAssetDir_getNextFileName)},
};
const PortImportTable port_assets_imports = {imports, sizeof(imports) / sizeof(imports[0])};
