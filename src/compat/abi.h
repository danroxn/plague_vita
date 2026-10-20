#ifndef PLAGUE_COMPAT_ABI_H
#define PLAGUE_COMPAT_ABI_H
#include <stddef.h>
#include <stdint.h>
/* The original Android module passes floating-point values in core registers.
 * SDK/libm/VitaGL functions use hard-float. Mark crossing function types with
 * SFP when their floating-point arguments or results depend on that ABI. */
#define SFP __attribute__((pcs("aapcs")))
#endif
