/* The engine behind the AES entries of zip.js: AES-CTR with the WinZip counter, a 16-byte
   little-endian integer starting at 1, and HMAC-SHA1 over the ciphertext. Forward-only T-table
   AES, the aes_core.c layout. One context per stream: init() takes the cipher key and the
   authentication key, process() works in place and takes whole 16-byte blocks except for the last
   call, end() writes the 20-byte MAC when given an output pointer, clears sensitive state, and
   frees the context. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_LENGTH 16
#define SHA1_BLOCK_LENGTH 64
#define SHA1_DIGEST_LENGTH 20
#define ROUND_KEYS_LENGTH 60

struct aes_hmac_ctx {
  uint32_t round_keys[ROUND_KEYS_LENGTH];
  int rounds;
  /* 64-bit WinZip counter followed by eight zero bytes; wasm32 is little-endian. */
  uint64_t counter[2];
  uint32_t sha1_state[5];
  uint64_t sha1_count;
  uint8_t sha1_buffer[SHA1_BLOCK_LENGTH];
  uint8_t opad[SHA1_BLOCK_LENGTH];
};

static uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
static uint8_t sbox[256];
static int tables_ready;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t load_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void store_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

/* Volatile stores keep LTO from removing the wipe before free(). */
__attribute__((noinline, minsize))
static void secure_clear(void *ptr, size_t length) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (length--) {
    *p++ = 0;
  }
}

static void init_tables(void) {
  uint8_t p = 1, q = 1;
  do {
    p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
    q ^= (uint8_t)(q << 1);
    q ^= (uint8_t)(q << 2);
    q ^= (uint8_t)(q << 4);
    q ^= (q & 0x80) ? 0x09 : 0;
    uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
    sbox[p] = (uint8_t)(x ^ 0x63);
  } while (p != 1);
  sbox[0] = 0x63;
  for (int i = 0; i < 256; i++) {
    uint8_t s = sbox[i], s2 = xtime(s), s3 = (uint8_t)(s2 ^ s);
    uint32_t t = ((uint32_t)s2 << 24) | ((uint32_t)s << 16) | ((uint32_t)s << 8) | s3;
    Te0[i] = t;
    Te1[i] = rotr(t, 8);
    Te2[i] = rotr(t, 16);
    Te3[i] = rotr(t, 24);
  }
  tables_ready = 1;
}

static uint32_t sub_word(uint32_t w) {
  return ((uint32_t)sbox[w >> 24] << 24) | ((uint32_t)sbox[(w >> 16) & 255] << 16) |
         ((uint32_t)sbox[(w >> 8) & 255] << 8) | sbox[w & 255];
}

static void expand_key(struct aes_hmac_ctx *c, const uint8_t *key, unsigned key_length) {
  int nk = (int)key_length / 4;
  c->rounds = nk + 6;
  int total = 4 * (c->rounds + 1);
  uint8_t rcon = 1;
  for (int i = 0; i < nk; i++) {
    c->round_keys[i] = load_be32(key + 4 * i);
  }
  for (int i = nk; i < total; i++) {
    uint32_t t = c->round_keys[i - 1];
    if (i % nk == 0) {
      t = sub_word(rotl(t, 8)) ^ ((uint32_t)rcon << 24);
      rcon = xtime(rcon);
    } else if (nk > 6 && i % nk == 4) {
      t = sub_word(t);
    }
    c->round_keys[i] = c->round_keys[i - nk] ^ t;
  }
}

