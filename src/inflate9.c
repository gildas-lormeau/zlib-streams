/* inflate9.c -- zlib decompression
 * Copyright (C) 2025 Gildas Lormeau (inflate9.c) - 1995-2022 Mark Adler (zlib)
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zutil.h"
#include "inftree9.h"
#include "inflate.h"

#include "infback9.h"

extern int ZEXPORT inflate9Init2_(z_streamp strm, int windowBits,
                                  const char *version, int stream_size);

int ZEXPORT inflate9Init(z_streamp strm) {
  return inflate9Init2_(strm, 16, ZLIB_VERSION, (int)sizeof(z_stream));
}

static unsigned in(void *in_desc, unsigned char FAR **next);

static const unsigned char order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                        11, 4,  12, 3, 13, 2, 14, 1, 15};

local int inflateStateCheck(z_streamp strm) {
  struct inflate_state FAR *state;
  if (strm == Z_NULL || strm->zalloc == (alloc_func)0 ||
      strm->zfree == (free_func)0)
    return 1;
  state = (struct inflate_state FAR *)strm->state;
  if (state == Z_NULL || state->strm != strm || state->mode < TYPE ||
      state->mode > BAD)
    return 1;
  return 0;
}

int ZEXPORT inflate9ResetKeep(z_streamp strm) {
  struct inflate_state FAR *state;

  if (inflateStateCheck(strm))
    return Z_STREAM_ERROR;
  state = (struct inflate_state FAR *)strm->state;
  strm->total_in = strm->total_out = state->total = 0;
  strm->msg = Z_NULL;
  if (state->wrap) /* to support ill-conceived Java test suite */
    strm->adler = state->wrap & 1;
  state->mode = TYPE;
  state->last = 0;
  state->havedict = 0;
  state->flags = -1;
  state->dmax = 65536U;
  state->head = Z_NULL;
  state->hold = 0;
  state->bits = 0;
  state->lencode = state->distcode = state->next = state->codes;
  state->sane = 1;
  state->back = -1;
  Tracev((stderr, "inflate: reset\n"));
  return Z_OK;
}

int ZEXPORT inflate9Reset(z_streamp strm) {
  struct inflate_state FAR *state;

  if (inflateStateCheck(strm))
    return Z_STREAM_ERROR;
  state = (struct inflate_state FAR *)strm->state;
  state->wsize = 0;
  state->whave = 0;
  state->wnext = 0;
  return inflate9ResetKeep(strm);
}

int ZEXPORT inflate9Reset2(z_streamp strm, int windowBits) {
  int wrap;
  struct inflate_state FAR *state;

  /* get the state */
  if (inflateStateCheck(strm))
    return Z_STREAM_ERROR;
  state = (struct inflate_state FAR *)strm->state;

  /* extract wrap request from windowBits parameter */
  if (windowBits < 0) {
    if (windowBits < -16)
      return Z_STREAM_ERROR;
    wrap = 0;
    windowBits = -windowBits;
  } else {
    wrap = (windowBits >> 4) + 5;
  }
  /* set number of window bits, free window if different */
  if (windowBits && (windowBits < 8 || windowBits > 16))
    return Z_STREAM_ERROR;
  if (state->window != Z_NULL && state->wbits != (unsigned)windowBits) {
    ZFREE(strm, state->window);
    state->window = Z_NULL;
  }
  /* update state and reset the rest of it */
  state->wrap = wrap;
  state->wbits = (unsigned)windowBits;
  return inflate9Reset(strm);
}

int ZEXPORT inflate9Init2_(z_streamp strm, int windowBits, const char *version,
                           int stream_size) {
  int ret;
  struct inflate_state FAR *state;

  if (version == Z_NULL || version[0] != ZLIB_VERSION[0] ||
      stream_size != (int)(sizeof(z_stream)))
    return Z_VERSION_ERROR;
  if (strm == Z_NULL)
    return Z_STREAM_ERROR;
  strm->msg = Z_NULL; /* in case we return an error */
  if (strm->zalloc == (alloc_func)0) {
    strm->zalloc = zcalloc;
    strm->opaque = (voidpf)0;
  }
  if (strm->zfree == (free_func)0)
    strm->zfree = zcfree;
  state =
      (struct inflate_state FAR *)ZALLOC(strm, 1, sizeof(struct inflate_state));
  if (state == Z_NULL)
    return Z_MEM_ERROR;
  Tracev((stderr, "inflate: allocated\n"));
  strm->state = (struct internal_state FAR *)state;
  state->strm = strm;
  state->window = Z_NULL;
  state->mode = TYPE;
  ret = inflate9Reset2(strm, windowBits);
  if (ret != Z_OK) {
    ZFREE(strm, state);
    strm->state = Z_NULL;
  }
  return ret;
}

