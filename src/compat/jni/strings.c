#include "compat/jni/strings.h"

#include "app/log.h"
#include "compat/jni/internal.h"
#include "compat/jni/runtime.h"
#include "compat/utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static jstring new_string(JNIEnv *environment, const jchar *chars, jsize length) {
    (void)environment;
    if (!chars && length)
        port_fail("Null JNI string data");
    JniObject *string_data = jni_object_create(JNI_OBJECT_STRING, length, sizeof(jchar));
    if (length)
        memcpy(string_data->storage, chars, (size_t)length * sizeof(jchar));
    return (jstring)string_data;
}
static jstring new_modified_utf8_string(JNIEnv *environment, const char *bytes) {
    if (!bytes)
        return new_string(environment, NULL, 0);
    size_t input_bytes = strlen(bytes);
    if (input_bytes > 1024 * 1024)
        port_fail("JNI UTF string exceeds limit");
    jchar *code_units = malloc((input_bytes + 1) * sizeof(jchar));
    if (!code_units)
        port_fail("JNI string allocation failed");
    jsize code_unit_count = 0;
    for (size_t byte_offset = 0; byte_offset < input_bytes;) {
        unsigned char lead_byte = (unsigned char)bytes[byte_offset++];
        uint32_t code_point;
        unsigned continuation_count;
        if (lead_byte < 0x80) {
            code_point = lead_byte;
            continuation_count = 0;
        } else if ((lead_byte & 0xe0) == 0xc0) {
            code_point = lead_byte & 31;
            continuation_count = 1;
        } else if ((lead_byte & 0xf0) == 0xe0) {
            code_point = lead_byte & 15;
            continuation_count = 2;
        } else if ((lead_byte & 0xf8) == 0xf0) {
            code_point = lead_byte & 7;
            continuation_count = 3;
        } else
            port_fail("Invalid JNI UTF lead byte");
        if (byte_offset + continuation_count > input_bytes)
            port_fail("Truncated JNI UTF string");
        for (unsigned continuation_index = 0; continuation_index < continuation_count;
             continuation_index++) {
            unsigned char continuation_byte = (unsigned char)bytes[byte_offset++];
            if ((continuation_byte & 0xc0) != 0x80)
                port_fail("Invalid JNI UTF continuation");
            code_point = (code_point << 6) | (continuation_byte & 63);
        }
        if (code_point > 0x10ffff)
            port_fail("Invalid JNI code point");
        if (code_point > 0xffff) {
            code_point -= 0x10000;
            code_units[code_unit_count++] = (jchar)(0xd800 + (code_point >> 10));
            code_units[code_unit_count++] = (jchar)(0xdc00 + (code_point & 1023));
        } else
            code_units[code_unit_count++] = (jchar)code_point;
    }
    jstring result = new_string(environment, code_units, code_unit_count);
    free(code_units);
    return result;
}
static JniObject *checked_string(jstring string) {
    if (!string)
        return NULL;
    JniObject *string_data = jni_object_checked((jobject)string);
    if (string_data->kind != JNI_OBJECT_STRING)
        port_fail("Expected JNI string");
    return string_data;
}
static jsize string_length(JNIEnv *environment, jstring string) {
    (void)environment;
    JniObject *string_data = checked_string(string);
    return string_data ? string_data->length : 0;
}
static const jchar *get_utf16_chars(JNIEnv *environment, jstring string, jboolean *is_copy) {
    static const jchar empty = 0;
    (void)environment;
    if (is_copy)
        *is_copy = JNI_FALSE;
    JniObject *string_data = checked_string(string);
    return string_data ? string_data->storage : &empty;
}
static void release_utf16_chars(JNIEnv *environment, jstring string, const jchar *characters) {
    (void)environment;
    (void)string;
    (void)characters;
}
static jsize count_modified_utf8_bytes(const jchar *chars, jsize code_unit_count) {
    jsize byte_count = 0;
    for (jsize code_unit_index = 0; code_unit_index < code_unit_count; code_unit_index++)
        byte_count += chars[code_unit_index] && chars[code_unit_index] < 128
                          ? 1
                          : (chars[code_unit_index] < 2048 ? 2 : 3);
    return byte_count;
}
static jsize modified_utf8_length(JNIEnv *environment, jstring string) {
    (void)environment;
    JniObject *string_data = checked_string(string);
    return string_data ? count_modified_utf8_bytes(string_data->storage, string_data->length) : 0;
}
static const char *get_modified_utf8_chars(JNIEnv *environment, jstring string, jboolean *is_copy) {
    (void)environment;
    JniObject *string_data = checked_string(string);
    jsize code_unit_count = string_data ? string_data->length : 0;
    const jchar *chars = string_data ? string_data->storage : NULL;
    char *encoded_bytes = malloc((size_t)count_modified_utf8_bytes(chars, code_unit_count) + 1);
    if (!encoded_bytes)
        port_fail("JNI UTF allocation failed");
    size_t output_offset = 0;
    for (jsize code_unit_index = 0; code_unit_index < code_unit_count; code_unit_index++) {
        unsigned code_unit = chars[code_unit_index];
        if (code_unit && code_unit < 128)
            encoded_bytes[output_offset++] = (char)code_unit;
        else if (code_unit < 2048) {
            encoded_bytes[output_offset++] = (char)(0xc0 | (code_unit >> 6));
            encoded_bytes[output_offset++] = (char)(0x80 | (code_unit & 63));
        } else {
            encoded_bytes[output_offset++] = (char)(0xe0 | (code_unit >> 12));
            encoded_bytes[output_offset++] = (char)(0x80 | ((code_unit >> 6) & 63));
            encoded_bytes[output_offset++] = (char)(0x80 | (code_unit & 63));
        }
    }
    encoded_bytes[output_offset] = 0;
    if (is_copy)
        *is_copy = JNI_TRUE;
    return encoded_bytes;
}
static void release_modified_utf8_chars(JNIEnv *environment, jstring string, char *encoded_bytes) {
    (void)environment;
    (void)string;
    free(encoded_bytes);
}
char *port_jni_string_to_utf8_alloc_with_length(jstring string, size_t *length) {
    JniObject *string_data = checked_string(string);
    const jchar *code_units = string_data ? string_data->storage : NULL;
    jsize code_unit_count = string_data ? string_data->length : 0;
    char *encoded_bytes = malloc((size_t)code_unit_count * 3 + 1);
    if (!encoded_bytes)
        port_fail("UTF-8 allocation failed");
    size_t output_offset = 0;
    for (jsize code_unit_index = 0; code_unit_index < code_unit_count; code_unit_index++) {
        uint32_t code_point = code_units[code_unit_index];
        if (code_point >= 0xd800 && code_point <= 0xdbff && code_unit_index + 1 < code_unit_count &&
            code_units[code_unit_index + 1] >= 0xdc00 &&
            code_units[code_unit_index + 1] <= 0xdfff) {
            code_point =
                0x10000 + ((code_point - 0xd800) << 10) + (code_units[++code_unit_index] - 0xdc00);
        } else if (code_point >= 0xd800 && code_point <= 0xdfff)
            code_point = 0xfffd;
        if (code_point < 128)
            encoded_bytes[output_offset++] = (char)code_point;
        else if (code_point < 2048) {
            encoded_bytes[output_offset++] = (char)(0xc0 | (code_point >> 6));
            encoded_bytes[output_offset++] = (char)(0x80 | (code_point & 63));
        } else if (code_point < 65536) {
            encoded_bytes[output_offset++] = (char)(0xe0 | (code_point >> 12));
            encoded_bytes[output_offset++] = (char)(0x80 | ((code_point >> 6) & 63));
            encoded_bytes[output_offset++] = (char)(0x80 | (code_point & 63));
        } else {
            encoded_bytes[output_offset++] = (char)(0xf0 | (code_point >> 18));
            encoded_bytes[output_offset++] = (char)(0x80 | ((code_point >> 12) & 63));
            encoded_bytes[output_offset++] = (char)(0x80 | ((code_point >> 6) & 63));
            encoded_bytes[output_offset++] = (char)(0x80 | (code_point & 63));
        }
    }
    encoded_bytes[output_offset] = 0;
    if (length)
        *length = output_offset;
    return encoded_bytes;
}
char *port_jni_string_to_utf8_alloc(jstring string) {
    return port_jni_string_to_utf8_alloc_with_length(string, NULL);
}

void port_jni_string_copy_utf8(jstring string, char *buffer, size_t capacity) {
    if (!capacity)
        return;
    char *text = port_jni_string_to_utf8_alloc(string);
    port_utf8_copy(buffer, capacity, text);
    free(text);
}

void jni_install_strings(struct JNINativeInterface *table) {
    table->NewString = new_string;
    table->NewStringUTF = new_modified_utf8_string;
    table->GetStringLength = string_length;
    table->GetStringChars = get_utf16_chars;
    table->ReleaseStringChars = release_utf16_chars;
    table->GetStringUTFLength = modified_utf8_length;
    table->GetStringUTFChars = get_modified_utf8_chars;
    table->ReleaseStringUTFChars = release_modified_utf8_chars;
}

jstring port_jni_string_from_utf8(const char *text) {
    return new_modified_utf8_string(&port_jni, text);
}
