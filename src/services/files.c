#include "services/files.h"

#include "services/methods.h"
#include "app/log.h"
#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"
#include "runtime/paths.h"
#include "runtime/resource_archive.h"

#include <openssl/evp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { FILE_PATH_CAPACITY = 1024, FILE_IO_BUFFER_SIZE = 4096, MD5_DIGEST_SIZE = 16 };

static void copy_file_object_path(jobject file, char path[FILE_PATH_CAPACITY]) {
    const char *attached_path = port_jni_attached_text(file);
    snprintf(path, FILE_PATH_CAPACITY, "%s", attached_path ? attached_path : "");
}

static void join_file_object_path(char path[FILE_PATH_CAPACITY], const char *parent,
                                  const char *child) {
    snprintf(path, FILE_PATH_CAPACITY, *child == '/' ? "%s%s" : "%s/%s", parent, child);
}

static jvalue file_construct(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    port_jni_string_copy_utf8((jstring)arguments[0].l, path, sizeof(path));
    port_jni_attach_text(receiver, path);
    return (jvalue){0};
}

static jvalue file_construct_with_parent_path(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    char parent[768];
    char child[256];
    port_jni_string_copy_utf8((jstring)arguments[0].l, parent, sizeof(parent));
    port_jni_string_copy_utf8((jstring)arguments[1].l, child, sizeof(child));
    join_file_object_path(path, parent, child);
    port_jni_attach_text(receiver, path);
    return (jvalue){0};
}

static jvalue file_construct_with_parent_file(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    char parent[768];
    char child[256];
    snprintf(parent, sizeof(parent), "%s", port_jni_attached_text(arguments[0].l));
    port_jni_string_copy_utf8((jstring)arguments[1].l, child, sizeof(child));
    join_file_object_path(path, parent, child);
    port_jni_attach_text(receiver, path);
    return (jvalue){0};
}

static jvalue file_get_absolute_path(const PortJniCall *call) {
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    copy_file_object_path(receiver, path);
    return (jvalue){.l = port_jni_string_from_utf8(path)};
}

static jvalue file_get_name(const PortJniCall *call) {
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    copy_file_object_path(receiver, path);
    char *slash = strrchr(path, '/');
    return (jvalue){.l = port_jni_string_from_utf8(slash ? slash + 1 : path)};
}

static jvalue file_exists(const PortJniCall *call) {
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    copy_file_object_path(receiver, path);
    struct stat info;
    int exists = port_resource_stat(port_path_normalize_file(path), &info) == 0;
    return (jvalue){.z = exists};
}

static jvalue file_length(const PortJniCall *call) {
    jobject receiver = call->receiver;
    char path[FILE_PATH_CAPACITY];
    copy_file_object_path(receiver, path);
    struct stat info;
    int exists = port_resource_stat(port_path_normalize_file(path), &info) == 0;
    return (jvalue){.j = (exists) ? (info.st_size) : (0)};
}

static int copy_file_contents(const char *source_path, const char *destination_path) {
    if (!port_path_is_local(destination_path))
        return 0;
    struct stat source_info, destination_info;
    if (!strcmp(source_path, destination_path) ||
        (!port_resource_stat(source_path, &source_info) && !stat(destination_path, &destination_info) &&
         source_info.st_ino &&
         source_info.st_dev == destination_info.st_dev &&
         source_info.st_ino == destination_info.st_ino))
        return 0;
    FILE *source_file = port_resource_fopen(source_path, "rb");
    if (!source_file)
        return 0;
    FILE *destination_file = fopen(destination_path, "wb");
    if (!destination_file) {
        fclose(source_file);
        return 0;
    }
    unsigned char buffer[FILE_IO_BUFFER_SIZE];
    size_t bytes_read;
    int succeeded = 1;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source_file)))
        if (fwrite(buffer, 1, bytes_read, destination_file) != bytes_read) {
            succeeded = 0;
            break;
        }
    if (ferror(source_file))
        succeeded = 0;
    if (fclose(destination_file))
        succeeded = 0;
    fclose(source_file);
    return succeeded;
}
static jvalue file_copy_copy_content(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;

    char *source = port_jni_string_to_utf8_alloc((jstring)arguments[0].l);
    char *destination = port_jni_string_to_utf8_alloc((jstring)arguments[1].l);
    const char *source_path = source;
    if (!strncmp(source_path, "file://", 7))
        source_path += 7;
    const char *destination_path = port_path_normalize_file(destination);
    int succeeded =
        port_path_is_local(source_path) && copy_file_contents(source_path, destination_path);
    if (!succeeded)
        port_log_error("FileCopy failed: source=%s destination=%s", source, destination);
    free(source);
    free(destination);
    return (jvalue){0};
}

