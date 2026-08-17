#include "VansGameplayActionNetwork.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace Vans
{
namespace
{
class Writer
{
public:
	template <typename T>
	void Pod(T value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
		using Storage = std::conditional_t<sizeof(T) == 1, std::uint8_t,
			std::conditional_t<sizeof(T) == 2, std::uint16_t,
			std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
		Storage raw = 0;
		std::memcpy(&raw, &value, sizeof(T));
		for (std::size_t index = 0; index < sizeof(T); ++index)
			bytes.push_back(static_cast<std::uint8_t>((raw >> (index * 8u)) & 0xffu));
	}
	void Raw(const void* source, std::size_t size)
	{
		if (size == 0) return;
		const auto* begin = static_cast<const std::uint8_t*>(source);
		bytes.insert(bytes.end(), begin, begin + size);
	}
	std::vector<std::uint8_t> bytes;
};

class Reader
{
public:
	Reader(const std::uint8_t* source, std::size_t size) : data(source), remaining(size) {}
	template <typename T>
	bool Pod(T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
		if (remaining < sizeof(T)) return false;
		using Storage = std::conditional_t<sizeof(T) == 1, std::uint8_t,
			std::conditional_t<sizeof(T) == 2, std::uint16_t,
			std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
		Storage raw = 0;
		for (std::size_t index = 0; index < sizeof(T); ++index)
			raw |= static_cast<Storage>(data[index]) << (index * 8u);
		std::memcpy(&value, &raw, sizeof(T));
		data += sizeof(T);
		remaining -= sizeof(T);
		return true;
	}
	bool Raw(void* destination, std::size_t size)
	{
		if (remaining < size) return false;
		std::memcpy(destination, data, size);
		data += size;
		remaining -= size;
		return true;
	}
	const std::uint8_t* data = nullptr;
	std::size_t remaining = 0;
};

bool WriteValue(Writer& writer, const VansSerializedValue& value,
	const VansActionNetworkLimits& limits, std::size_t depth, std::string& error)
{
	if (depth > limits.maximumDepth) { error = "Payload nesting budget exceeded"; return false; }
	writer.Pod(static_cast<std::uint8_t>(value.kind));
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Null: return true;
	case VansSerializedValue::Kind::Bool: writer.Pod(static_cast<std::uint8_t>(value.boolValue)); return true;
	case VansSerializedValue::Kind::Int: writer.Pod(value.intValue); return true;
	case VansSerializedValue::Kind::Float:
		if (!std::isfinite(value.floatValue)) { error = "Payload contains a non-finite number"; return false; }
		writer.Pod(value.floatValue); return true;
	case VansSerializedValue::Kind::String:
	{
		if (value.stringValue.size() > limits.maximumStringBytes)
			{ error = "Payload string budget exceeded"; return false; }
		writer.Pod(static_cast<std::uint32_t>(value.stringValue.size()));
		writer.Raw(value.stringValue.data(), value.stringValue.size());
		return true;
	}
	case VansSerializedValue::Kind::Array:
	{
		if (value.arrayItems.size() > limits.maximumContainerItems)
			{ error = "Payload array budget exceeded"; return false; }
		writer.Pod(static_cast<std::uint32_t>(value.arrayItems.size()));
		for (const auto& item : value.arrayItems)
			if (!WriteValue(writer, item, limits, depth + 1, error)) return false;
		return true;
	}
	case VansSerializedValue::Kind::Object:
	{
		if (value.objectFields.size() > limits.maximumContainerItems)
			{ error = "Payload object budget exceeded"; return false; }
		std::unordered_set<std::string> names;
		writer.Pod(static_cast<std::uint32_t>(value.objectFields.size()));
		for (const auto& [name, item] : value.objectFields)
		{
			if (name.size() > limits.maximumStringBytes || !names.insert(name).second)
				{ error = "Payload object contains an invalid field name"; return false; }
			writer.Pod(static_cast<std::uint32_t>(name.size()));
			writer.Raw(name.data(), name.size());
			if (!WriteValue(writer, item, limits, depth + 1, error)) return false;
		}
		return true;
	}
	}
	error = "Payload value kind is invalid";
	return false;
}

bool ReadString(Reader& reader, std::string& value,
	const VansActionNetworkLimits& limits, std::string& error)
{
	std::uint32_t size = 0;
	if (!reader.Pod(size) || size > limits.maximumStringBytes || size > reader.remaining)
		{ error = "Payload string is malformed"; return false; }
	value.resize(size);
	return size == 0 || reader.Raw(value.data(), size);
}

bool ReadValue(Reader& reader, VansSerializedValue& value,
	const VansActionNetworkLimits& limits, std::size_t depth, std::string& error)
{
	if (depth > limits.maximumDepth) { error = "Payload nesting budget exceeded"; return false; }
	std::uint8_t rawKind = 0;
	if (!reader.Pod(rawKind) || rawKind > static_cast<std::uint8_t>(VansSerializedValue::Kind::Object))
		{ error = "Payload value kind is malformed"; return false; }
	value = {};
	value.kind = static_cast<VansSerializedValue::Kind>(rawKind);
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Null: return true;
	case VansSerializedValue::Kind::Bool:
	{
		std::uint8_t raw = 0;
		if (!reader.Pod(raw) || raw > 1) { error = "Payload bool is malformed"; return false; }
		value.boolValue = raw != 0; return true;
	}
	case VansSerializedValue::Kind::Int: return reader.Pod(value.intValue);
	case VansSerializedValue::Kind::Float:
		if (!reader.Pod(value.floatValue) || !std::isfinite(value.floatValue))
			{ error = "Payload number is malformed"; return false; }
		return true;
	case VansSerializedValue::Kind::String: return ReadString(reader, value.stringValue, limits, error);
	case VansSerializedValue::Kind::Array:
	{
		std::uint32_t count = 0;
		if (!reader.Pod(count) || count > limits.maximumContainerItems)
			{ error = "Payload array is malformed"; return false; }
		value.arrayItems.reserve(count);
		for (std::uint32_t index = 0; index < count; ++index)
		{
			VansSerializedValue item;
			if (!ReadValue(reader, item, limits, depth + 1, error)) return false;
			value.arrayItems.push_back(std::move(item));
		}
		return true;
	}
	case VansSerializedValue::Kind::Object:
	{
		std::uint32_t count = 0;
		if (!reader.Pod(count) || count > limits.maximumContainerItems)
			{ error = "Payload object is malformed"; return false; }
		std::unordered_set<std::string> names;
		value.objectFields.reserve(count);
		for (std::uint32_t index = 0; index < count; ++index)
		{
			std::string name;
			VansSerializedValue item;
			if (!ReadString(reader, name, limits, error) || !names.insert(name).second ||
				!ReadValue(reader, item, limits, depth + 1, error))
			{
				if (error.empty()) error = "Payload object contains duplicate fields";
				return false;
			}
			value.objectFields.emplace_back(std::move(name), std::move(item));
		}
		return true;
	}
	}
	return false;
}

