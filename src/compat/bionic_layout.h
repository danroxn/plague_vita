#ifndef PLAGUE_COMPAT_BIONIC_LAYOUT_H
#define PLAGUE_COMPAT_BIONIC_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
enum { ANDROID_DT_DIR = 4, ANDROID_DT_REG = 8 };

typedef struct {
    uint64_t device;
    uint32_t pad0;
    uint32_t legacy_inode;
    uint32_t mode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdevice;
    uint32_t pad3;
    uint32_t pad_size;
    int64_t size;
    uint32_t block_size;
    uint32_t pad_blocks;
    uint64_t blocks;
    uint32_t access_sec;
    uint32_t access_nsec;
    uint32_t modify_sec;
    uint32_t modify_nsec;
    uint32_t change_sec;
    uint32_t change_nsec;
    uint64_t inode;
} AndroidStat;
typedef struct {
    uint64_t inode;
    int64_t offset;
    uint16_t record_size;
    uint8_t type;
    char name[256];
} AndroidDirent;
_Static_assert(sizeof(AndroidStat) == 104, "Android ARM stat size");
_Static_assert(offsetof(AndroidStat, size) == 48, "Android ARM stat size offset");
_Static_assert(offsetof(AndroidStat, blocks) == 64, "Android ARM stat blocks offset");
_Static_assert(offsetof(AndroidDirent, name) == 19, "Android ARM dirent name offset");
#endif