int ZEXPORT inflate9Init_(z_streamp strm, const char *version,
                          int stream_size) {
  return inflate9Init2_(strm, DEF_WBITS, version, stream_size);
}

static unsigned in(void *in_desc, unsigned char FAR **next) {
  z_stream *strm = (z_stream *)in_desc;

  if (strm == Z_NULL || strm->next_in == Z_NULL || strm->avail_in == 0) {
    *next = Z_NULL;
    return 0;
  }
  *next = strm->next_in;
  return strm->avail_in;
}

void fixedtables(struct inflate_state FAR *state) {
  static int virgin = 1;
  static code *lenfix, *distfix;
  static code fixed[544];
  if (virgin) {
    unsigned sym, bits;
    static code *next;
    sym = 0;
    while (sym < 144)
      state->lens[sym++] = 8;
    while (sym < 256)
      state->lens[sym++] = 9;
    while (sym < 280)
      state->lens[sym++] = 7;
    while (sym < 288)
      state->lens[sym++] = 8;
    next = fixed;
    lenfix = next;
    bits = 9;
    inflate_table9(LENS, state->lens, 288, &(next), &(bits), state->work);

    /* distance table */
    sym = 0;
    while (sym < 32)
      state->lens[sym++] = 5;
    distfix = next;
    bits = 5;
    inflate_table9(DISTS, state->lens, 32, &(next), &(bits), state->work);

    /* do this just once */
    virgin = 0;
  }
  state->lencode = lenfix;
  state->lenbits = 9;
  state->distcode = distfix;
  state->distbits = 5;
}

local int updatewindow(z_streamp strm, const Bytef *end, unsigned copy) {
  struct inflate_state FAR *state;
  unsigned dist;

  state = (struct inflate_state FAR *)strm->state;

  /* if it hasn't been done already, allocate space for the window */
  if (state->window == Z_NULL) {
    state->window = (unsigned char FAR *)ZALLOC(strm, 1U << state->wbits,
                                                sizeof(unsigned char));
    if (state->window == Z_NULL)
      return 1;
  }

  /* if window not in use yet, initialize */
  if (state->wsize == 0) {
    state->wsize = 1U << state->wbits;
    state->wnext = 0;
    state->whave = 0;
  }

  /* copy state->wsize or less output bytes into the circular window */
  if (copy >= state->wsize) {
    zmemcpy(state->window, end - state->wsize, state->wsize);
    state->wnext = 0;
    state->whave = state->wsize;
  } else {
    dist = state->wsize - state->wnext;
    if (dist > copy)
      dist = copy;
    zmemcpy(state->window + state->wnext, end - copy, dist);
    copy -= dist;
    if (copy) {
      zmemcpy(state->window, end - copy, copy);
      state->wnext = copy;
      state->whave = state->wsize;
    } else {
      state->wnext += dist;
      if (state->wnext == state->wsize)
        state->wnext = 0;
      if (state->whave < state->wsize)
        state->whave += dist;
    }
  }
  return 0;
}

/* Load registers with state in inflate() for speed */
#define LOAD()                                                                 \
  do {                                                                         \
    put = strm->next_out;                                                      \
    left = strm->avail_out;                                                    \
    next = strm->next_in;                                                      \
    have = strm->avail_in;                                                     \
    hold = state->hold;                                                        \
    bits = state->bits;                                                        \
  } while (0)

/* Restore state from registers in inflate() */
#define RESTORE()                                                              \
  do {                                                                         \
    strm->next_out = put;                                                      \
    strm->avail_out = left;                                                    \
    strm->next_in = next;                                                      \
    strm->avail_in = have;                                                     \
    state->hold = hold;                                                        \
    state->bits = bits;                                                        \
  } while (0)

/* Clear the input bit accumulator */
#define INITBITS()                                                             \
  do {                                                                         \
    hold = 0;                                                                  \
    bits = 0;                                                                  \
  } while (0)

#define PULLBYTE()                                                             \
  do {                                                                         \
    if (have == 0)                                                             \
      goto inf_leave;                                                          \
    have--;                                                                    \
    hold += (unsigned long)(*next++) << bits;                                  \
    bits += 8;                                                                 \
  } while (0)

#define NEEDBITS(n)                                                            \
  do {                                                                         \
    while (bits < (unsigned)(n))                                               \
      PULLBYTE();                                                              \
  } while (0)

