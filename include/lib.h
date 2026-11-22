/* Library entry points (CRT, XAPI, D3D, XNet, ...) the app calls; they link to the RXDK SDK
 * (lib_sdk.h) or to the shims in src/sdk/ (sdk_shim.h). Aliases are other names the
 * reconstructed code uses for the same function. */
#pragma once
#include "types.h"

/* (lib_sdk.h and sdk_shim.h are included at the end of app.h: their prototypes use module types) */
