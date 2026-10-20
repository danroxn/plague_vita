#include "compat/import_table.h"

#include "compat/abi.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <vitaGL.h>

#define FLOAT_MATH_CATALOG(Unary, Binary, Special) \
    Unary(double, acos) \
    Unary(float, acosf) \
    Unary(double, acosh) \
    Unary(double, asin) \
    Unary(float, asinf) \
    Unary(double, asinh) \
    Unary(double, atan) \
    Unary(float, atanf) \
    Unary(double, atanh) \
    Binary(double, atan2) \
    Binary(float, atan2f) \
    Unary(double, cbrt) \
    Unary(double, ceil) \
    Unary(float, ceilf) \
    Binary(double, copysign) \
    Unary(double, cos) \
    Unary(float, cosf) \
    Unary(double, cosh) \
    Unary(double, erf) \
    Unary(double, erfc) \
    Unary(double, exp) \
    Unary(float, expf) \
    Unary(double, exp2) \
    Unary(double, expm1) \
    Unary(double, fabs) \
    Unary(float, fabsf) \
    Unary(double, floor) \
    Unary(float, floorf) \
    Binary(float, fmaxf) \
    Binary(float, fminf) \
    Binary(double, fmod) \
    Binary(float, fmodf) \
    Binary(double, hypot) \
    Special(ilogb) \
    Unary(double, lgamma) \
    Special(llrint) \
    Unary(double, log) \
    Unary(float, logf) \
    Unary(double, log10) \
    Unary(float, log10f) \
    Unary(double, log1p) \
    Unary(double, logb) \
    Special(lrint) \
    Unary(double, nearbyint) \
    Binary(double, nextafter) \
    Binary(float, nextafterf) \
    Binary(double, pow) \
    Binary(float, powf) \
    Binary(double, remainder) \
    Special(remquo) \
    Unary(double, rint) \
    Unary(float, roundf) \
    Special(scalbn) \
    Special(scalbnl) \
    Unary(double, sin) \
    Unary(float, sinf) \
    Unary(double, sinh) \
    Unary(double, sqrt) \
    Unary(float, sqrtf) \
    Unary(double, tan) \
    Unary(float, tanf) \
    Unary(double, tanh) \
    Unary(double, tgamma) \
    Special(frexp) \
    Special(modf) \
    Special(strtod) \
    Special(difftime)

#define DEFINE_UNARY_BRIDGE(Type, name)                                                            \
    static Type SFP sfp_##name(Type value) {                                                       \
        return name(value);                                                                        \
    }
#define DEFINE_BINARY_BRIDGE(Type, name)                                                           \
    static Type SFP sfp_##name(Type left, Type right) {                                            \
        return name(left, right);                                                                  \
    }

#define DECLARE_SPECIAL(name)
FLOAT_MATH_CATALOG(DEFINE_UNARY_BRIDGE, DEFINE_BINARY_BRIDGE, DECLARE_SPECIAL)
#undef DECLARE_SPECIAL

#undef DEFINE_UNARY_BRIDGE
#undef DEFINE_BINARY_BRIDGE

static int SFP sfp_ilogb(double value) {
    return ilogb(value);
}
static long SFP sfp_lrint(double value) {
    return lrint(value);
}
static long long SFP sfp_llrint(double value) {
    return llrint(value);
}
static double SFP sfp_frexp(double value, int *exponent_out) {
    return frexp(value, exponent_out);
}
static double SFP sfp_modf(double value, double *integer_part_out) {
    return modf(value, integer_part_out);
}
static double SFP sfp_remquo(double dividend, double divisor, int *quotient_out) {
    return remquo(dividend, divisor, quotient_out);
}
static double SFP sfp_scalbn(double value, int exponent) {
    return scalbn(value, exponent);
}
static long double SFP sfp_scalbnl(long double value, int exponent) {
    return scalbnl(value, exponent);
}
static double SFP sfp_strtod(const char *text, char **end_out) {
    return strtod(text, end_out);
}
static double SFP sfp_difftime(time_t end_time, time_t start_time) {
    return difftime(end_time, start_time);
}
static int SFP sfp_isfinite(double value) {
    return isfinite(value);
}
static int SFP sfp_signbit(double value) {
    return signbit(value);
}
static int SFP sfp_fpclassify(double value) {
    return fpclassify(value);
}
static int SFP sfp_isnanf(float value) {
    return isnan(value);
}

static void SFP sfp_glAlphaFunc(GLenum comparison, GLclampf reference_alpha) {
    glAlphaFunc(comparison, reference_alpha);
}
static void SFP sfp_glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    glClearColor(red, green, blue, alpha);
}
static void SFP sfp_glClearDepthf(GLclampf depth) {
    glClearDepth(depth);
}
static void SFP sfp_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    glColor4f(red, green, blue, alpha);
}
static void SFP sfp_glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
                               GLfloat near_plane, GLfloat far_plane) {
    glFrustum(left, right, bottom, top, near_plane, far_plane);
}
static void SFP sfp_glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
                             GLfloat near_plane, GLfloat far_plane) {
    glOrtho(left, right, bottom, top, near_plane, far_plane);
}
static void SFP sfp_glLineWidth(GLfloat width) {
    glLineWidth(width);
}
static void SFP sfp_glRotatef(GLfloat angle_degrees, GLfloat x, GLfloat y, GLfloat z) {
    glRotatef(angle_degrees, x, y, z);
}
static void SFP sfp_glScalef(GLfloat x, GLfloat y, GLfloat z) {
    glScalef(x, y, z);
}
static void SFP sfp_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    glTranslatef(x, y, z);
}
static void SFP sfp_glTexParameterf(GLenum target, GLenum parameter, GLfloat value) {
    glTexParameterf(target, parameter, value);
}

#define ENTRY(n) {#n, (uintptr_t)sfp_##n}
static const PortImportEntry fp_symbols[] = {
#define MATH_IMPORT(Type, name) ENTRY(name),
#define SPECIAL_IMPORT(name) ENTRY(name),
    FLOAT_MATH_CATALOG(MATH_IMPORT, MATH_IMPORT, SPECIAL_IMPORT)
#undef MATH_IMPORT
#undef SPECIAL_IMPORT
    {"__isfinite", (uintptr_t)sfp_isfinite},
    {"__signbit", (uintptr_t)sfp_signbit},
    {"__fpclassifyd", (uintptr_t)sfp_fpclassify},
    {"isnanf", (uintptr_t)sfp_isnanf},
    ENTRY(glAlphaFunc),
    ENTRY(glClearColor),
    ENTRY(glClearDepthf),
    ENTRY(glColor4f),
    ENTRY(glFrustumf),
    ENTRY(glOrthof),
    ENTRY(glLineWidth),
    ENTRY(glRotatef),
    ENTRY(glScalef),
    ENTRY(glTranslatef),
    ENTRY(glTexParameterf),
};
const PortImportTable port_float_imports = {fp_symbols, sizeof(fp_symbols) / sizeof(fp_symbols[0])};

#undef FLOAT_MATH_CATALOG
#undef ENTRY
