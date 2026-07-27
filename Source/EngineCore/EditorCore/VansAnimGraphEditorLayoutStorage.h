#pragma once

#include <string>
#include <vector>

namespace Vans
{
	struct AnimGraphEditorNodeLayout
	{
		std::string stateName;
		float x = 0.0f;
		float y = 0.0f;
	};

	class VansAnimGraphEditorLayoutStorage
	{
	public:
		static bool Save(
			const std::string& filePath,
			const std::vector<AnimGraphEditorNodeLayout>& layouts,
			std::string& error);
	};
}