static void encrypt_block(const struct aes_hmac_ctx *c, const uint8_t in[BLOCK_LENGTH], uint8_t out[BLOCK_LENGTH]) {
  const uint32_t *rk = c->round_keys;
  uint32_t s0 = load_be32(in) ^ rk[0], s1 = load_be32(in + 4) ^ rk[1];
  uint32_t s2 = load_be32(in + 8) ^ rk[2], s3 = load_be32(in + 12) ^ rk[3];
  uint32_t t0, t1, t2, t3;
#define AES_ROUND(O0, O1, O2, O3, I0, I1, I2, I3, K) do { \
  (O0) = Te0[(I0) >> 24] ^ Te1[((I1) >> 16) & 255] ^ Te2[((I2) >> 8) & 255] ^ Te3[(I3) & 255] ^ (K)[0]; \
  (O1) = Te0[(I1) >> 24] ^ Te1[((I2) >> 16) & 255] ^ Te2[((I3) >> 8) & 255] ^ Te3[(I0) & 255] ^ (K)[1]; \
  (O2) = Te0[(I2) >> 24] ^ Te1[((I3) >> 16) & 255] ^ Te2[((I0) >> 8) & 255] ^ Te3[(I1) & 255] ^ (K)[2]; \
  (O3) = Te0[(I3) >> 24] ^ Te1[((I0) >> 16) & 255] ^ Te2[((I1) >> 8) & 255] ^ Te3[(I2) & 255] ^ (K)[3]; \
} while (0)
  AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 4);
  AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk + 8);
  AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 12);
  AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk + 16);
  AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 20);
  AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk + 24);
  AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 28);
  AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk + 32);
  AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 36);
  rk += 40;
  if (c->rounds > 10) {
    AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk);
    AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 4);
    rk += 8;
  }
  if (c->rounds > 12) {
    AES_ROUND(s0, s1, s2, s3, t0, t1, t2, t3, rk);
    AES_ROUND(t0, t1, t2, t3, s0, s1, s2, s3, rk + 4);
    rk += 8;
  }
#undef AES_ROUND
  store_be32(out, (Te2[t0 >> 24] & 0xff000000) ^ (Te3[(t1 >> 16) & 255] & 0x00ff0000) ^
      (Te0[(t2 >> 8) & 255] & 0x0000ff00) ^ (Te1[t3 & 255] & 0x000000ff) ^ rk[0]);
  store_be32(out + 4, (Te2[t1 >> 24] & 0xff000000) ^ (Te3[(t2 >> 16) & 255] & 0x00ff0000) ^
      (Te0[(t3 >> 8) & 255] & 0x0000ff00) ^ (Te1[t0 & 255] & 0x000000ff) ^ rk[1]);
  store_be32(out + 8, (Te2[t2 >> 24] & 0xff000000) ^ (Te3[(t3 >> 16) & 255] & 0x00ff0000) ^
      (Te0[(t0 >> 8) & 255] & 0x0000ff00) ^ (Te1[t1 & 255] & 0x000000ff) ^ rk[2]);
  store_be32(out + 12, (Te2[t3 >> 24] & 0xff000000) ^ (Te3[(t0 >> 16) & 255] & 0x00ff0000) ^
      (Te0[(t1 >> 8) & 255] & 0x0000ff00) ^ (Te1[t2 & 255] & 0x000000ff) ^ rk[3]);
}

/*
 * SHA-1 in C
 * By Steve Reid <steve@edmweb.com>
 * 100% Public Domain
 *
 * Adapted from OpenSSH portable's openbsd-compat/sha1.c for this combined
 * AES/HMAC context and the little-endian wasm32 target.
 */
typedef union {
  uint8_t c[SHA1_BLOCK_LENGTH];
  uint32_t l[SHA1_BLOCK_LENGTH / sizeof(uint32_t)];
} sha1_block;

#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
/* wasm32 is little-endian, so the first 16 words need byte swapping. */
#define SHA1_BLK0(block, i) ((block).l[i] = \
    (SHA1_ROL((block).l[i], 24) & 0xFF00FF00) | \
    (SHA1_ROL((block).l[i], 8) & 0x00FF00FF))
#define SHA1_BLK(block, i) ((block).l[(i) & 15] = SHA1_ROL( \
    (block).l[((i) + 13) & 15] ^ (block).l[((i) + 8) & 15] ^ \
    (block).l[((i) + 2) & 15] ^ (block).l[(i) & 15], 1))
