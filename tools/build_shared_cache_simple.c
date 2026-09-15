/*
 * build_shared_cache_simple.c — Panthera shared cache (Phase A)
 *
 * Concatenates dylib files with a header for fast lookup.
 * Runtime copies segments to contiguous VM and applies fixups normally.
 * Eliminates 14 open() syscalls per process launch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mach-o/loader.h>

#define PAGE_SZ 4096
#define PAGE_ALIGN(x) (((x) + PAGE_SZ - 1) & ~(uint64_t)(PAGE_SZ - 1))
#define MAX_DYLIBS 32
#define CACHE_BASE 0x200000000ULL

struct dyld_cache_header {
    char magic[16]; uint32_t mappingOffset; uint32_t mappingCount;
    uint32_t imagesOffsetOld; uint32_t imagesCountOld;
    char _pad1[184]; /* fill to sharedRegionStart at offset 224 */
    uint64_t sharedRegionStart; uint64_t sharedRegionSize;
    char _pad2[256]; /* remaining fields */
};

struct dyld_cache_image_info {
    uint64_t address; uint64_t modTime; uint64_t inode;
    uint32_t pathFileOffset; uint32_t pad;
};

struct DylibInfo {
    char installName[256]; uint8_t *mapped; size_t fileSize;
    struct stat st; uint64_t cacheFileOff;
};

static struct DylibInfo gDylibs[MAX_DYLIBS];
static int gNumDylibs;

static int load_dylib(const char *path) {
    if (gNumDylibs >= MAX_DYLIBS) return -1;
    struct DylibInfo *d = &gDylibs[gNumDylibs];
    memset(d, 0, sizeof(*d));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    fstat(fd, &d->st);
    d->fileSize = d->st.st_size;
    d->mapped = mmap(NULL, d->fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (d->mapped == MAP_FAILED) return -1;
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    if (mh->magic != MH_MAGIC_64) { munmap(d->mapped, d->fileSize); return -1; }
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_ID_DYLIB) {
            const struct dylib_command *dc = (const struct dylib_command *)cmd;
            snprintf(d->installName, sizeof(d->installName), "%s", (const char *)cmd + dc->dylib.name.offset);
        }
        cmd += lc->cmdsize;
    }
    if (!d->installName[0]) { munmap(d->mapped, d->fileSize); return -1; }
    for (int i = 0; i < gNumDylibs; i++)
        if (strcmp(gDylibs[i].installName, d->installName) == 0) { munmap(d->mapped, d->fileSize); return -1; }
    gNumDylibs++;
    return 0;
}

int main(int argc, char **argv) {
    const char *sysroot = NULL, *output = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sysroot") == 0 && i+1<argc) sysroot = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i+1<argc) output = argv[++i];
    }
    if (!sysroot || !output) { fprintf(stderr, "Usage: %s --sysroot <path> --output <path>\n", argv[0]); return 1; }

    static const char *paths[] = {
        "usr/lib/libSystem.B.dylib", "usr/lib/libiconv.2.dylib",
        "usr/lib/libncurses.5.4.dylib", "usr/lib/system/libsystem_kernel.dylib",
        "usr/lib/system/libsystem_platform.dylib", "usr/lib/system/libsystem_malloc.dylib",
        "usr/lib/system/libsystem_c.dylib", "usr/lib/system/libsystem_info.dylib",
        "usr/lib/system/libsystem_pthread.dylib", "usr/lib/system/libdispatch.dylib",
        "usr/lib/system/libxpc.dylib", "usr/lib/system/libpanthera_launchd.dylib",
        "usr/lib/system/libpanthera_extra.dylib", "usr/lib/system/libpanthera_patch.dylib",
        NULL
    };
    printf("Panthera shared cache builder\n");
    for (int i = 0; paths[i]; i++) {
        char fp[1024]; snprintf(fp, sizeof(fp), "%s/%s", sysroot, paths[i]);
        load_dylib(fp);
    }
    printf("  %d dylibs loaded\n", gNumDylibs);

    uint32_t imagesOff = sizeof(struct dyld_cache_header);
    uint32_t pathsOff = imagesOff + gNumDylibs * sizeof(struct dyld_cache_image_info);
    uint64_t pathsSize = 0;
    for (int i = 0; i < gNumDylibs; i++) pathsSize += strlen(gDylibs[i].installName) + 1;
    uint64_t metaSize = PAGE_ALIGN(pathsOff + pathsSize);

    uint64_t cursor = metaSize;
    for (int i = 0; i < gNumDylibs; i++) {
        gDylibs[i].cacheFileOff = cursor;
        cursor += PAGE_ALIGN(gDylibs[i].fileSize);
    }
    uint64_t totalSize = cursor;

    uint8_t *buf = calloc(1, totalSize);
    if (!buf) { fprintf(stderr, "calloc failed\n"); return 1; }
    for (int i = 0; i < gNumDylibs; i++)
        memcpy(buf + gDylibs[i].cacheFileOff, gDylibs[i].mapped, gDylibs[i].fileSize);

    struct dyld_cache_header *hdr = (struct dyld_cache_header *)buf;
    memcpy(hdr->magic, "dyld_v1  x86_64\0", 16);
    hdr->mappingOffset = 0; hdr->mappingCount = 0;
    hdr->imagesOffsetOld = imagesOff;
    hdr->imagesCountOld = gNumDylibs;
    hdr->sharedRegionStart = CACHE_BASE;
    hdr->sharedRegionSize = totalSize;

    struct dyld_cache_image_info *imgs = (struct dyld_cache_image_info *)(buf + imagesOff);
    uint32_t pathCur = pathsOff;
    for (int i = 0; i < gNumDylibs; i++) {
        imgs[i].address = CACHE_BASE + gDylibs[i].cacheFileOff;
        imgs[i].modTime = gDylibs[i].st.st_mtime;
        imgs[i].inode = gDylibs[i].st.st_ino;
        imgs[i].pathFileOffset = pathCur;
        size_t nl = strlen(gDylibs[i].installName) + 1;
        memcpy(buf + pathCur, gDylibs[i].installName, nl);
        pathCur += nl;
    }

    FILE *fp = fopen(output, "wb");
    if (!fp) { perror(output); return 1; }
    fwrite(buf, 1, totalSize, fp);
    fclose(fp);
    printf("  Cache: %s (%.1f MB, %d dylibs)\n", output, totalSize / (1024.0*1024.0), gNumDylibs);

    for (int i = 0; i < gNumDylibs; i++) munmap(gDylibs[i].mapped, gDylibs[i].fileSize);
    free(buf);
    return 0;
}
