// Copyright (c) 2008-2020 NVIDIA Corporation. All rights reserved.
//
// PsSort.h - Compatibility stub for NvCloth extension sources.
// The original PsSort.h is an internal NvCloth source file not distributed
// with the public SDK.  This stub provides only what the extension cooker
// sources actually need:
//   - ps::sort()  (backed by std::sort)
//   - ps::NonTrackingAllocator  (aliased from nv::cloth::NonTrackingAllocator)
// Less<T> is already declared in NvCloth/ps/PsBasicTemplates.h.

#pragma once
#include <algorithm>
// Pull in the real NvCloth ps types so we don't redefine them.
#include "NvCloth/ps/PsBasicTemplates.h"  // Less<T>, Greater<T>, ...
#include "NvCloth/ps/PsAllocator.h"       // nv::cloth::NonTrackingAllocator

namespace nv
{
namespace cloth
{
namespace ps
{

// NonTrackingAllocator is already declared in nv::cloth::ps by PsAllocator.h above.
// No using declaration needed here.

// sort — delegates to std::sort; allocator argument is accepted but ignored.
template <class T, class Pred, class Alloc>
inline void sort(T* first, size_t count, const Pred& pred,
                 const Alloc& /*alloc*/ = NonTrackingAllocator())
{
    std::sort(first, first + count, pred);
}

} // namespace ps
} // namespace cloth
} // namespace nv