#define SHA1_R0(block, v, w, x, y, z, i) do { \
  (z) += (((w) & ((x) ^ (y))) ^ (y)) + SHA1_BLK0((block), (i)) + \
      0x5A827999 + SHA1_ROL((v), 5); \
  (w) = SHA1_ROL((w), 30); \
} while (0)
#define SHA1_R1(block, v, w, x, y, z, i) do { \
  (z) += (((w) & ((x) ^ (y))) ^ (y)) + SHA1_BLK((block), (i)) + \
      0x5A827999 + SHA1_ROL((v), 5); \
  (w) = SHA1_ROL((w), 30); \
} while (0)
#define SHA1_R2(block, v, w, x, y, z, i) do { \
  (z) += ((w) ^ (x) ^ (y)) + SHA1_BLK((block), (i)) + \
      0x6ED9EBA1 + SHA1_ROL((v), 5); \
  (w) = SHA1_ROL((w), 30); \
} while (0)
#define SHA1_R3(block, v, w, x, y, z, i) do { \
  (z) += ((((w) | (x)) & (y)) | ((w) & (x))) + SHA1_BLK((block), (i)) + \
      0x8F1BBCDC + SHA1_ROL((v), 5); \
  (w) = SHA1_ROL((w), 30); \
} while (0)
#define SHA1_R4(block, v, w, x, y, z, i) do { \
  (z) += ((w) ^ (x) ^ (y)) + SHA1_BLK((block), (i)) + \
      0xCA62C1D6 + SHA1_ROL((v), 5); \
  (w) = SHA1_ROL((w), 30); \
} while (0)

static void sha1_transform(uint32_t state[5], const uint8_t buffer[SHA1_BLOCK_LENGTH]) {
  uint32_t a, b, c, d, e;
  sha1_block block;

  memcpy(&block, buffer, sizeof(block));
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];

  SHA1_R0(block,a,b,c,d,e, 0); SHA1_R0(block,e,a,b,c,d, 1); SHA1_R0(block,d,e,a,b,c, 2); SHA1_R0(block,c,d,e,a,b, 3);
  SHA1_R0(block,b,c,d,e,a, 4); SHA1_R0(block,a,b,c,d,e, 5); SHA1_R0(block,e,a,b,c,d, 6); SHA1_R0(block,d,e,a,b,c, 7);
  SHA1_R0(block,c,d,e,a,b, 8); SHA1_R0(block,b,c,d,e,a, 9); SHA1_R0(block,a,b,c,d,e,10); SHA1_R0(block,e,a,b,c,d,11);
  SHA1_R0(block,d,e,a,b,c,12); SHA1_R0(block,c,d,e,a,b,13); SHA1_R0(block,b,c,d,e,a,14); SHA1_R0(block,a,b,c,d,e,15);
  SHA1_R1(block,e,a,b,c,d,16); SHA1_R1(block,d,e,a,b,c,17); SHA1_R1(block,c,d,e,a,b,18); SHA1_R1(block,b,c,d,e,a,19);
  SHA1_R2(block,a,b,c,d,e,20); SHA1_R2(block,e,a,b,c,d,21); SHA1_R2(block,d,e,a,b,c,22); SHA1_R2(block,c,d,e,a,b,23);
  SHA1_R2(block,b,c,d,e,a,24); SHA1_R2(block,a,b,c,d,e,25); SHA1_R2(block,e,a,b,c,d,26); SHA1_R2(block,d,e,a,b,c,27);
  SHA1_R2(block,c,d,e,a,b,28); SHA1_R2(block,b,c,d,e,a,29); SHA1_R2(block,a,b,c,d,e,30); SHA1_R2(block,e,a,b,c,d,31);
  SHA1_R2(block,d,e,a,b,c,32); SHA1_R2(block,c,d,e,a,b,33); SHA1_R2(block,b,c,d,e,a,34); SHA1_R2(block,a,b,c,d,e,35);
  SHA1_R2(block,e,a,b,c,d,36); SHA1_R2(block,d,e,a,b,c,37); SHA1_R2(block,c,d,e,a,b,38); SHA1_R2(block,b,c,d,e,a,39);
  SHA1_R3(block,a,b,c,d,e,40); SHA1_R3(block,e,a,b,c,d,41); SHA1_R3(block,d,e,a,b,c,42); SHA1_R3(block,c,d,e,a,b,43);
  SHA1_R3(block,b,c,d,e,a,44); SHA1_R3(block,a,b,c,d,e,45); SHA1_R3(block,e,a,b,c,d,46); SHA1_R3(block,d,e,a,b,c,47);
  SHA1_R3(block,c,d,e,a,b,48); SHA1_R3(block,b,c,d,e,a,49); SHA1_R3(block,a,b,c,d,e,50); SHA1_R3(block,e,a,b,c,d,51);
  SHA1_R3(block,d,e,a,b,c,52); SHA1_R3(block,c,d,e,a,b,53); SHA1_R3(block,b,c,d,e,a,54); SHA1_R3(block,a,b,c,d,e,55);
  SHA1_R3(block,e,a,b,c,d,56); SHA1_R3(block,d,e,a,b,c,57); SHA1_R3(block,c,d,e,a,b,58); SHA1_R3(block,b,c,d,e,a,59);
  SHA1_R4(block,a,b,c,d,e,60); SHA1_R4(block,e,a,b,c,d,61); SHA1_R4(block,d,e,a,b,c,62); SHA1_R4(block,c,d,e,a,b,63);
  SHA1_R4(block,b,c,d,e,a,64); SHA1_R4(block,a,b,c,d,e,65); SHA1_R4(block,e,a,b,c,d,66); SHA1_R4(block,d,e,a,b,c,67);
  SHA1_R4(block,c,d,e,a,b,68); SHA1_R4(block,b,c,d,e,a,69); SHA1_R4(block,a,b,c,d,e,70); SHA1_R4(block,e,a,b,c,d,71);
  SHA1_R4(block,d,e,a,b,c,72); SHA1_R4(block,c,d,e,a,b,73); SHA1_R4(block,b,c,d,e,a,74); SHA1_R4(block,a,b,c,d,e,75);
  SHA1_R4(block,e,a,b,c,d,76); SHA1_R4(block,d,e,a,b,c,77); SHA1_R4(block,c,d,e,a,b,78); SHA1_R4(block,b,c,d,e,a,79);

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  a = b = c = d = e = 0;
}

