#pragma once

#include "../GameplayActionCore/VansGameplayModuleContributor.h"

#include <memory>

namespace Vans
{
std::shared_ptr<const IVansGameplayModuleContributor>
VansMakeGameplayPrimitivesGAFContributor();
}
