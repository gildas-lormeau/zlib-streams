// Instantiate GUID symbols for the SDK (one TU must define INITGUID)
// Build one TU that defines GUID instances used by the SDK.
#define INITGUID
#include "Common/MyInitGuid.h"
// Including MyInitGuid.h will include MyGuidDef.h and initialize IID_IUnknown
// and other GUIDs declared with DEFINE_GUID in headers that get compiled into
// the project.
