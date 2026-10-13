#pragma once
#include "../VansNavigationMeshAsset.h"
#include <DetourTileCacheBuilder.h>

namespace Vans
{
// 每个 RLE block 至多 128 字节；同一编码器用于构建与资产层校验。
class VansNavigationTileCacheLayerCompressor final : public dtTileCacheCompressor
{
public:
    int maxCompressedSize(int size) override;
    dtStatus compress(const unsigned char* source,int size,unsigned char* target,int capacity,int* written) override;
    dtStatus decompress(const unsigned char* source,int size,unsigned char* target,int capacity,int* written) override;
};
// 只消费拥有型 CPU bytes；不初始化 native cache，不触碰 Scene 或 I/O。
// maximumBytes 包含索引、压缩 bytes 和单层解压 scratch。
bool ValidateNavigationTileCacheLayerSet(const std::vector<VansNavigationTileCacheLayerPayload>& layers,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    std::uint64_t maximumBytes,std::string& error);
bool ValidateNavigationTileLayerData(const std::vector<VansNavigationTileLayerData>& rows,std::uint64_t baseHash,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,std::uint64_t maximumBytes,std::string& error);
}