#define BITS(n) ((unsigned)hold & ((1U << (n)) - 1))

#define DROPBITS(n)                                                            \
  do {                                                                         \
    hold >>= (n);                                                              \
    bits -= (unsigned)(n);                                                     \
  } while (0)

#define BYTEBITS()                                                             \
  do {                                                                         \
    hold >>= bits & 7;                                                         \
    bits -= bits & 7;                                                          \
  } while (0)

int ZEXPORT inflate9(z_streamp strm, int flush) {
  struct inflate_state FAR *state;
  z_const unsigned char FAR *next; /* next input */
  unsigned char FAR *put;          /* next output */
  unsigned have, left;             /* available input and output */
  unsigned long hold;              /* bit buffer */
  unsigned bits;                   /* bits in bit buffer */
  unsigned in, out;        /* save starting available input and output */
  unsigned copy;           /* number of stored or match bytes to copy */
  unsigned char FAR *from; /* where to copy match bytes from */
  code here;               /* current decoding table entry */
  code last;               /* parent table entry */
  unsigned len;            /* length to copy for repeats, bits to drop */
  int ret;                 /* return code */
  static const unsigned short order[19] = /* permutation of code lengths */
      {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

  if (inflateStateCheck(strm) || strm->next_out == Z_NULL ||
      (strm->next_in == Z_NULL && strm->avail_in != 0))
    return Z_STREAM_ERROR;

  state = (struct inflate_state FAR *)strm->state;
  LOAD();
  in = have;
  out = left;
  ret = Z_OK;
  for (;;)
    switch (state->mode) {
    case TYPE:
      if (flush == Z_BLOCK || flush == Z_TREES)
        goto inf_leave;
      /* fallthrough */
    case TYPEDO:
      if (state->last) {
        BYTEBITS();
        state->mode = DONE;
        break;
      }
      NEEDBITS(3);
      state->last = BITS(1);
      DROPBITS(1);
      switch (BITS(2)) {
      case 0: /* stored block */
        Tracev((stderr, "inflate:     stored block%s\n",
                state->last ? " (last)" : ""));
        state->mode = STORED;
        break;
      case 1: /* fixed block */
        fixedtables(state);
        Tracev((stderr, "inflate:     fixed codes block%s\n",
                state->last ? " (last)" : ""));
        state->mode = LEN_; /* decode codes */
        if (flush == Z_TREES) {
          DROPBITS(2);
          goto inf_leave;
        }
        break;
      case 2: /* dynamic block */
        Tracev((stderr, "inflate:     dynamic codes block%s\n",
                state->last ? " (last)" : ""));
        state->mode = TABLE;
        break;
      case 3:
        strm->msg = (z_const char *)"invalid block type";
        state->mode = BAD;
      }
      DROPBITS(2);
      break;
    case STORED:
      BYTEBITS();
      NEEDBITS(32);
      if ((hold & 0xffff) != ((hold >> 16) ^ 0xffff)) {
        strm->msg = (z_const char *)"invalid stored block lengths";
        state->mode = BAD;
        break;
      }
      state->length = (unsigned)hold & 0xffff;
      Tracev((stderr, "inflate:       stored length %u\n", state->length));
      INITBITS();
      state->mode = COPY_;
      if (flush == Z_TREES)
        goto inf_leave;
      /* fallthrough */
    case COPY_:
      state->mode = COPY;
      /* fallthrough */
    case COPY:
      copy = state->length;
      if (copy) {
        if (copy > have)
          copy = have;
        if (copy > left)
          copy = left;
        if (copy == 0)
          goto inf_leave;
        zmemcpy(put, next, copy);
        have -= copy;
        next += copy;
        left -= copy;
        put += copy;
        state->length -= copy;
        break;
      }
      Tracev((stderr, "inflate:       stored end\n"));
      state->mode = TYPE;
      break;
    case TABLE:
      NEEDBITS(14);
      state->nlen = BITS(5) + 257;
      DROPBITS(5);
      state->ndist = BITS(5) + 1;
      DROPBITS(5);
      state->ncode = BITS(4) + 4;
      DROPBITS(4);
      if (state->nlen > 286) {
        strm->msg = (z_const char *)"too many length or distance symbols";
        state->mode = BAD;
        break;
      }
      Tracev((stderr, "inflate:       table sizes ok\n"));
      state->have = 0;
      state->mode = LENLENS;
      /* fallthrough */
    case LENLENS:
      while (state->have < state->ncode) {
        NEEDBITS(3);
        state->lens[order[state->have++]] = (unsigned short)BITS(3);
        DROPBITS(3);
      }
      while (state->have < 19)
        state->lens[order[state->have++]] = 0;
      state->next = state->codes;
      state->lencode = state->distcode = (const code FAR *)(state->next);
      state->lenbits = 7;
      ret = inflate_table9(CODES, state->lens, 19, &(state->next),
                           &(state->lenbits), state->work);
      if (ret) {
        strm->msg = (z_const char *)"invalid code lengths set";
        state->mode = BAD;
        break;
      }
      Tracev((stderr, "inflate:       code lengths ok\n"));
      state->have = 0;
      state->mode = CODELENS;
      /* fallthrough */
    case CODELENS:
      while (state->have < state->nlen + state->ndist) {
        for (;;) {
          here = state->lencode[BITS(state->lenbits)];
          if ((unsigned)(here.bits) <= bits)
            break;
          PULLBYTE();
        }
        if (here.val < 16) {
          DROPBITS(here.bits);
          state->lens[state->have++] = here.val;
        } else {
          if (here.val == 16) {
            NEEDBITS(here.bits + 2);
            DROPBITS(here.bits);
            if (state->have == 0) {
              strm->msg = (z_const char *)"invalid bit length repeat";
              state->mode = BAD;
              break;
            }
            len = state->lens[state->have - 1];
            copy = 3 + BITS(2);
            DROPBITS(2);
          } else if (here.val == 17) {
            NEEDBITS(here.bits + 3);
            DROPBITS(here.bits);
            len = 0;
            copy = 3 + BITS(3);
            DROPBITS(3);
          } else {
            NEEDBITS(here.bits + 7);
            DROPBITS(here.bits);
            len = 0;
            copy = 11 + BITS(7);
            DROPBITS(7);
          }
          if (state->have + copy > state->nlen + state->ndist) {
            strm->msg = (z_const char *)"invalid bit length repeat";
            state->mode = BAD;
            break;
          }
          while (copy--)
            state->lens[state->have++] = (unsigned short)len;
        }
      }

      /* handle error breaks in while */
      if (state->mode == BAD)
        break;

      /* check for end-of-block code (better have one) */
      if (state->lens[256] == 0) {
        strm->msg = (z_const char *)"invalid code -- missing end-of-block";
        state->mode = BAD;
        break;
      }

      state->next = state->codes;
      state->lencode = (const code FAR *)(state->next);
      ret = inflate_table9(LENS, state->lens, state->nlen, &(state->next),
                           &(state->lenbits), state->work);
      if (ret) {
        strm->msg = (z_const char *)"invalid literal/lengths set";
        state->mode = BAD;
        break;
      }
      state->distcode = (const code FAR *)(state->next);
      ret = inflate_table9(DISTS, state->lens + state->nlen, state->ndist,
                           &(state->next), &(state->distbits), state->work);
      if (ret) {
        strm->msg = (z_const char *)"invalid distances set";
        state->mode = BAD;
        break;
      }
      Tracev((stderr, "inflate:       codes ok\n"));
      state->mode = LEN_;
      if (flush == Z_TREES)
        goto inf_leave;
      /* fallthrough */
    case LEN_:
      state->mode = LEN;
      /* fallthrough */
    case LEN:
      state->back = 0;
      for (;;) {
        here = state->lencode[BITS(state->lenbits)];
        if ((unsigned)(here.bits) <= bits)
          break;
        PULLBYTE();
      }
      if (here.op && (here.op & 0xf0) == 0) {
        last = here;
        for (;;) {
          here = state->lencode[last.val +
                                (BITS(last.bits + last.op) >> last.bits)];
          if ((unsigned)(last.bits + here.bits) <= bits)
            break;
          PULLBYTE();
        }
        DROPBITS(last.bits);
        state->back += last.bits;
      }
      DROPBITS(here.bits);
      state->back += here.bits;
      state->length = (unsigned)here.val;
      if ((int)(here.op) == 0) {
        Tracevv((stderr,
                 here.val >= 0x20 && here.val < 0x7f
                     ? "inflate:         literal '%c'\n"
                     : "inflate:         literal 0x%02x\n",
                 here.val));
        state->mode = LIT;
        break;
      }
      if (here.op & 32) {
        Tracevv((stderr, "inflate:         end of block\n"));
        state->back = -1;
        state->mode = TYPE;
        break;
      }
      if (here.op & 64) {
        strm->msg = (z_const char *)"invalid literal/length code";
        state->mode = BAD;
        break;
      }
      state->extra = (unsigned)(here.op) & 15;
      state->mode = LENEXT;
      /* fallthrough */
    case LENEXT:
      if (state->extra) {
        NEEDBITS(state->extra);
        state->length += BITS(state->extra);
        DROPBITS(state->extra);
        state->back += state->extra;
      }
      Tracevv((stderr, "inflate:         length %u\n", state->length));
      state->was = state->length;
      state->mode = DIST;
      /* fallthrough */
    case DIST:
      for (;;) {
        here = state->distcode[BITS(state->distbits)];
        if ((unsigned)(here.bits) <= bits)
          break;
        PULLBYTE();
      }
      if ((here.op & 0xf0) == 0) {
        last = here;
        for (;;) {
          here = state->distcode[last.val +
                                 (BITS(last.bits + last.op) >> last.bits)];
          if ((unsigned)(last.bits + here.bits) <= bits)
            break;
          PULLBYTE();
        }
        DROPBITS(last.bits);
        state->back += last.bits;
      }
      DROPBITS(here.bits);
      state->back += here.bits;
      if (here.op & 64) {
        strm->msg = (z_const char *)"invalid distance code";
        state->mode = BAD;
        break;
      }
      state->offset = (unsigned)here.val;
      state->extra = (unsigned)(here.op) & 15;
      state->mode = DISTEXT;
      /* fallthrough */
    case DISTEXT:
      if (state->extra) {
        NEEDBITS(state->extra);
        state->offset += BITS(state->extra);
        DROPBITS(state->extra);
        state->back += state->extra;
      }
      if (state->offset > state->dmax) {
        strm->msg = (z_const char *)"invalid distance too far back";
        state->mode = BAD;
        break;
      }
      Tracevv((stderr, "inflate:         distance %u\n", state->offset));
      state->mode = MATCH;
      /* fallthrough */
    case MATCH:
      if (left == 0)
        goto inf_leave;
      copy = out - left;
      if (state->offset > copy) { /* copy from window */
        copy = state->offset - copy;
        if (copy > state->whave) {
          if (state->sane) {
            strm->msg = (z_const char *)"invalid distance too far back";
            state->mode = BAD;
            break;
          }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
          copy -= state->whave;
          if (copy > state->length)
            copy = state->length;
          if (copy > left)
            copy = left;
          left -= copy;
          state->length -= copy;
          do {
            *put++ = 0;
          } while (--copy);
          if (state->length == 0)
            state->mode = LEN;
          break;
#endif
        }
        if (copy > state->wnext) {
          copy -= state->wnext;
          from = state->window + (state->wsize - copy);
        } else
          from = state->window + (state->wnext - copy);
        if (copy > state->length)
          copy = state->length;
      } else { /* copy from output */
        from = put - state->offset;
        copy = state->length;
      }
      if (copy > left)
        copy = left;
      left -= copy;
      state->length -= copy;
      do {
        *put++ = *from++;
      } while (--copy);
      if (state->length == 0)
        state->mode = LEN;
      break;
    case LIT:
      if (left == 0)
        goto inf_leave;
      *put++ = (unsigned char)(state->length);
      left--;
      state->mode = LEN;
      break;
    case DONE:
      ret = Z_STREAM_END;
      goto inf_leave;
    case BAD:
      ret = Z_DATA_ERROR;
      goto inf_leave;
    default:
      return Z_STREAM_ERROR;
    }

inf_leave:
  state->hold = hold;
  RESTORE();
  if (state->wsize || (out != strm->avail_out && state->mode < BAD &&
                       (state->mode < DONE || flush != Z_FINISH)))
    if (updatewindow(strm, strm->next_out, out - strm->avail_out)) {
      state->mode = MEM;
      return Z_MEM_ERROR;
    }
  in -= strm->avail_in;
  out -= strm->avail_out;
  strm->total_in += in;
  strm->total_out += out;
  state->total += out;
  strm->data_type = (int)state->bits + (state->last ? 64 : 0) +
                    (state->mode == TYPE ? 128 : 0) +
                    (state->mode == LEN_ || state->mode == COPY_ ? 256 : 0);
  if (((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
    ret = Z_BUF_ERROR;
  return ret;
}

int ZEXPORT inflate9End(z_streamp strm) {
  struct inflate_state FAR *state;
  if (inflateStateCheck(strm))
    return Z_STREAM_ERROR;
  state = (struct inflate_state FAR *)strm->state;
  if (state->window != Z_NULL)
    ZFREE(strm, state->window);
  ZFREE(strm, strm->state);
  strm->state = Z_NULL;
  Tracev((stderr, "inflate: end\n"));
  return Z_OK;
}