static void sha1_init(struct aes_hmac_ctx *c) {
  c->sha1_count = 0;
  c->sha1_state[0] = 0x67452301;
  c->sha1_state[1] = 0xEFCDAB89;
  c->sha1_state[2] = 0x98BADCFE;
  c->sha1_state[3] = 0x10325476;
  c->sha1_state[4] = 0xC3D2E1F0;
}

static void sha1_update(struct aes_hmac_ctx *c, const uint8_t *data, size_t length) {
  size_t i, j;

  j = (size_t)((c->sha1_count >> 3) & 63);
  c->sha1_count += ((uint64_t)length << 3);
  if (j + length > 63) {
    memcpy(&c->sha1_buffer[j], data, (i = SHA1_BLOCK_LENGTH - j));
    sha1_transform(c->sha1_state, c->sha1_buffer);
    for (; i + 63 < length; i += SHA1_BLOCK_LENGTH) {
      sha1_transform(c->sha1_state, &data[i]);
    }
    j = 0;
  } else {
    i = 0;
  }
  memcpy(&c->sha1_buffer[j], &data[i], length - i);
}

static void sha1_final(struct aes_hmac_ctx *c, uint8_t out[SHA1_DIGEST_LENGTH]) {
  uint8_t final_count[8];

  for (unsigned i = 0; i < 8; i++) {
    final_count[i] = (uint8_t)((c->sha1_count >> ((7 - i) * 8)) & 255);
  }
  sha1_update(c, (const uint8_t *)"\200", 1);
  while ((c->sha1_count & 504) != 448) {
    sha1_update(c, (const uint8_t *)"\0", 1);
  }
  sha1_update(c, final_count, sizeof(final_count));
  for (unsigned i = 0; i < SHA1_DIGEST_LENGTH; i++) {
    out[i] = (uint8_t)((c->sha1_state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
  secure_clear(c->sha1_state, sizeof(c->sha1_state));
  secure_clear(c->sha1_buffer, sizeof(c->sha1_buffer));
  c->sha1_count = 0;
}

#undef SHA1_R4
#undef SHA1_R3
#undef SHA1_R2
#undef SHA1_R1
#undef SHA1_R0
#undef SHA1_BLK
#undef SHA1_BLK0
#undef SHA1_ROL

static void hmac_init(struct aes_hmac_ctx *c, const uint8_t *key, unsigned length) {
  uint8_t ipad[SHA1_BLOCK_LENGTH], hashed_key[SHA1_DIGEST_LENGTH];
  if (length > SHA1_BLOCK_LENGTH) {
    sha1_init(c);
    sha1_update(c, key, length);
    sha1_final(c, hashed_key);
    key = hashed_key;
    length = SHA1_DIGEST_LENGTH;
  }
  for (unsigned i = 0; i < SHA1_BLOCK_LENGTH; i++) {
    uint8_t k = i < length ? key[i] : 0;
    ipad[i] = (uint8_t)(k ^ 0x36);
    c->opad[i] = (uint8_t)(k ^ 0x5c);
  }
  sha1_init(c);
  sha1_update(c, ipad, SHA1_BLOCK_LENGTH);
}

static void hmac_final(struct aes_hmac_ctx *c, uint8_t out[SHA1_DIGEST_LENGTH]) {
  uint8_t inner[SHA1_DIGEST_LENGTH];
  sha1_final(c, inner);
  sha1_init(c);
  sha1_update(c, c->opad, SHA1_BLOCK_LENGTH);
  sha1_update(c, inner, SHA1_DIGEST_LENGTH);
  sha1_final(c, out);
}

unsigned aes_hmac_new(void) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)malloc(sizeof(*c));
  if (!c) {
    return 0;
  }
  memset(c, 0, sizeof(*c));
  return (unsigned)(uintptr_t)c;
}

int aes_hmac_init(unsigned ctx, unsigned key_ptr, unsigned key_length, unsigned auth_ptr, unsigned auth_length) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  if (!c || (key_length != 16 && key_length != 24 && key_length != 32)) {
    return -1;
  }
  if (!tables_ready) {
    init_tables();
  }
  expand_key(c, (const uint8_t *)(uintptr_t)key_ptr, key_length);
  memset(c->counter, 0, sizeof(c->counter));
  hmac_init(c, (const uint8_t *)(uintptr_t)auth_ptr, auth_length);
  return 0;
}