VansActionNetworkResult Failure(VansActionNetworkError error, std::string message)
{
	return { error, std::move(message) };
}

VansSerializedValue HandleValue(VansEntityHandle handle)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(handle.index) },
		{ "generation", VansSerializedValue::Int(handle.generation) }
	});
}

bool ReadHandle(const VansSerializedValue* value, VansEntityHandle& handle)
{
	if (!value || value->kind != VansSerializedValue::Kind::Object) return false;
	const auto* index = FindObjectField(*value, "index");
	const auto* generation = FindObjectField(*value, "generation");
	if (!index || !generation || index->kind != VansSerializedValue::Kind::Int ||
		generation->kind != VansSerializedValue::Kind::Int || index->intValue < 0 ||
		generation->intValue < 0 || index->intValue > UINT32_MAX || generation->intValue > UINT32_MAX)
		return false;
	handle = { static_cast<std::uint32_t>(index->intValue),
		static_cast<std::uint32_t>(generation->intValue) };
	return true;
}

bool ReadUnsigned(const VansSerializedValue* value, std::uint64_t& result)
{
	if (!value || value->kind != VansSerializedValue::Kind::String || value->stringValue.empty())
		return false;
	const char* begin = value->stringValue.data();
	const char* end = begin + value->stringValue.size();
	const auto parsed = std::from_chars(begin, end, result);
	return parsed.ec == std::errc{} && parsed.ptr == end;
}

}

std::uint64_t VansActionNetworkCodec::PayloadHash(const std::vector<std::uint8_t>& bytes)
{
	std::uint64_t hash = 1469598103934665603ull;
	for (std::uint8_t value : bytes) { hash ^= value; hash *= 1099511628211ull; }
	return hash;
}

