#pragma once

#include "VansEventTypeId.h"

#include <string>

namespace Vans
{
	struct VansEventMetadata
	{
		VansEventTypeId typeId = 0;
		std::string name;
		std::string category;
		bool scriptVisible = false;
		bool editorVisible = true;
	};
}
