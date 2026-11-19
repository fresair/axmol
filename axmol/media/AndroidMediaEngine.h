/****************************************************************************
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/
#pragma once

#if defined(__ANDROID__)
#    include "axmol/media/MediaEngine.h"

#    include <jni.h>
#    include <atomic>

namespace ax
{

class AndroidMediaEngine : public MediaEngine
{
public:
    AndroidMediaEngine();
    ~AndroidMediaEngine();
    void setCallbacks(std::function<void(MEMediaEventType)> onMediaEvent,
                      std::function<void(const MEVideoFrame&)> onVideoFrame) override
    {
        _onMediaEvent = std::move(onMediaEvent);
        _onVideoFrame = std::move(onVideoFrame);
    }
    void setAutoPlay(bool bAutoPlay) override;
    bool open(std::string_view sourceUri) override;
    bool close() override;
    bool setLoop(bool bLooping) override;
    bool setRate(double fRate) override;
    bool setVolume(double volume) override;
    double getVolume() const override;
    bool setCurrentTime(double fSeekTimeInSec) override;
    double getCurrentTime() override { return _currentTime.load(std::memory_order_relaxed); }
    double getDuration() override { return _duration.load(std::memory_order_relaxed); }
    bool play() override;
    bool pause() override;
    bool stop() override;
    bool isPlaybackEnded() const override;
    MEMediaState getState() const override;
    bool transferVideoFrame() override;

    // internal stubs
    void _fireMediaEvent(MEMediaEventType event)
    {
        if (_onMediaEvent)
            _onMediaEvent(event);
    }
    void _setDuration(double duration) { _duration.store(duration, std::memory_order_relaxed); }

    void _processVideoFrame(const uint8_t* sampleData, size_t sampleLen, int64_t presentationTimeUs,
                            int outputX, int outputY, int videoX, int videoY,
                            int cbcrOffset, int rotation, int videoPF);

private:
    jobject _mediaPlayer{nullptr};  // java object strong-refs
    std::function<void(MEMediaEventType)> _onMediaEvent;
    std::function<void(const MEVideoFrame&)> _onVideoFrame;

    struct FrameMetadata
    {
        MEVideoPixelDesc pixelDesc;
        MEIntPoint videoDim;
        size_t chromaOffset{0};
        int64_t presentationTimeUs{0};
    };
    static bool isFrameValid(size_t dataLen, const FrameMetadata& metadata);
    FrameMetadata _pendingFrameMetadata;  // protected together with _frameBuffer1

    tlx::byte_buffer _frameBuffer1;  // for write
    tlx::byte_buffer _frameBuffer2;  // for read
    mutable std::mutex _frameBuffer1Mtx;

    std::atomic<double> _currentTime{0.0};  // latest accepted producer PTS, seconds
    std::atomic<double> _duration{0.0};
    double _volume{1.0};
};

struct AndroidMediaEngineFactory : public MediaEngineFactory
{
    MediaEngine* createMediaEngine() override { return new AndroidMediaEngine(); }
    void destroyMediaEngine(MediaEngine* me) override { delete static_cast<AndroidMediaEngine*>(me); }
};

}  // namespace ax

#endif