VansActionNetworkResult VansActionNetworkCodec::Encode(
	const VansActionNetworkPacket& packet,
	std::vector<std::uint8_t>& bytes,
	const VansActionNetworkLimits& limits)
{
	Writer payload;
	std::string error;
	if (!WriteValue(payload, packet.payload, limits, 0, error))
		return Failure(VansActionNetworkError::BudgetExceeded, std::move(error));
	if (payload.bytes.size() > limits.maximumPayloadBytes)
		return Failure(VansActionNetworkError::BudgetExceeded, "Payload byte budget exceeded");
	Writer writer;
	writer.Pod(VansGameplayActionNetworkMagic);
	writer.Pod(packet.header.protocolVersion);
	writer.Pod(static_cast<std::uint8_t>(packet.header.type));
	writer.Pod(packet.header.flags);
	writer.Pod(packet.header.connection);
	writer.Pod(packet.header.sequence);
	writer.Pod(packet.header.acknowledgement);
	writer.Pod(packet.header.dictionaryVersion);
	writer.Pod(packet.header.contentManifestHash);
	writer.Pod(static_cast<std::uint32_t>(payload.bytes.size()));
	writer.Pod(PayloadHash(payload.bytes));
	writer.Raw(payload.bytes.data(), payload.bytes.size());
	if (writer.bytes.size() > limits.maximumPacketBytes)
		return Failure(VansActionNetworkError::BudgetExceeded, "Packet byte budget exceeded");
	bytes = std::move(writer.bytes);
	return {};
}

VansActionNetworkResult VansActionNetworkCodec::Decode(
	const std::vector<std::uint8_t>& bytes,
	VansActionNetworkPacket& packet,
	const VansActionNetworkLimits& limits)
{
	if (bytes.size() > limits.maximumPacketBytes)
		return Failure(VansActionNetworkError::BudgetExceeded, "Packet byte budget exceeded");
	Reader reader(bytes.data(), bytes.size());
	std::uint32_t magic = 0;
	std::uint8_t type = 0;
	std::uint32_t payloadSize = 0;
	std::uint64_t payloadHash = 0;
	if (!reader.Pod(magic) || !reader.Pod(packet.header.protocolVersion) || !reader.Pod(type) ||
		!reader.Pod(packet.header.flags) || !reader.Pod(packet.header.connection) ||
		!reader.Pod(packet.header.sequence) || !reader.Pod(packet.header.acknowledgement) ||
		!reader.Pod(packet.header.dictionaryVersion) || !reader.Pod(packet.header.contentManifestHash) ||
		!reader.Pod(payloadSize) || !reader.Pod(payloadHash))
		return Failure(VansActionNetworkError::Malformed, "Packet header is truncated");
	if (magic != VansGameplayActionNetworkMagic ||
		type > static_cast<std::uint8_t>(VansActionNetworkMessageType::ActionResult))
		return Failure(VansActionNetworkError::Malformed, "Packet header identity is invalid");
	if (payloadSize > limits.maximumPayloadBytes || payloadSize != reader.remaining)
		return Failure(VansActionNetworkError::Malformed, "Packet payload size is invalid");
	std::vector<std::uint8_t> payload(reader.data, reader.data + reader.remaining);
	if (PayloadHash(payload) != payloadHash)
		return Failure(VansActionNetworkError::HashMismatch, "Packet payload hash mismatch");
	packet.header.type = static_cast<VansActionNetworkMessageType>(type);
	Reader payloadReader(payload.data(), payload.size());
	std::string error;
	if (!ReadValue(payloadReader, packet.payload, limits, 0, error) || payloadReader.remaining != 0)
		return Failure(VansActionNetworkError::Malformed,
			error.empty() ? "Packet payload has trailing bytes" : std::move(error));
	return {};
}

