// =============================================================================
// LTCData.cpp — Definitions of kLTC1 / kLTC2 are pulled from the generated .inl
// produced by the offline LTC LUT generator.
//
// The .inl is intentionally NOT committed (it is large auto-generated data).
// Run the generator script once after first checkout:
//
//      Rebuild the generated LTC table with the offline LUT generator.
//
// After generation, the .inl will provide the bodies of:
//      const float VansGraphics::LTC::kLTC1[kLUTFloats] = { ... };
//      const float VansGraphics::LTC::kLTC2[kLUTFloats] = { ... };
// =============================================================================
#include "LTCData.h"

#if __has_include("LTCData.generated.inl")
    #include "LTCData.generated.inl"
#else
    // ---------------------------------------------------------------------
    // Fallback definitions so the engine still links before the generator
    // has been run.  These zero arrays will produce a black area-light
    // contribution; a startup log warns the developer.  Replace by running
    // the offline LTC LUT generator next to this file.
    // ---------------------------------------------------------------------
    namespace VansGraphics { namespace LTC {
        const float kLTC1[kLUTFloats] = { 0.0f };
        const float kLTC2[kLUTFloats] = { 0.0f };
    }}
    #pragma message("[LTC] LTCData.generated.inl missing -- rebuild the offline LTC LUT data.")
#endif
