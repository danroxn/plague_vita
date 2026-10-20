# JNI interface types

Unmodified `jni.h`, copied from FalsoJNI revision `083d5a07e025c6dfbb54ac68fd1acf696c23a86c`.
The header is Copyright (C) 2006 The Android Open Source Project and is
licensed under Apache-2.0; see its original notice and [LICENSE](LICENSE).

Only the VM-neutral interface types are used. The port implements its own
reference management and dispatch in `src/compat/jni/`.
