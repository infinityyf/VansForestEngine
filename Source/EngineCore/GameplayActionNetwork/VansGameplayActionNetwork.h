#pragma once

#include "../GameplayActionCore/VansActionHost.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
inline constexpr std::uint32_t VansGameplayActionNetworkMagic = 0x31464147u;
inline constexpr std::uint16_t VansGameplayActionProtocolVersion = 1;

enum class VansActionNetworkMessageType : std::uint8_t
{
	Handshake,
	ActivationRequest,
	ActivationConfirm,
	ActivationReject,
	ActivationCorrection,
	SpecDelta,
	ActionStateDelta,
	ActionEvent,
	EffectDelta,
	Cue,
	ActionResult
};

enum class VansActionNetworkError : std::uint8_t
{
	None,
	Malformed,
	BudgetExceeded,
	ProtocolMismatch,
	DictionaryMismatch,
	ContentMismatch,
	HashMismatch,
	Duplicate,
	TooOld,
	RateLimited,
	Disconnected,
	AuthorityRejected
};

struct VansActionNetworkLimits
{
	std::size_t maximumPacketBytes = 64 * 1024;
	std::size_t maximumPayloadBytes = 60 * 1024;
	std::size_t maximumStringBytes = 16 * 1024;
	std::size_t maximumContainerItems = 4096;
	std::size_t maximumDepth = 32;
};

struct VansActionNetworkHeader
{
	std::uint16_t protocolVersion = VansGameplayActionProtocolVersion;
	VansActionNetworkMessageType type = VansActionNetworkMessageType::Handshake;
	std::uint8_t flags = 0;
	std::uint32_t connection = 0;
	std::uint32_t sequence = 0;
	std::uint32_t acknowledgement = 0;
	std::uint64_t dictionaryVersion = 0;
	std::uint64_t contentManifestHash = 0;
};

struct VansActionNetworkPacket
{
	VansActionNetworkHeader header;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansActionNetworkResult
{
	VansActionNetworkError error = VansActionNetworkError::None;
	std::string message;
	explicit operator bool() const { return error == VansActionNetworkError::None; }
};

class VansActionNetworkCodec
{
public:
	static VansActionNetworkResult Encode(const VansActionNetworkPacket& packet,
		std::vector<std::uint8_t>& bytes,
		const VansActionNetworkLimits& limits = {});
	static VansActionNetworkResult Decode(const std::vector<std::uint8_t>& bytes,
		VansActionNetworkPacket& packet,
		const VansActionNetworkLimits& limits = {});
	static std::uint64_t PayloadHash(const std::vector<std::uint8_t>& bytes);
};

struct VansActionNetworkPeerPolicy
{
	std::uint16_t protocolVersion = VansGameplayActionProtocolVersion;
	std::uint64_t dictionaryVersion = 0;
	std::uint64_t contentManifestHash = 0;
	double maximumPacketsPerSecond = 120.0;
	double burstPackets = 32.0;
	bool requireDictionaryMatch = true;
	bool requireContentMatch = true;
};

class VansActionNetworkGate
{
public:
	explicit VansActionNetworkGate(VansActionNetworkPeerPolicy policy = {})
		: m_Policy(policy) {}
	VansActionNetworkResult Accept(const VansActionNetworkPacket& packet, double nowSeconds);
	void Reset(std::uint32_t connection);

private:
	struct PeerState
	{
		double tokens = 0.0;
		double lastTime = 0.0;
		std::uint32_t highestSequence = 0;
		std::uint64_t replayWindow = 0;
		bool initialized = false;
	};

	VansActionNetworkPeerPolicy m_Policy;
	std::unordered_map<std::uint32_t, PeerState> m_Peers;
};

struct VansActionLoopbackConfig
{
	std::uint32_t latencyTicks = 0;
	std::uint32_t dropEvery = 0;
	std::uint32_t duplicateEvery = 0;
	std::uint32_t reorderEvery = 0;
	std::size_t maximumQueuedPackets = 4096;
};

class VansActionLoopbackTransport
{
public:
	explicit VansActionLoopbackTransport(VansActionLoopbackConfig config = {})
		: m_Config(config) {}
	bool Send(std::uint32_t source, std::uint32_t destination,
		std::vector<std::uint8_t> packet, std::string& error);
	void Advance(std::uint32_t ticks = 1);
	bool Receive(std::uint32_t destination, std::vector<std::uint8_t>& packet,
		std::uint32_t* source = nullptr);
	void Disconnect(std::uint32_t connection);
	void Reconnect(std::uint32_t connection);
	std::size_t QueuedCount() const { return m_InFlight.size(); }

private:
	struct Envelope
	{
		std::uint32_t source = 0;
		std::uint32_t destination = 0;
		std::uint64_t deliveryTick = 0;
		std::uint64_t order = 0;
		std::vector<std::uint8_t> packet;
	};

	VansActionLoopbackConfig m_Config;
	std::uint64_t m_Tick = 0;
	std::uint64_t m_SendCount = 0;
	std::uint64_t m_NextOrder = 1;
	std::vector<Envelope> m_InFlight;
	std::unordered_map<std::uint32_t, std::deque<Envelope>> m_Received;
	std::unordered_map<std::uint32_t, bool> m_Disconnected;
};

enum class VansPredictionRecordState : std::uint8_t
{
	Pending,
	Confirmed,
	Rejected,
	Corrected
};

struct VansPredictionRecordView
{
	VansPredictionKey key;
	VansEntityHandle owner;
	VansActionHandle action;
	std::uint64_t contentHash = 0;
	VansPredictionRecordState state = VansPredictionRecordState::Pending;
	std::string reason;
};

class VansGameplayPredictionManager
{
public:
	bool Begin(VansPredictionKey key, std::shared_ptr<VansActionHost> host,
		VansActionHandle action, std::uint64_t contentHash, std::string& error);
	bool Confirm(VansPredictionKey key, std::string& error);
	bool Reject(VansPredictionKey key, std::string reason, std::string& error);
	bool Correct(VansPredictionKey key, std::function<bool(std::string&)> applyCorrection,
		std::string& error);
	std::optional<VansPredictionRecordView> Query(VansPredictionKey key) const;
	std::vector<VansPredictionRecordView> Records() const;
	void ForgetResolved();

private:
	struct KeyHash
	{
		std::size_t operator()(VansPredictionKey key) const noexcept
		{
			return (static_cast<std::size_t>(key.connection) << 32u) ^ key.sequence;
		}
	};
	struct Record
	{
		VansPredictionRecordView view;
		std::weak_ptr<VansActionHost> host;
	};
	std::unordered_map<VansPredictionKey, Record, KeyHash> m_Records;
};

struct VansActionActivationNetworkMessage
{
	VansActionId action;
	VansActionContext context;
	VansTargetData targetData;
	bool hasTargetData = false;
	std::uint64_t definitionContentHash = 0;
};

using VansTargetDataNetworkPolicy = VansTargetDataValidationPolicy;

VansSerializedValue VansEncodeActionActivationMessage(
	const VansActionActivationNetworkMessage& message);
bool VansDecodeActionActivationMessage(const VansSerializedValue& value,
	VansActionActivationNetworkMessage& message, std::string& error,
	const VansTargetDataNetworkPolicy& targetPolicy = {});
}
