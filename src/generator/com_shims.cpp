// Minimal COM/BSTR/VARIANT shims to satisfy limited SDK usage on macOS
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Minimal BSTR implementation: allocate wchar_t* with length prefixed (not full
// COM BSTR semantics)
extern "C" {
typedef wchar_t *BSTR;

BSTR SysAllocStringLen(const wchar_t *src, unsigned int len) {
  // allocate len+1 wchar_t and copy
  BSTR b = (BSTR)malloc((len + 1) * sizeof(wchar_t));
  if (!b)
    return NULL;
  if (src && len)
    wmemcpy(b, src, len);
  b[len] = 0;
  return b;
}

BSTR SysAllocString(const wchar_t *src) {
  if (!src)
    return NULL;
  unsigned int len = (unsigned int)wcslen(src);
  return SysAllocStringLen(src, len);
}

void SysFreeString(BSTR b) {
  if (b)
    free(b);
}

// Minimal VARIANT-like helpers: SDK sometimes calls VariantClear/VariantCopy on
// PROPVARIANT wrappers. Provide no-op implementations that assume the SDK isn't
// relying on full COM Variant semantics.
typedef struct tagVARIANT {
  void *_v;
} VARIANT;

int VariantClear(VARIANT *v) {
  if (!v)
    return 0;
  // nothing to free in our minimal stub
  v->_v = NULL;
  return 0; // S_OK
}

int VariantCopy(VARIANT *dest, const VARIANT *src) {
  if (!dest || !src)
    return -1; // E_POINTER
  dest->_v = src->_v;
  return 0; // S_OK
}

int VariantInit(VARIANT *v) {
  if (!v)
    return -1;
  v->_v = NULL;
  return 0;
}
}
