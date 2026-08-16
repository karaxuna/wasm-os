/*
 * littlefs image builder compiled to WebAssembly: create a ready-to-flash
 * littlefs partition image with no native binaries, in Node and the browser
 * alike. The module imports nothing, so it instantiates anywhere.
 *
 * Geometry is caller-provided (block/read/prog/cache/lookahead sizes must
 * match what will mount the image); ../src/index.js defaults to the
 * geometry ESP-IDF's esp_littlefs uses.
 *
 * The ABI is deliberately primitive (static buffers, no allocator) so the
 * module needs no imports: the host writes the filename into mk_name_buf()
 * and the file bytes into mk_file_buf(), then calls mk_add_file(len).
 */
#include <stdint.h>
#include <string.h>

#include "lfs.h"

#define BLOCK_CYCLES 512

#define MAX_IMAGE_BYTES (16u * 1024u * 1024u)
#define MAX_FILE_BYTES (8u * 1024u * 1024u)
#define MAX_NAME_BYTES 256
#define MAX_CACHE_BYTES 4096
#define MAX_LOOKAHEAD_BYTES 1024

#define EXPORT(name) __attribute__((export_name(name)))

static uint8_t s_image[MAX_IMAGE_BYTES];
static uint8_t s_file_buf[MAX_FILE_BYTES];
static char s_name_buf[MAX_NAME_BYTES];

static uint8_t s_read_cache[MAX_CACHE_BYTES];
static uint8_t s_prog_cache[MAX_CACHE_BYTES];
static uint8_t s_file_cache[MAX_CACHE_BYTES];
static uint8_t s_lookahead[MAX_LOOKAHEAD_BYTES];

static lfs_t s_lfs;
static struct lfs_config s_cfg;

static int bd_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
  memcpy(buffer, &s_image[block * c->block_size + off], size);
  return 0;
}

static int bd_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
  memcpy(&s_image[block * c->block_size + off], buffer, size);
  return 0;
}

static int bd_erase(const struct lfs_config* c, lfs_block_t block) {
  memset(&s_image[block * c->block_size], 0xFF, c->block_size);
  return 0;
}

static int bd_sync(const struct lfs_config* c) {
  return 0;
}

EXPORT("mk_file_buf") uint8_t* mk_file_buf(void) {
  return s_file_buf;
}

EXPORT("mk_file_cap") uint32_t mk_file_cap(void) {
  return MAX_FILE_BYTES;
}

EXPORT("mk_name_buf") char* mk_name_buf(void) {
  return s_name_buf;
}

EXPORT("mk_name_cap") uint32_t mk_name_cap(void) {
  return MAX_NAME_BYTES;
}

EXPORT("mk_image_ptr") uint8_t* mk_image_ptr(void) {
  return s_image;
}

/*
 * Format and mount a fresh filesystem. Geometry constraints are validated
 * here because littlefs's own asserts are compiled out: read/prog must
 * divide cache, cache must divide block, lookahead is a multiple of 8.
 */
EXPORT("mk_create")
int32_t mk_create(uint32_t block_size, uint32_t block_count, uint32_t read_size, uint32_t prog_size,
                  uint32_t cache_size, uint32_t lookahead_size) {
  if (block_size == 0 || block_count == 0 || (uint64_t)block_size * block_count > MAX_IMAGE_BYTES) {
    return LFS_ERR_INVAL;
  }
  if (read_size == 0 || prog_size == 0 || cache_size == 0 || cache_size > MAX_CACHE_BYTES ||
      cache_size % read_size != 0 || cache_size % prog_size != 0 || block_size % cache_size != 0) {
    return LFS_ERR_INVAL;
  }
  if (lookahead_size == 0 || lookahead_size > MAX_LOOKAHEAD_BYTES || lookahead_size % 8 != 0) {
    return LFS_ERR_INVAL;
  }

  memset(s_image, 0xFF, block_size * block_count);

  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.read = bd_read;
  s_cfg.prog = bd_prog;
  s_cfg.erase = bd_erase;
  s_cfg.sync = bd_sync;
  s_cfg.read_size = read_size;
  s_cfg.prog_size = prog_size;
  s_cfg.block_size = block_size;
  s_cfg.block_count = block_count;
  s_cfg.block_cycles = BLOCK_CYCLES;
  s_cfg.cache_size = cache_size;
  s_cfg.lookahead_size = lookahead_size;
  s_cfg.read_buffer = s_read_cache;
  s_cfg.prog_buffer = s_prog_cache;
  s_cfg.lookahead_buffer = s_lookahead;

  int err = lfs_format(&s_lfs, &s_cfg);
  if (err) {
    return err;
  }
  return lfs_mount(&s_lfs, &s_cfg);
}

/** Write mk_file_buf()[0..len) to the file named in mk_name_buf(). */
EXPORT("mk_add_file") int32_t mk_add_file(uint32_t len) {
  if (len > MAX_FILE_BYTES || s_name_buf[0] == '\0') {
    return LFS_ERR_INVAL;
  }

  struct lfs_file_config file_cfg = {.buffer = s_file_cache};
  lfs_file_t file;
  int err = lfs_file_opencfg(&s_lfs, &file, s_name_buf, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &file_cfg);
  if (err) {
    return err;
  }

  lfs_ssize_t written = lfs_file_write(&s_lfs, &file, s_file_buf, len);
  int close_err = lfs_file_close(&s_lfs, &file);
  if (written < 0) {
    return (int32_t)written;
  }
  if ((uint32_t)written != len) {
    return LFS_ERR_NOSPC;
  }
  return close_err;
}

/** Create a directory (parents must already exist). */
EXPORT("mk_mkdir") int32_t mk_mkdir(void) {
  if (s_name_buf[0] == '\0') {
    return LFS_ERR_INVAL;
  }
  return lfs_mkdir(&s_lfs, s_name_buf);
}

/** Unmount; the finished image is at mk_image_ptr(). */
EXPORT("mk_finish") int32_t mk_finish(void) {
  return lfs_unmount(&s_lfs);
}
