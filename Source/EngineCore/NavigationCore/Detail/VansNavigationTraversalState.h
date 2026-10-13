#pragma once

#include "../VansNavigationTypes.h"
#include <algorithm>

namespace Vans
{
// Immutable and borrowed only during a query quantum. Dirty cells keep their
// old native bytes for rollback/diagnostics but are unavailable for traversal.
struct VansNavigationTraversalState
{
    std::uint64_t revision=0;
    std::vector<VansNavigationTileInputStamp> dirty;
    bool unavailable=false;

    const VansNavigationTileInputStamp* Find(VansNavigationTileCell cell) const
    {
        const auto found=std::lower_bound(dirty.begin(),dirty.end(),cell,
            [](const auto& stamp,const auto& key){return stamp.cell<key;});
        return found!=dirty.end() && found->cell==cell?&*found:nullptr;
    }
    std::uint64_t EstimateBytes() const
    { return sizeof(*this)+dirty.capacity()*sizeof(VansNavigationTileInputStamp); }
};
}
