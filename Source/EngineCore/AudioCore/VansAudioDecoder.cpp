#include "VansAudioDecoder.h"
#include "../MediaCore/VansMediaDecodeSession.h"
#include "../Util/VansLog.h"

namespace VansEngine
{

VansAudioDecoder::VansAudioDecoder() = default;

VansAudioDecoder::~VansAudioDecoder()
{
    Close();
}

bool VansAudioDecoder::Open(const std::string& filePath,
                             int targetChannels,
                             int targetSampleRate)
{
    Close();
    if (filePath.empty())
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Open failed: empty filePath");
        return false;
    }

    m_FilePath = filePath;

    auto session = std::make_unique<VansMediaDecodeSession>();
    std::string error;
    if (!session->OpenAudio(filePath, targetChannels, targetSampleRate, error))
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Open failed: " << filePath << " (" << error << ")");
        m_FilePath.clear();
        return false;
    }

    m_TargetChannels = session->GetChannels();
    m_TargetSampleRate = session->GetSampleRate();
    m_Duration = session->GetDuration();
    m_Session = std::move(session);

    VANS_LOG("[VansAudioDecoder] Opened: " << filePath
             << "  channels=" << m_TargetChannels
             << "  sampleRate=" << m_TargetSampleRate
             << "  duration=" << m_Duration << "s");
    return true;
}

void VansAudioDecoder::Close()
{
    std::lock_guard<std::mutex> decodeLock(m_DecodeMutex);
    m_Session.reset();
    m_Duration = 0.0;
    m_FilePath.clear();
}

AudioPCMChunk VansAudioDecoder::DecodeNextChunk()
{
    std::lock_guard<std::mutex> decodeLock(m_DecodeMutex);

    AudioPCMChunk chunk;
    chunk.channels   = m_TargetChannels;
    chunk.sampleRate = m_TargetSampleRate;

    if (!m_Session)
    {
        chunk.endOfStream = true;
        return chunk;
    }

    std::string error;
    const VansMediaDecodeStatus status =
        m_Session->DecodeAudioChunk(4096, chunk.samples, error);
    if (status == VansMediaDecodeStatus::Failed)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Decode failed: " << m_FilePath << " (" << error << ")");
        chunk.samples.clear();
        chunk.endOfStream = true;
    }
    else
    {
        chunk.endOfStream = status == VansMediaDecodeStatus::EndOfStream;
    }
    return chunk;
}

std::vector<int16_t> VansAudioDecoder::DecodeAll(int& outChannels, int& outSampleRate)
{
    outChannels   = m_TargetChannels;
    outSampleRate = m_TargetSampleRate;

    std::vector<int16_t> allSamples;
    allSamples.reserve(static_cast<size_t>(m_Duration * m_TargetSampleRate * m_TargetChannels) + 4096);

    while (true)
    {
        AudioPCMChunk chunk = DecodeNextChunk();
        if (!chunk.samples.empty())
            allSamples.insert(allSamples.end(), chunk.samples.begin(), chunk.samples.end());

        if (chunk.endOfStream)
            break;
    }

    VANS_LOG("[VansAudioDecoder] DecodeAll: " << allSamples.size() / m_TargetChannels << " samples");
    return allSamples;
}

bool VansAudioDecoder::Reset()
{
    std::lock_guard<std::mutex> decodeLock(m_DecodeMutex);

    if (!m_Session)
        return false;

    std::string error;
    if (!m_Session->Reset(error))
    {
        VANS_LOG_WARN("[VansAudioDecoder] Reset failed: " << m_FilePath << " (" << error << ")");
        return false;
    }
    return true;
}

bool VansAudioDecoder::IsOpen() const
{
    return m_Session && m_Session->IsOpen();
}

} // namespace VansEngine
