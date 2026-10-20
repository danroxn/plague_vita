#include "compat/jni/signature.h"

#include "app/log.h"

static const char *skip_type(const char *cursor, int allow_void) {
    int array = 0;
    while (*cursor == '[') {
        array = 1;
        cursor++;
    }
    switch (*cursor++) {
    case 'V':
        if (allow_void && !array)
            return cursor;
        break;
    case 'Z':
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'J':
    case 'F':
    case 'D':
        return cursor;
    case 'L': {
        const char *name = cursor;
        while (*cursor && *cursor != ';')
            cursor++;
        if (*cursor == ';' && cursor != name)
            return cursor + 1;
        break;
    }
    }
    port_fail("Malformed JNI type descriptor");
}

JniCallSignature port_jni_describe_method(const char *descriptor) {
    if (!descriptor || *descriptor != '(')
        port_fail("Invalid JNI method descriptor");
    JniCallSignature signature = {0};
    const char *cursor = descriptor + 1;
    while (*cursor && *cursor != ')') {
        if (signature.argument_count == PORT_JNI_MAX_ARGUMENTS)
            port_fail("Too many JNI arguments");
        signature.argument_types[signature.argument_count++] = *cursor;
        cursor = skip_type(cursor, 0);
    }
    if (*cursor != ')')
        port_fail("Unterminated JNI method descriptor");
    const char *result = cursor + 1;
    if (!*result || *skip_type(result, 1))
        port_fail("Invalid JNI return descriptor");
    signature.return_type = result;
    return signature;
}
void port_jni_unpack_call(const JniCallSignature *signature, va_list arguments, jvalue *output) {
    for (unsigned index = 0; index < signature->argument_count; index++) {
        jvalue *value = &output[index];
        switch (signature->argument_types[index]) {
        case 'Z':
            value->z = (jboolean)va_arg(arguments, int);
            break;
        case 'B':
            value->b = (jbyte)va_arg(arguments, int);
            break;
        case 'C':
            value->c = (jchar)va_arg(arguments, int);
            break;
        case 'S':
            value->s = (jshort)va_arg(arguments, int);
            break;
        case 'I':
            value->i = va_arg(arguments, jint);
            break;
        case 'J':
            value->j = va_arg(arguments, jlong);
            break;
        case 'F':
            value->f = (jfloat)va_arg(arguments, double);
            break;
        case 'D':
            value->d = va_arg(arguments, double);
            break;
        case 'L':
        case '[':
            value->l = va_arg(arguments, jobject);
            break;
        }
    }
}