static jvalue cocojava_copy_asset(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;

    char *resource_name = port_jni_string_to_utf8_alloc((jstring)arguments[0].l);
    char path[FILE_PATH_CAPACITY];
    char destination[FILE_PATH_CAPACITY];
    int succeeded =
        port_path_build_resource(resource_name, path, sizeof(path), PORT_JAVA_ASSET_PATH);
    const char *basename = strrchr(resource_name, '/');
    basename = (basename) ? (basename + 1) : (resource_name);
    mkdir(PORT_ROOT "export", 0777);
    snprintf(destination, sizeof(destination), PORT_ROOT "export/%s", basename);
    succeeded = succeeded && copy_file_contents(path, destination);
    if (!succeeded)
        port_log_error("Asset export failed: source=%s destination=%s", resource_name, destination);
    free(resource_name);
    return (jvalue){0};
}

static jvalue asset_utils_is_directory_in_apk(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;

    char *resource_name = port_jni_string_to_utf8_alloc((jstring)arguments[0].l);
    char path[FILE_PATH_CAPACITY];
    int valid = port_path_build_resource(resource_name, path, sizeof(path), PORT_JAVA_ASSET_PATH);
    free(resource_name);
    struct stat file_info;
    return (jvalue){.z = (valid && (port_resource_stat(path, &file_info) == 0)) &&
                         S_ISDIR(file_info.st_mode)};
}

static size_t md5_file(const char *path, unsigned char digest[MD5_DIGEST_SIZE]) {
    FILE *file = path ? port_resource_fopen(path, "rb") : NULL;
    if (!file)
        return 0;
    unsigned char buffer[FILE_IO_BUFFER_SIZE];
    unsigned digest_length = 0;
    EVP_MD_CTX *digest_context = EVP_MD_CTX_create();
    int succeeded = digest_context && EVP_DigestInit_ex(digest_context, EVP_md5(), NULL) == 1;
    if (succeeded) {
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)))
            if (EVP_DigestUpdate(digest_context, buffer, bytes_read) != 1) {
                succeeded = 0;
                break;
            }

        if (ferror(file))
            succeeded = 0;
        if (succeeded)
            succeeded = EVP_DigestFinal_ex(digest_context, digest, &digest_length) == 1;
    }
    fclose(file);
    EVP_MD_CTX_destroy(digest_context);
    return succeeded ? digest_length : 0;
}

static jvalue asset_utils_md5_checksum(const PortJniCall *call) {
    const jvalue *arguments = call->arguments;

    char *resource_name = port_jni_string_to_utf8_alloc((jstring)arguments[0].l);
    char path[FILE_PATH_CAPACITY];
    int valid;
    if (arguments[1].z)
        valid = port_path_build_resource(resource_name, path, sizeof(path), PORT_JAVA_ASSET_PATH);
    else {
        const char *file = port_path_normalize_file(resource_name);
        valid = port_path_is_local(file) && (strlen(file) < (sizeof(path)));
        if (valid)
            snprintf(path, sizeof(path), "%s", file);
    }
    free(resource_name);
    unsigned char digest[MD5_DIGEST_SIZE];
    size_t digest_length = md5_file(valid ? path : NULL, digest);
    jbyteArray bytes = port_jni->NewByteArray(&port_jni, digest_length);
    if (digest_length)
        port_jni->SetByteArrayRegion(&port_jni, bytes, 0, digest_length, (jbyte *)digest);
    return (jvalue){.l = bytes};
}

static const PortServiceMethod methods[] = {
    {{"java/io/File", "<init>", "(Ljava/lang/String;)V", 0}, file_construct},
    {{"java/io/File", "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", 0},
     file_construct_with_parent_path},
    {{"java/io/File", "<init>", "(Ljava/io/File;Ljava/lang/String;)V", 0},
     file_construct_with_parent_file},
    {{"java/io/File", "getAbsolutePath", "()Ljava/lang/String;", 0}, file_get_absolute_path},
    {{"java/io/File", "getName", "()Ljava/lang/String;", 0}, file_get_name},
    {{"java/io/File", "exists", "()Z", 0}, file_exists},
    {{"java/io/File", "length", "()J", 0}, file_length},

    {{"com/miniclip/filecopy/FileCopy", "copyContent", "(Ljava/lang/String;Ljava/lang/String;)V",
      1},
     file_copy_copy_content},
    {{"com/miniclip/nativeJNI/cocojava", "copyAsset", "(Ljava/lang/String;)V", 1},
     cocojava_copy_asset},
    {{"com/miniclip/utils/AssetUtils", "isDirectoryInApk", "(Ljava/lang/String;)Z", 1},
     asset_utils_is_directory_in_apk},
    {{"com/miniclip/utils/AssetUtils", "md5Checksum", "(Ljava/lang/String;Z)[B", 1},
     asset_utils_md5_checksum},
};
const PortServiceMethodTable port_files_methods = {methods, sizeof(methods) / sizeof(methods[0])};

PortJniHandler port_files_resolve_fallback(const PortJniMethodKey *method) {
    if (strcmp(method->owner, "java/io/File") || strcmp(method->name, "<init>"))
        return NULL;
    if (!strcmp(method->descriptor, "(Ljava/lang/String;Ljava/lang/String;)V"))
        return file_construct_with_parent_path;
    if (!strcmp(method->descriptor, "(Ljava/io/File;Ljava/lang/String;)V"))
        return file_construct_with_parent_file;
    return file_construct;
}