void aes_hmac_process(unsigned ctx, unsigned ptr, unsigned length, int decrypt) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  uint8_t *p = (uint8_t *)(uintptr_t)ptr;
  uint32_t keystream[4], words[4];
  if (!c) {
    return;
  }
  if (decrypt) {
    sha1_update(c, p, length);
  }
  for (unsigned i = 0; i < length; i += BLOCK_LENGTH) {
    ++c->counter[0];
    encrypt_block(c, (const uint8_t *)c->counter, (uint8_t *)keystream);
    if (length - i >= BLOCK_LENGTH) {
      memcpy(words, p + i, BLOCK_LENGTH);
      words[0] ^= keystream[0];
      words[1] ^= keystream[1];
      words[2] ^= keystream[2];
      words[3] ^= keystream[3];
      memcpy(p + i, words, BLOCK_LENGTH);
    } else {
      const uint8_t *k = (const uint8_t *)keystream;
      for (unsigned j = 0; j < length - i; j++) {
        p[i + j] ^= k[j];
      }
    }
  }
  if (!decrypt) {
    sha1_update(c, p, length);
  }
}

void aes_hmac_end(unsigned ctx, unsigned out_ptr) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  if (!c) {
    return;
  }
  if (out_ptr) {
    hmac_final(c, (uint8_t *)(uintptr_t)out_ptr);
  }
  secure_clear(c, sizeof(*c));
  free(c);
}
