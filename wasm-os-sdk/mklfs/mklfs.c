/*
 * littlefs image builder compiled to WebAssembly, so the SDK can create a
 * ready-to-flash littlefs partition image with no external binaries — in
 * Node and in the browser alike.
 *
 * Pinned to the same littlefs version and geometry as the firmware's
 * esp_littlefs (joltwallet__littlefs): lfs v2.11, block 4096, read/prog 128,
 * cache 512, lookahead 128, block_cycles 512, single-version disk format.
 * Drift here produces images the device cannot mount.
 *
 * The ABI is deliberately primitive (static buffers, no allocator) so the
 * module needs no WASI imports: JS writes the filename into mk_name_buf()
 * and the file bytes into mk_file_buf(), then calls mk_add_file(len).
 */
#include <stdint.h>
#include <string.h>

#include "lfs.h"

#define BLOCK_SIZE 4096
#define READ_SIZE 128
#define PROG_SIZE 128
#define CACHE_SIZE 512
#define LOOKAHEAD_SIZE 128
#define BLOCK_CYCLES 512

#define MAX_IMAGE_BYTES (16u * 1024u * 1024u)
#define MAX_FILE_BYTES (8u * 1024u * 1024u)
#define MAX_NAME_BYTES 128

#define EXPORT(name) __attribute__((export_name(name)))

static uint8_t s_image[MAX_IMAGE_BYTES];
static uint8_t s_file_buf[MAX_FILE_BYTES];
static char s_name_buf[MAX_NAME_BYTES];

static uint8_t s_read_cache[CACHE_SIZE];
static uint8_t s_prog_cache[CACHE_SIZE];
static uint8_t s_file_cache[CACHE_SIZE];
static uint8_t s_lookahead[LOOKAHEAD_SIZE];

static lfs_t s_lfs;
static struct lfs_config s_cfg;
static uint32_t s_block_count;

static int bd_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
  memcpy(buffer, &s_image[block * BLOCK_SIZE + off], size);
  return 0;
}

static int bd_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
  memcpy(&s_image[block * BLOCK_SIZE + off], buffer, size);
  return 0;
}

static int bd_erase(const struct lfs_config* c, lfs_block_t block) {
  memset(&s_image[block * BLOCK_SIZE], 0xFF, BLOCK_SIZE);
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

/** Format and mount a fresh filesystem of block_count 4 KB blocks. */
EXPORT("mk_create") int32_t mk_create(uint32_t block_count) {
  if (block_count == 0 || (uint64_t)block_count * BLOCK_SIZE > MAX_IMAGE_BYTES) {
    return LFS_ERR_INVAL;
  }
  s_block_count = block_count;
  memset(s_image, 0xFF, block_count * BLOCK_SIZE);

  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.read = bd_read;
  s_cfg.prog = bd_prog;
  s_cfg.erase = bd_erase;
  s_cfg.sync = bd_sync;
  s_cfg.read_size = READ_SIZE;
  s_cfg.prog_size = PROG_SIZE;
  s_cfg.block_size = BLOCK_SIZE;
  s_cfg.block_count = block_count;
  s_cfg.block_cycles = BLOCK_CYCLES;
  s_cfg.cache_size = CACHE_SIZE;
  s_cfg.lookahead_size = LOOKAHEAD_SIZE;
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

/** Unmount; the finished image is at mk_image_ptr(). */
EXPORT("mk_finish") int32_t mk_finish(void) {
  return lfs_unmount(&s_lfs);
}
