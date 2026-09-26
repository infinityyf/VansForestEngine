#pragma once

#include "VansTimelineApplierRegistry.h"

#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansTimelinePreAnimatedStoreResult
{
	bool accepted = false;
	VansTimelineWriterHandle overlappingWriter;
};

class VansTimelinePreAnimatedState
{
public:
	void BindAppliers(const VansTimelineApplierRegistry* appliers) { m_Appliers = appliers; }
	VansTimelinePreAnimatedStoreResult Store(VansTimelineRestoreToken token);
	bool ReleaseWriter(VansTimelineWriterHandle writer, bool restore);
	void RestoreAll();

private:
	struct TokenKey
	{
		VansTimelineRestoreHandle handle;
		VansTimelineApplierSlot applier = VansInvalidTimelineApplierSlot;
		friend bool operator==(const TokenKey& left, const TokenKey& right)
		{ return left.handle == right.handle && left.applier == right.applier; }
	};
	struct StoredToken
	{
		VansTimelineRestoreToken token;
		bool pending = false;
		bool restore = false;
	};
	const VansTimelineApplierRegistry* m_Appliers = nullptr;
	std::vector<StoredToken> m_Tokens;
	std::unordered_map<VansTimelineResourceId, std::vector<TokenKey>, VansTimelineResourceIdHash> m_ResourceStacks;
};
}
