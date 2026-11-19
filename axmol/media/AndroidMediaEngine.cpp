/****************************************************************************
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/

#if defined(__ANDROID__)
#    include "axmol/media/AndroidMediaEngine.h"
#    include "axmol/platform/android/jni/JniHelper.h"
#    include <cstdint>
#    include <limits>

extern "C" {
JNIEXPORT void JNICALL Java_dev_axmol_lib_AxmolMediaPlayer_nativeFireEvent(JNIEnv* env, jclass, jlong pME, int arg1)
{
    auto mediaEngine = (ax::AndroidMediaEngine*)((uintptr_t)pME);
    if (!mediaEngine)
        return;

    mediaEngine->_fireMediaEvent((ax::MEMediaEventType)arg1);
}

JNIEXPORT void JNICALL Java_dev_axmol_lib_AxmolMediaPlayer_nativeSetDuration(JNIEnv* env,
                                                                             jclass,
                                                                             jlong pME,
                                                                             double duration)
{
    auto mediaEngine = (ax::AndroidMediaEngine*)((uintptr_t)pME);
    if (!mediaEngine)
        return;

    mediaEngine->_setDuration(duration);
}

JNIEXPORT void JNICALL Java_dev_axmol_lib_AxmolMediaPlayer_nativeProcessVideoFrame(JNIEnv* env,
                                                                                   jclass,
                                                                                   jlong pME,
                                                                                   jobject sampleBuffer,
                                                                                   int sampleLen,
                                                                                   jlong presentationTimeUs,
                                                                                   int outputX,
                                                                                   int outputY,
                                                                                   int videoX,
                                                                                   int videoY,
                                                                                   int cbcrOffset,
                                                                                   int rotation,
                                                                                   int videoPF)
{
    auto mediaEngine = (ax::AndroidMediaEngine*)((uintptr_t)pME);
    if (!mediaEngine || !sampleBuffer || sampleLen <= 0 || env->ExceptionCheck())
        return;

    const auto capacity = env->GetDirectBufferCapacity(sampleBuffer);
    if (capacity < 0 || static_cast<jlong>(sampleLen) > capacity || env->ExceptionCheck())
        return;
    // Java submits a normalized direct buffer at position zero with this exact length.
    const auto sampleData = static_cast<const uint8_t*>(env->GetDirectBufferAddress(sampleBuffer));
    if (sampleData)
        mediaEngine->_processVideoFrame(sampleData, static_cast<size_t>(sampleLen), presentationTimeUs,
                                         outputX, outputY, videoX, videoY, cbcrOffset, rotation, videoPF);
}
}

namespace ax
{

static const char* className = "dev.axmol.lib.AxmolMediaPlayer";

AndroidMediaEngine::AndroidMediaEngine()
{
    // Create Java MediaPlayer and take ownership of the global reference.
    // Note: JniHelper::callStaticObjectMethod returns a jobject that has been
    // promoted to a GlobalRef (via NewGlobalRef). Do NOT call NewGlobalRef again;
    // ensure DeleteGlobalRef(_mediaPlayer) is called during native teardown.
    _mediaPlayer = JniHelper::callStaticObjectMethod(className, "createMediaPlayer", (jlong)(uintptr_t)this);
}
AndroidMediaEngine::~AndroidMediaEngine()
{
    if (_mediaPlayer)
    {
        // clear callback
        JniHelper::callVoidMethod(className, "dispose", _mediaPlayer);
        JniHelper::getEnv()->DeleteGlobalRef(_mediaPlayer);
        _mediaPlayer = nullptr;
    }
}

void AndroidMediaEngine::setAutoPlay(bool bAutoPlay)
{
    if (_mediaPlayer)
        JniHelper::callVoidMethod(className, "setAutoPlay", _mediaPlayer, bAutoPlay);
}
bool AndroidMediaEngine::open(std::string_view sourceUri)
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "open", _mediaPlayer, sourceUri);
}
bool AndroidMediaEngine::close()
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "close", _mediaPlayer);
}
bool AndroidMediaEngine::setLoop(bool bLooping)
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "setLoop", _mediaPlayer, bLooping);
}
bool AndroidMediaEngine::setRate(double fRate)
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "setRate", _mediaPlayer, fRate);
}
bool AndroidMediaEngine::setVolume(double volume)
{
    if (!_mediaPlayer || !JniHelper::callBooleanMethod(className, "setVolume", _mediaPlayer, volume))
        return false;

    _volume = volume;
    return true;
}
double AndroidMediaEngine::getVolume() const
{
    return _volume;
}
bool AndroidMediaEngine::setCurrentTime(double fSeekTimeInSec)
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "setCurrentTime", _mediaPlayer, fSeekTimeInSec);
}
bool AndroidMediaEngine::play()
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "play", _mediaPlayer);
}
bool AndroidMediaEngine::pause()
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "pause", _mediaPlayer);
}
bool AndroidMediaEngine::stop()
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "stop", _mediaPlayer);
}
bool AndroidMediaEngine::isPlaybackEnded() const
{
    return _mediaPlayer && JniHelper::callBooleanMethod(className, "isPlaybackEnded", _mediaPlayer);
}
MEMediaState AndroidMediaEngine::getState() const
{
    return _mediaPlayer ? (MEMediaState)JniHelper::callIntMethod(className, "getState", _mediaPlayer)
                        : MEMediaState::Closed;
}

bool AndroidMediaEngine::isFrameValid(size_t dataLen, const FrameMetadata& metadata)
{
    const auto& dim = metadata.pixelDesc._dim;
    const auto& video = metadata.videoDim;
    const auto format = metadata.pixelDesc._PF;
    if ((format != MEVideoPixelFormat::NV12 && format != MEVideoPixelFormat::I420) ||
        dim.x <= 0 || dim.y <= 0 || video.x <= 0 || video.y <= 0 ||
        video.x > dim.x || video.y > dim.y ||
        ((dim.x | dim.y | video.x | video.y) & 1) != 0)
        return false;
    const int rotation = metadata.pixelDesc._rotation;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        return false;

    const uint64_t yBytes = static_cast<uint64_t>(dim.x) * static_cast<uint64_t>(dim.y);
    const uint64_t chromaBytes = (static_cast<uint64_t>(dim.x) / 2) *
                                 (static_cast<uint64_t>(dim.y) / 2) * 2;
    // Check by subtraction: neither offset addition nor pointer arithmetic may wrap.
    return yBytes <= dataLen && metadata.chromaOffset >= yBytes &&
           metadata.chromaOffset <= dataLen && chromaBytes <= dataLen - metadata.chromaOffset;
}

bool AndroidMediaEngine::transferVideoFrame()
{
    if (!_mediaPlayer)
        return false;

    FrameMetadata metadata;
    {
        std::unique_lock<std::mutex> lck(_frameBuffer1Mtx);
        if (_frameBuffer1.empty())
            return false;
        _frameBuffer2.swap(_frameBuffer1);
        metadata = _pendingFrameMetadata;
    }
    // The producer can replace the next pending frame while this local snapshot
    // and the separate read buffer are uploaded. Never hold its lock in callbacks.
    auto& buffer = _frameBuffer2;
    if (!isFrameValid(buffer.size(), metadata) || !_onVideoFrame)
    {
        buffer.clear();
        return false;
    }
    ax::MEVideoFrame frame{buffer.data(), buffer.data() + metadata.chromaOffset, buffer.size(),
                           metadata.pixelDesc, metadata.videoDim};
    _onVideoFrame(frame);
    buffer.clear();
    return true;
}

void AndroidMediaEngine::_processVideoFrame(const uint8_t* sampleData, size_t sampleLen,
                                             int64_t presentationTimeUs,
                                             int outputX, int outputY, int videoX, int videoY,
                                             int cbcrOffset, int rotation, int videoPF)
{
    if (!sampleData || sampleLen > static_cast<size_t>(std::numeric_limits<int>::max()) || cbcrOffset < 0)
        return;
    FrameMetadata metadata;
    metadata.pixelDesc = MEVideoPixelDesc{static_cast<MEVideoPixelFormat>(videoPF),
                                         MEIntPoint{outputX, outputY}};
    metadata.pixelDesc._rotation = rotation;
    metadata.videoDim = MEIntPoint{videoX, videoY};
    metadata.chromaOffset = static_cast<size_t>(cbcrOffset);
    metadata.presentationTimeUs = presentationTimeUs;
    if (!isFrameValid(sampleLen, metadata))
        return;

    std::unique_lock<std::mutex> lck(_frameBuffer1Mtx);
    _frameBuffer1.assign(sampleData, sampleData + sampleLen);
    _pendingFrameMetadata = metadata;
    // Retain the original latest-produced-frame timing semantics, without a data race.
    _currentTime.store(presentationTimeUs / 1000000.0, std::memory_order_relaxed);
}

}  // namespace ax

#endif
