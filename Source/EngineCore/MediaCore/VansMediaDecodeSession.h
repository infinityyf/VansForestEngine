#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VansEngine
{
enum class VansMediaDecodeStatus
{
    FrameReady,
    EndOfStream,
    Failed
};

enum class VansMediaEndPolicy
{
    StopAtInputEnd,
    DrainDecoder
};

struct VansMediaDecodeState;

class VansMediaDecodeSession
{
public:
    VansMediaDecodeSession();
    ~VansMediaDecodeSession();

    VansMediaDecodeSession(const VansMediaDecodeSession&) = delete;
    VansMediaDecodeSession& operator=(const VansMediaDecodeSession&) = delete;

    bool OpenAudio(
        const std::string& filePath,
        int targetChannels,
        int targetSampleRate,
        std::string& error);
    bool OpenVideo(const std::string& filePath, std::string& error);
    void Close();

    bool IsOpen() const;
    int GetWidth() const;
    int GetHeight() const;
    int GetChannels() const;
    int GetSampleRate() const;
    double GetDuration() const;

    VansMediaDecodeStatus DecodeAudioChunk(
        int targetSampleFrames,
        std::vector<std::int16_t>& samples,
        std::string& error);
    VansMediaDecodeStatus DecodeVideoFrame(
        std::vector<std::uint8_t>& rgba,
        double& presentationTime,
        VansMediaEndPolicy endPolicy,
        std::string& error);

    bool Seek(double seconds, std::string& error);
    bool Reset(std::string& error);

private:
    std::unique_ptr<VansMediaDecodeState> m_State;
};
}