VansActionNetworkResult VansActionNetworkGate::Accept(
	const VansActionNetworkPacket& packet,
	double nowSeconds)
{
	if (packet.header.protocolVersion != m_Policy.protocolVersion)
		return Failure(VansActionNetworkError::ProtocolMismatch, "Gameplay Action protocol version mismatch");
	if (m_Policy.requireDictionaryMatch &&
		packet.header.dictionaryVersion != m_Policy.dictionaryVersion)
		return Failure(VansActionNetworkError::DictionaryMismatch, "Gameplay Tag dictionary version mismatch");
	if (m_Policy.requireContentMatch &&
		packet.header.contentManifestHash != m_Policy.contentManifestHash)
		return Failure(VansActionNetworkError::ContentMismatch, "Gameplay content manifest mismatch");
	if (!std::isfinite(nowSeconds) || packet.header.sequence == 0)
		return Failure(VansActionNetworkError::Malformed, "Packet time or sequence is invalid");
	PeerState& peer = m_Peers[packet.header.connection];
	if (!peer.initialized)
	{
		peer.initialized = true;
		peer.tokens = m_Policy.burstPackets;
		peer.lastTime = nowSeconds;
	}
	const double elapsed = (std::max)(0.0, nowSeconds - peer.lastTime);
	peer.tokens = (std::min)(m_Policy.burstPackets,
		peer.tokens + elapsed * m_Policy.maximumPacketsPerSecond);
	peer.lastTime = nowSeconds;
	if (peer.tokens < 1.0)
		return Failure(VansActionNetworkError::RateLimited, "Gameplay packet rate limit exceeded");
	peer.tokens -= 1.0;
	const std::uint32_t sequence = packet.header.sequence;
	if (peer.highestSequence == 0)
	{
		peer.highestSequence = sequence;
		peer.replayWindow = 1;
		return {};
	}
	if (sequence > peer.highestSequence)
	{
		const std::uint32_t shift = sequence - peer.highestSequence;
		peer.replayWindow = shift >= 64 ? 1ull : (peer.replayWindow << shift) | 1ull;
		peer.highestSequence = sequence;
		return {};
	}
	const std::uint32_t age = peer.highestSequence - sequence;
	if (age >= 64) return Failure(VansActionNetworkError::TooOld, "Gameplay packet is outside the replay window");
	const std::uint64_t bit = 1ull << age;
	if ((peer.replayWindow & bit) != 0)
		return Failure(VansActionNetworkError::Duplicate, "Duplicate gameplay packet rejected");
	peer.replayWindow |= bit;
	return {};
}

void VansActionNetworkGate::Reset(std::uint32_t connection)
{
	m_Peers.erase(connection);
}

bool VansActionLoopbackTransport::Send(
	std::uint32_t source,
	std::uint32_t destination,
	std::vector<std::uint8_t> packet,
	std::string& error)
{
	if (m_Disconnected[source] || m_Disconnected[destination])
		{ error = "Loopback endpoint is disconnected"; return false; }
	if (m_InFlight.size() >= m_Config.maximumQueuedPackets)
		{ error = "Loopback queue budget exceeded"; return false; }
	++m_SendCount;
	if (m_Config.dropEvery != 0 && m_SendCount % m_Config.dropEvery == 0) return true;
	Envelope envelope{ source, destination, m_Tick + m_Config.latencyTicks,
		m_NextOrder++, std::move(packet) };
	if (m_Config.reorderEvery != 0 && m_SendCount % m_Config.reorderEvery == 0 &&
		envelope.deliveryTick > 0) --envelope.deliveryTick;
	m_InFlight.push_back(envelope);
	if (m_Config.duplicateEvery != 0 && m_SendCount % m_Config.duplicateEvery == 0)
	{
		if (m_InFlight.size() >= m_Config.maximumQueuedPackets)
			{ error = "Loopback queue budget exceeded by duplicate"; return false; }
		Envelope duplicate = envelope;
		duplicate.order = m_NextOrder++;
		m_InFlight.push_back(std::move(duplicate));
	}
	return true;
}

void VansActionLoopbackTransport::Advance(std::uint32_t ticks)
{
	m_Tick += ticks;
	std::stable_sort(m_InFlight.begin(), m_InFlight.end(), [](const Envelope& left, const Envelope& right)
	{
		if (left.deliveryTick != right.deliveryTick) return left.deliveryTick < right.deliveryTick;
		return left.order < right.order;
	});
	auto end = std::stable_partition(m_InFlight.begin(), m_InFlight.end(),
		[this](const Envelope& envelope) { return envelope.deliveryTick > m_Tick; });
	for (auto iterator = end; iterator != m_InFlight.end(); ++iterator)
		if (!m_Disconnected[iterator->destination])
			m_Received[iterator->destination].push_back(std::move(*iterator));
	m_InFlight.erase(end, m_InFlight.end());
}

