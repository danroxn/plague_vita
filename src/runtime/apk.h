#ifndef PLAGUE_RUNTIME_APK_H
#define PLAGUE_RUNTIME_APK_H

// Verify the complete APK with XXH3-128 before opening its archive or starting
// loading workers. Every launch reads all bytes; no timestamp trust is cached.
// Returns 0 on success, or -1 with errno: ENOENT for a missing file, EINVAL for
// an unsupported size/hash, and EIO for a read or verification failure.
int port_apk_verify(const char *path);

#endif