bool VansActionLoopbackTransport::Receive(
	std::uint32_t destination,
	std::vector<std::uint8_t>& packet,
	std::uint32_t* source)
{
	auto found = m_Received.find(destination);
	if (found == m_Received.end() || found->second.empty()) return false;
	Envelope envelope = std::move(found->second.front());
	found->second.pop_front();
	if (source) *source = envelope.source;
	packet = std::move(envelope.packet);
	return true;
}

void VansActionLoopbackTransport::Disconnect(std::uint32_t connection)
{
	m_Disconnected[connection] = true;
	m_InFlight.erase(std::remove_if(m_InFlight.begin(), m_InFlight.end(),
		[connection](const Envelope& envelope)
		{ return envelope.source == connection || envelope.destination == connection; }), m_InFlight.end());
	m_Received.erase(connection);
}

void VansActionLoopbackTransport::Reconnect(std::uint32_t connection)
{
	m_Disconnected.erase(connection);
}

bool VansGameplayPredictionManager::Begin(
	VansPredictionKey key,
	std::shared_ptr<VansActionHost> host,
	VansActionHandle action,
	std::uint64_t contentHash,
	std::string& error)
{
	if (!key.IsValid() || !host || !action || contentHash == 0)
		{ error = "Prediction record identity is invalid"; return false; }
	if (m_Records.find(key) != m_Records.end())
		{ error = "Prediction key is already registered"; return false; }
	Record record;
	record.view = { key, host->Owner(), action, contentHash, VansPredictionRecordState::Pending, {} };
	record.host = host;
	m_Records.emplace(key, std::move(record));
	return true;
}

bool VansGameplayPredictionManager::Confirm(VansPredictionKey key, std::string& error)
{
	auto found = m_Records.find(key);
	if (found == m_Records.end() || found->second.view.state != VansPredictionRecordState::Pending)
		{ error = "Prediction key is not pending"; return false; }
	found->second.view.state = VansPredictionRecordState::Confirmed;
	return true;
}

bool VansGameplayPredictionManager::Reject(
	VansPredictionKey key,
	std::string reason,
	std::string& error)
{
	auto found = m_Records.find(key);
	if (found == m_Records.end() || found->second.view.state != VansPredictionRecordState::Pending)
		{ error = "Prediction key is not pending"; return false; }
	auto host = found->second.host.lock();
	std::vector<std::string> rollbackErrors;
	if (!host || !host->RollbackPrediction(found->second.view.action, rollbackErrors))
	{
		error = rollbackErrors.empty() ? "Predicted resources could not be rolled back"
			: rollbackErrors.front();
		return false;
	}
	found->second.view.state = VansPredictionRecordState::Rejected;
	found->second.view.reason = std::move(reason);
	return true;
}

bool VansGameplayPredictionManager::Correct(
	VansPredictionKey key,
	std::function<bool(std::string&)> applyCorrection,
	std::string& error)
{
	auto found = m_Records.find(key);
	if (found == m_Records.end() || found->second.view.state != VansPredictionRecordState::Pending ||
		!applyCorrection)
		{ error = "Prediction correction request is invalid"; return false; }
	auto host = found->second.host.lock();
	std::vector<std::string> rollbackErrors;
	if (!host || !host->RollbackPrediction(found->second.view.action, rollbackErrors))
		{ error = rollbackErrors.empty() ? "Prediction rollback failed" : rollbackErrors.front(); return false; }
	if (!applyCorrection(error)) return false;
	std::vector<std::string> replayErrors;
	if (!host->ReplayPrediction(found->second.view.action, replayErrors))
		{ error = replayErrors.empty() ? "Prediction replay failed" : replayErrors.front(); return false; }
	found->second.view.state = VansPredictionRecordState::Corrected;
	return true;
}

std::optional<VansPredictionRecordView> VansGameplayPredictionManager::Query(VansPredictionKey key) const
{
	const auto found = m_Records.find(key);
	return found == m_Records.end() ? std::nullopt
		: std::optional<VansPredictionRecordView>(found->second.view);
}

std::vector<VansPredictionRecordView> VansGameplayPredictionManager::Records() const
{
	std::vector<VansPredictionRecordView> result;
	result.reserve(m_Records.size());
	for (const auto& [key, record] : m_Records) { (void)key; result.push_back(record.view); }
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		if (left.key.connection != right.key.connection)
			return left.key.connection < right.key.connection;
		return left.key.sequence < right.key.sequence;
	});
	return result;
}

void VansGameplayPredictionManager::ForgetResolved()
{
	for (auto iterator = m_Records.begin(); iterator != m_Records.end();)
		if (iterator->second.view.state != VansPredictionRecordState::Pending)
			iterator = m_Records.erase(iterator);
		else ++iterator;
}

VansSerializedValue VansEncodeActionActivationMessage(
	const VansActionActivationNetworkMessage& message)
{
	VansSerializedValue result = VansSerializedValue::Object({
		{ "action", VansSerializedValue::String(std::to_string(message.action.value)) },
		{ "owner", HandleValue(message.context.owner) },
		{ "instigator", HandleValue(message.context.instigator) },
		{ "source", HandleValue(message.context.source) },
		{ "target", HandleValue(message.context.primaryTarget) },
		{ "predictionConnection", VansSerializedValue::Int(message.context.predictionKey.connection) },
		{ "predictionSequence", VansSerializedValue::Int(message.context.predictionKey.sequence) },
		{ "randomSeed", VansSerializedValue::String(std::to_string(message.context.randomSeed)) },
		{ "contentHash", VansSerializedValue::String(std::to_string(message.definitionContentHash)) },
		{ "payload", message.context.payload }
	});
	if (message.hasTargetData)
		SetSerializedObjectField(result, "targetData", VansEncodeTargetData(message.targetData));
	return result;
}

bool VansDecodeActionActivationMessage(
	const VansSerializedValue& value,
	VansActionActivationNetworkMessage& message,
	std::string& error,
	const VansTargetDataNetworkPolicy& targetPolicy)
{
	message = {};
	if (value.kind != VansSerializedValue::Kind::Object)
		{ error = "Activation message must be an object"; return false; }
	const auto* action = FindObjectField(value, "action");
	const auto* predictionConnection = FindObjectField(value, "predictionConnection");
	const auto* predictionSequence = FindObjectField(value, "predictionSequence");
	const auto* randomSeed = FindObjectField(value, "randomSeed");
	const auto* contentHash = FindObjectField(value, "contentHash");
	const auto* payload = FindObjectField(value, "payload");
	std::uint64_t actionValue = 0;
	std::uint64_t randomSeedValue = 0;
	std::uint64_t contentHashValue = 0;
	if (!action || !predictionConnection || !predictionSequence || !randomSeed || !contentHash ||
		!payload || !ReadUnsigned(action, actionValue) ||
		!ReadUnsigned(randomSeed, randomSeedValue) || !ReadUnsigned(contentHash, contentHashValue) ||
		predictionConnection->kind != VansSerializedValue::Kind::Int ||
		predictionSequence->kind != VansSerializedValue::Kind::Int ||
		actionValue == 0 || contentHashValue == 0 ||
		predictionConnection->intValue < 0 || predictionSequence->intValue < 0 ||
		predictionConnection->intValue > UINT32_MAX ||
		predictionSequence->intValue > UINT32_MAX)
		{ error = "Activation message fields are malformed"; return false; }
	if (!ReadHandle(FindObjectField(value, "owner"), message.context.owner) ||
		!ReadHandle(FindObjectField(value, "instigator"), message.context.instigator) ||
		!ReadHandle(FindObjectField(value, "source"), message.context.source) ||
		!ReadHandle(FindObjectField(value, "target"), message.context.primaryTarget) ||
		!message.context.owner.IsValid())
		{ error = "Activation message handles are malformed"; return false; }
	message.action = VansActionId{ actionValue };
	message.context.predictionKey = {
		static_cast<std::uint32_t>(predictionConnection->intValue),
		static_cast<std::uint32_t>(predictionSequence->intValue) };
	message.context.randomSeed = randomSeedValue;
	message.definitionContentHash = contentHashValue;
	message.context.payload = *payload;
	if (const VansSerializedValue* targetData = FindObjectField(value, "targetData"))
	{
		if (!VansDecodeTargetData(*targetData, message.targetData, error, targetPolicy))
			return false;
		message.hasTargetData = true;
	}
	return true;
}
}
