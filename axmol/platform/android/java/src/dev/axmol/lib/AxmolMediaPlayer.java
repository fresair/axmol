/****************************************************************************
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/
package dev.axmol.lib;

import android.content.Context;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.Handler;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.VideoSize;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.datasource.DataSource;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.datasource.HttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.Renderer;
import androidx.media3.exoplayer.mediacodec.MediaCodecAdapter;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import androidx.media3.exoplayer.source.MediaSource;
import androidx.media3.exoplayer.source.ProgressiveMediaSource;
import androidx.media3.exoplayer.video.VideoFrameMetadataListener;
import androidx.media3.exoplayer.video.VideoRendererEventListener;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

@UnstableApi @SuppressWarnings("unused")
public class AxmolMediaPlayer extends DefaultRenderersFactory implements Player.Listener, ByteBufferVideoRenderer.VideoFrameProcessor, VideoFrameMetadataListener {
    // The native media events, match with MEMediaEventType
    public static final int EVENT_PLAYING = 0;
    public static final int EVENT_PAUSED = 1;
    public static final int EVENT_STOPPED = 2;
    public static final int EVENT_ERROR = 3;

    // The native video pixel formats, match with MEVideoPixelFormat
    public static final int VIDEO_PF_NV12 = 3;
    public static final int VIDEO_PF_I420 = 4;

    /**
     * Media has been closed and cannot be played again.
     */
    public static final int STATE_CLOSED = 0;

    /**
     * Media is being prepared for playback.
     */
    public static final int STATE_PREPARING = 1;

    /**
     * Media is currently playing.
     */
    public static final int STATE_PLAYING = 2;

    /**
     * Playback has been paused, but can be resumed.
     */
    public static final int STATE_PAUSED = 3;

    /**
     * Playback has been stopped, but can be restarted.
     */
    public static final int STATE_STOPPED = 4;

    /**
     * Unrecoverable error occurred during playback.
     */
    public static final int STATE_ERROR = 5;

    // The desired video pixel format
    /**
     * COLOR_FormatYUV420SemiPlanar (yyyyyyyy uvuv)  (NV12)
     * COLOR_FormatYUV420Planar (yyyyyyyy uu vv)     (YUV420p)
     * COLOR_FormatYUV422SemiPlanar (Y0 U0 Y1 V0)    (YUY2)
     */
    public static final String TAG = "AxmolMediaPlayer";
    private Context mContext;
    private volatile ExoPlayer mPlayer;
    private volatile ByteBufferVideoRenderer mVideoRenderer;
    private final Object mLifecycleLock = new Object();
    private long mRequestGeneration;
    private long mPlaybackIntentVersion;
    private long mAppliedPlaybackIntentVersion = -1;
    private int mPlaybackTarget = STATE_CLOSED;
    private volatile boolean mBackendPlaying;
    private final ArrayDeque<PlaybackCommand> mPlaybackCommands = new ArrayDeque<>();
    private boolean mPlaybackDrainScheduled;
    private boolean mManualStopApplying;
    private boolean mManualStopNotified;
    private String mSourceUri = "";
    private PlayerSession mSession;
    private final AtomicInteger mLifecycleDiagnostics = new AtomicInteger();
    private final AtomicInteger mHttpResponseDiagnostics = new AtomicInteger();
    private volatile MediaFormat mOutputFormat;
    private static final class FrameFormat {
        final Yuv420FrameLayout layout;
        final int rotation;

        FrameFormat(Yuv420FrameLayout layout, int rotation) {
            this.layout = layout;
            this.rotation = rotation;
        }
    }
    private volatile FrameFormat mFrameFormat;
    private ByteBuffer mNormalizedFrame;
    private final AtomicInteger mFrameDiagnostics = new AtomicInteger();
    private final AtomicInteger mLayoutDiagnostics = new AtomicInteger();
    private final AtomicInteger mLayoutRejects = new AtomicInteger();
    private final AtomicInteger mFrameRejects = new AtomicInteger();
    private volatile long mNativeObj = 0; // native object address for send event to C++, weak ref
    private boolean mAutoPlay = false;
    private boolean mLooping = false;
    private volatile boolean mPlayWhenReady = false;
    private volatile boolean mPlaybackEnded = false;
    private volatile double mVolume = 1.0;
    private AtomicInteger mState = new AtomicInteger(STATE_CLOSED);

    // Each installed listener/output belongs to exactly one player and source request.
    // Hold the lifecycle gate while forwarding so close cannot invalidate a frame midway
    // between its metadata/normalization and native delivery.
    private final class PlayerSession implements Player.Listener,
        ByteBufferVideoRenderer.VideoFrameProcessor, VideoFrameMetadataListener {
        final long generation;
        final ExoPlayer player;
        final ByteBufferVideoRenderer renderer;
        boolean released;

        PlayerSession(long generation, ExoPlayer player, ByteBufferVideoRenderer renderer) {
            this.generation = generation;
            this.player = player;
            this.renderer = renderer;
        }
        @Override public void onIsPlayingChanged(boolean playing) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) {
                    // A queued callback argument can precede a newer UI command.
                    mBackendPlaying = player.isPlaying();
                    AxmolMediaPlayer.this.onIsPlayingChanged(mBackendPlaying);
                }
            }
        }
        @Override public void onPlaybackStateChanged(int state) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.onPlaybackStateChanged(state);
            }
        }
        @Override public void onPlayerError(PlaybackException error) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.onPlayerError(error);
            }
        }
        @Override public void onVideoSizeChanged(VideoSize size) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.onVideoSizeChanged(size);
            }
        }
        @Override public void onIsLoadingChanged(boolean loading) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.onIsLoadingChanged(loading);
            }
        }
        @Override public void onVideoFrameAboutToBeRendered(long pts, long release,
            Format format, @Nullable MediaFormat mediaFormat) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.onVideoFrameAboutToBeRendered(
                    pts, release, format, mediaFormat);
            }
        }
        @Override public void processVideoFrame(MediaCodecAdapter codec, int index, long pts) {
            synchronized (mLifecycleLock) {
                if (isCurrent(this)) AxmolMediaPlayer.this.processVideoFrame(codec, index, pts);
            }
        }
    }

    private interface PlayerAction { void run(ExoPlayer player); }

    private static final class PlaybackCommand {
        final PlayerSession session;
        final long version;
        final int target;
        final PlayerAction action;

        PlaybackCommand(PlayerSession session, long version, int target) {
            this.session = session;
            this.version = version;
            this.target = target;
            this.action = null;
        }

        PlaybackCommand(PlayerSession session, PlayerAction action) {
            this.session = session;
            this.version = 0;
            this.target = 0;
            this.action = action;
        }
    }

    // Called only while holding mLifecycleLock.
    private boolean isCurrent(PlayerSession session) {
        return session != null && session == mSession && session.generation == mRequestGeneration
            && session.player == mPlayer && mNativeObj != 0;
    }

    private boolean postPlayerAction(PlayerAction action) {
        synchronized (mLifecycleLock) {
            PlayerSession session = mSession;
            if (!isCurrent(session)) return false;
            // Share the session FIFO with playback commands: a later PLAY cannot
            // jump over an accepted seek/rate/loop action between STOP and PLAY.
            mPlaybackCommands.addLast(new PlaybackCommand(session, action));
            schedulePlaybackDrainLocked(session);
            return true;
        }
    }

    // Intent is accepted synchronously, including the interval before the UI has
    // constructed a player. Once a session exists, accepted actions remain ordered:
    // e.g. stop/play must execute the stop and rewind even if PLAY is the final target.
    private boolean requestPlaybackTarget(int target) {
        synchronized (mLifecycleLock) {
            final PlayerSession session = mSession;
            boolean pendingOpen = session == null && mState.get() == STATE_PREPARING;
            if (mNativeObj == 0 || (!isCurrent(session) && !pendingOpen)) return false;
            mPlaybackTarget = target;
            final long version = ++mPlaybackIntentVersion;
            mPlayWhenReady = target == STATE_PLAYING;
            if (target == STATE_PLAYING || target == STATE_STOPPED) mPlaybackEnded = false;
            if (session == null) return true; // applyOpen consumes the latest target.
            mPlaybackCommands.addLast(new PlaybackCommand(session, version, target));
            schedulePlaybackDrainLocked(session);
            return true;
        }
    }

    private void schedulePlaybackDrainLocked(PlayerSession session) {
        if (!mPlaybackDrainScheduled) {
            mPlaybackDrainScheduled = true;
            AxmolEngine.runOnUiThread(() -> drainPlaybackCommands(session));
        }
    }

    private void drainPlaybackCommands(PlayerSession session) {
        synchronized (mLifecycleLock) {
            if (!isCurrent(session)) return;
            try {
                while (isCurrent(session) && !mPlaybackCommands.isEmpty()) {
                    PlaybackCommand command = mPlaybackCommands.removeFirst();
                    if (command.session == session) {
                        if (command.action != null) command.action.run(session.player);
                        else applyPlaybackTargetLocked(session, command.version, command.target);
                    }
                }
            } finally {
                // A reentrant source change may already own a new drain/queue.
                if (isCurrent(session)) mPlaybackDrainScheduled = false;
            }
        }
    }

    private void applyPlaybackTargetLocked(PlayerSession session, long version, int target) {
        if (!isCurrent(session)) return;
        ExoPlayer player = session.player;
        mAppliedPlaybackIntentVersion = version;
        // Do not replace the final desired state with an older accepted action.
        switch (target) {
            case STATE_PLAYING:
                mPlaybackEnded = false;
                int playbackState = player.getPlaybackState();
                boolean restartingFromStopped = mState.compareAndSet(STATE_STOPPED, STATE_PREPARING);
                if (playbackState == Player.STATE_IDLE) player.prepare();
                if (restartingFromStopped || playbackState == Player.STATE_IDLE
                    || playbackState == Player.STATE_ENDED) player.seekTo(0);
                player.play();
                break;
            case STATE_PAUSED:
                player.pause();
                break;
            case STATE_STOPPED:
                // Clear natural completion before the real IDLE/STOP notification.
                mPlaybackEnded = false;
                mManualStopApplying = true;
                mManualStopNotified = false;
                try {
                    // Media3 stop() preserves playWhenReady. Disable actual playback
                    // explicitly so loop STOP is not classified using a pending PLAY.
                    player.setPlayWhenReady(false);
                    player.stop();
                    publishManualStopIfIdleLocked(session);
                } finally {
                    mManualStopApplying = false;
                }
                break;
            default:
                break;
        }
    }

    private void publishManualStopIfIdleLocked(PlayerSession session) {
        if (!mManualStopApplying || mManualStopNotified || !isCurrent(session)
            || session.player.getPlaybackState() != Player.STATE_IDLE) return;
        mManualStopNotified = true;
        mBackendPlaying = false;
        mPlaybackEnded = false;
        mState.set(STATE_STOPPED);
        nativeEvent(EVENT_STOPPED);
    }

    private void lifecycleDiagnostic(String operation, String phase, long generation,
        String sourceUri, boolean accepted) {
        if (mLifecycleDiagnostics.getAndIncrement() >= 24) return;
        Log.d(TAG, "MediaSourceLifecycle operation=" + operation + " phase=" + phase
            + " generation=" + generation + " accepted=" + accepted + " uri=" + sanitizedUri(sourceUri));
    }

    private static String boundedDiagnosticText(String value) {
        if (value == null) return "<absent>";
        String text = value.replace('\n', '_').replace('\r', '_');
        return text.length() > 256 ? text.substring(0, 256) : text;
    }

    private static String sanitizedUri(String sourceUri) {
        // Keep the original fixture URL visible without logging credentials/query secrets.
        String safeUri = "<unavailable>";
        try {
            if (sourceUri != null) {
                Uri uri = Uri.parse(sourceUri);
                String authority = uri.getEncodedAuthority();
                if (authority != null && authority.contains("@"))
                    authority = authority.substring(authority.lastIndexOf('@') + 1);
                safeUri = uri.buildUpon().encodedAuthority(authority).clearQuery().fragment(null)
                    .build().toString().replace('\n', '_').replace('\r', '_');
            }
        } catch (RuntimeException ignored) { /* Diagnostics never decide whether open is accepted. */ }
        return boundedDiagnosticText(safeUri);
    }

    private static String responseHeader(Map<String, List<String>> headers, String wanted) {
        if (headers == null) return null;
        for (Map.Entry<String, List<String>> entry : headers.entrySet()) {
            if (wanted.equalsIgnoreCase(entry.getKey())) {
                List<String> values = entry.getValue();
                return values == null || values.isEmpty() ? null : values.get(0);
            }
        }
        return null;
    }

    private void httpResponseDiagnostic(Throwable error) {
        synchronized (mLifecycleLock) {
            PlayerSession session = mSession;
            if (!isCurrent(session)) return;
            Throwable cause = error;
            for (int depth = 0; cause != null && depth < 8; ++depth, cause = cause.getCause()) {
                if (!(cause instanceof HttpDataSource.InvalidResponseCodeException)) continue;
                if (mHttpResponseDiagnostics.getAndIncrement() >= 8) return;
                HttpDataSource.InvalidResponseCodeException response =
                    (HttpDataSource.InvalidResponseCodeException) cause;
                // Only these two selected headers: never body, cookies or a whole header map.
                String location = responseHeader(response.headerFields, "Location");
                String contentType = responseHeader(response.headerFields, "Content-Type");
                Log.w(TAG, "MediaHttpResponse generation=" + session.generation
                    + " status=" + response.responseCode
                    + " uri=" + sanitizedUri(response.dataSpec == null ? null : response.dataSpec.uri.toString())
                    + " location=" + (location == null ? "<absent>" : sanitizedUri(location))
                    + " contentType=" + boundedDiagnosticText(contentType));
                return;
            }
        }
    }

    // UI cleanup owns only this captured player. Never publish state or clear current fields here.
    // In particular release must run outside mLifecycleLock: it can wait for the playback thread.
    private void releaseSession(PlayerSession session) {
        if (session == null) return;
        synchronized (session) {
            if (session.released) return;
            session.released = true;
        }
        try {
            if (session.renderer != null) session.renderer.setOutput(null);
            session.player.removeListener(session);
            session.player.clearVideoFrameMetadataListener(session);
            session.player.stop();
        }
        finally { session.player.release(); }
    }

    /**
     * ------ native methods -------
     */
    public static native void nativeFireEvent(long nativeObj, int arg1);

    public static native void nativeSetDuration(long nativeObj, double duration);
    public static native void nativeProcessVideoFrame(long nativeObj, ByteBuffer sampleData,
        int sampleLen, long presentationTimeUs, int outputX, int outputY, int videoX, int videoY,
        int cbcrOffset, int rotation, int videoPF);

    @SuppressWarnings("unused")
    public static Object createMediaPlayer(long nativeObj) {
        return new AxmolMediaPlayer(AxmolEngine.getApplicationContext(), nativeObj);
    }

    /**
     * @param context A {@link Context}.
     */
    public AxmolMediaPlayer(Context context, long nativeObj) {
        super(context);

        mContext = context;
        mNativeObj = nativeObj;
        setAllowedVideoJoiningTimeMs(0);
    }

    @SuppressWarnings("unused")
    public void dispose() {
        synchronized (mLifecycleLock) { mNativeObj = 0; }
        close();
    }

    @Override
    protected void buildVideoRenderers(
        @NonNull Context context,
        @DefaultRenderersFactory.ExtensionRendererMode int extensionRendererMode,
        @NonNull MediaCodecSelector mediaCodecSelector,
        boolean enableDecoderFallback,
        @NonNull Handler eventHandler,
        @NonNull VideoRendererEventListener eventListener,
        long allowedVideoJoiningTimeMs,
        ArrayList<Renderer> out) {
        out.add(
            new ByteBufferVideoRenderer(
                context,
                getCodecAdapterFactory(),
                mediaCodecSelector,
                allowedVideoJoiningTimeMs,
                enableDecoderFallback,
                eventHandler,
                eventListener,
                MAX_DROPPED_VIDEO_FRAME_COUNT_TO_NOTIFY));
    }

    public void setAutoPlay(boolean bAutoPlay) {
        synchronized (mLifecycleLock) {
            mAutoPlay = bAutoPlay;
            mPlayWhenReady = bAutoPlay;
        }
    }

    /**
     * @param sourceUri: url: http(s)://
     *                   disk: file://path/to/file
     *                   assets: file:///android_asset
     * @return
     */
    public boolean open(String sourceUri) {
        final long generation;
        final PlayerSession previous;
        synchronized (mLifecycleLock) {
            if (mState.get() == STATE_PREPARING || mNativeObj == 0) {
                lifecycleDiagnostic("open", "request", mRequestGeneration, sourceUri, false);
                return false;
            }
            previous = mSession;
            generation = ++mRequestGeneration;
            ++mPlaybackIntentVersion;
            mAppliedPlaybackIntentVersion = -1;
            mPlaybackCommands.clear();
            mPlaybackDrainScheduled = false;
            mManualStopApplying = false;
            mManualStopNotified = false;
            mPlaybackTarget = mAutoPlay ? STATE_PLAYING : STATE_PAUSED;
            mBackendPlaying = false;
            mSourceUri = sourceUri;
            mSession = null;
            mPlayer = null;
            mVideoRenderer = null;
            mOutputFormat = null;
            mFrameFormat = null;
            mPlaybackEnded = false;
            mPlayWhenReady = mAutoPlay;
            mState.set(STATE_PREPARING);
            lifecycleDiagnostic("open", "request", generation, sourceUri, true);
        }
        if (previous != null) AxmolEngine.runOnUiThread(() -> releaseSession(previous));
        AxmolEngine.runOnUiThread(() -> applyOpen(generation, sourceUri));
        return true;
    }

    private void applyOpen(long generation, String sourceUri) {
        synchronized (mLifecycleLock) {
            if (generation != mRequestGeneration || mNativeObj == 0) {
                lifecycleDiagnostic("open", "apply", generation, sourceUri, false);
                return;
            }
        }
        ExoPlayer player = null;
        PlayerSession session = null;
        try {
            DataSource.Factory httpFactory = new DefaultHttpDataSource.Factory()
                .setAllowCrossProtocolRedirects(true);
            DataSource.Factory dataSourceFactory = new DefaultDataSource.Factory(mContext, httpFactory);
            MediaSource mediaSource = new ProgressiveMediaSource.Factory(dataSourceFactory)
                .createMediaSource(MediaItem.fromUri(Uri.parse(sourceUri)));
            player = new ExoPlayer.Builder(mContext, this).build();
            ByteBufferVideoRenderer renderer = null;
            for (int i = 0; i < player.getRendererCount(); ++i) {
                Renderer candidate = player.getRenderer(i);
                if (candidate instanceof ByteBufferVideoRenderer) {
                    renderer = (ByteBufferVideoRenderer) candidate;
                    break;
                }
            }
            session = new PlayerSession(generation, player, renderer);
            boolean accepted;
            synchronized (mLifecycleLock) {
                accepted = generation == mRequestGeneration && mNativeObj != 0;
                if (accepted) {
                    mSession = session;
                    mPlayer = player;
                    mVideoRenderer = renderer;
                    player.setVolume((float) mVolume);
                    if (renderer != null) renderer.setOutput(session);
                    player.setVideoFrameMetadataListener(session);
                    player.addListener(session);
                    player.setMediaSource(mediaSource);
                    player.setRepeatMode(mLooping ? Player.REPEAT_MODE_ALL : Player.REPEAT_MODE_OFF);
                    player.prepare();
                    applyPlaybackTargetLocked(session, mPlaybackIntentVersion, mPlaybackTarget);
                }
                lifecycleDiagnostic("open", "apply", generation, sourceUri, accepted);
            }
            if (!accepted) releaseSession(session);
        } catch (Exception failure) {
            Log.e(TAG, "open failed generation=" + generation, failure);
            synchronized (mLifecycleLock) {
                if (generation == mRequestGeneration && mNativeObj != 0) {
                    mSession = null;
                    mPlayer = null;
                    mVideoRenderer = null;
                    mFrameFormat = null;
                    mBackendPlaying = false;
                    mState.set(STATE_ERROR);
                    mPlayWhenReady = false;
                    nativeFireEvent(mNativeObj, EVENT_ERROR);
                }
            }
            if (session != null) releaseSession(session);
            else if (player != null) player.release();
        }
    }

    public boolean close() {
        final PlayerSession previous;
        final long generation;
        final String sourceUri;
        synchronized (mLifecycleLock) {
            generation = ++mRequestGeneration; // Also cancels an open not yet applied.
            ++mPlaybackIntentVersion;
            mPlaybackCommands.clear();
            mPlaybackDrainScheduled = false;
            mManualStopApplying = false;
            mManualStopNotified = false;
            mPlaybackTarget = STATE_CLOSED;
            mBackendPlaying = false;
            sourceUri = mSourceUri;
            previous = mSession;
            mSession = null;
            mPlayer = null;
            mVideoRenderer = null;
            mOutputFormat = null;
            mFrameFormat = null;
            mPlayWhenReady = false;
            mPlaybackEnded = false;
            mState.set(STATE_CLOSED); // Native callers must observe this before close returns.
            lifecycleDiagnostic("close", "request", generation, sourceUri, true);
        }
        if (previous != null) AxmolEngine.runOnUiThread(() -> {
            releaseSession(previous);
            lifecycleDiagnostic("close", "apply", generation, sourceUri, true);
        });
        return true;
    }

    public boolean setLoop(boolean bLooping) {
        synchronized (mLifecycleLock) {
            if (mLooping == bLooping) return true;
            mLooping = bLooping;
            return postPlayerAction(player -> player.setRepeatMode(
                mLooping ? Player.REPEAT_MODE_ALL : Player.REPEAT_MODE_OFF));
        }
    }

    @SuppressWarnings("unused")
    public boolean setRate(double fRate) {
        return postPlayerAction(player -> player.setPlaybackSpeed((float) fRate));
    }

    public boolean setVolume(double volume) {
        synchronized (mLifecycleLock) {
            mVolume = volume;
            if (mSession == null) return true;
            return postPlayerAction(player -> player.setVolume((float) mVolume));
        }
    }

    public double getVolume() {
        return mVolume;
    }

    public boolean setCurrentTime(double fSeekTimeInSec) {
        return postPlayerAction(player -> {
            mPlaybackEnded = false;
            player.seekTo((long) (fSeekTimeInSec * 1000));
        });
    }

    public boolean play() {
        return requestPlaybackTarget(STATE_PLAYING);
    }

    public boolean pause() {
        return requestPlaybackTarget(STATE_PAUSED);
    }

    public boolean stop() {
        return requestPlaybackTarget(STATE_STOPPED);
    }

    /**
     * Get playback State match with native MEMediaState
     */
    public int getState() {
        return mState.get();
    }

    @Override
    public synchronized void onVideoFrameAboutToBeRendered(
        long presentationTimeUs,
        long releaseTimeNs,
        Format format,
        @Nullable MediaFormat mediaFormat) {
        if (mOutputFormat != mediaFormat) {
            // format.sampleMimeType will be video/hevc or video/avc
            mOutputFormat = mediaFormat;
            handleVideoMetaChanged();
        }
    }

    // Raw decoder storage and cropped native output are different layouts. A missing
    // final padding tail is legal; a missing visible byte is not.
    private synchronized void handleVideoMetaChanged() {
        MediaFormat format = mOutputFormat;
        FrameFormat nextFrameFormat = null;
        if (format != null) {
            try {
                int videoPF;
                int colorFormat = format.getInteger(MediaFormat.KEY_COLOR_FORMAT);
                switch (colorFormat) {
                    // Preserve the existing flexible-output interpretation.
                    case MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible:
                    case MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar:
                        videoPF = VIDEO_PF_NV12;
                        break;
                    case MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar:
                        videoPF = VIDEO_PF_I420;
                        break;
                    default:
                        throw new IllegalArgumentException("unsupported color format " + colorFormat);
                }
                int width = format.getInteger(MediaFormat.KEY_WIDTH);
                int height = format.getInteger(MediaFormat.KEY_HEIGHT);
                int rawStride = formatInteger(format, MediaFormat.KEY_STRIDE, 0);
                int rawSliceHeight = formatInteger(format, MediaFormat.KEY_SLICE_HEIGHT, 0);
                Yuv420FrameLayout layout = new Yuv420FrameLayout(width, height,
                    Math.max(width, rawStride), Math.max(height, rawSliceHeight),
                    formatInteger(format, MediaFormat.KEY_CROP_LEFT, 0),
                    formatInteger(format, MediaFormat.KEY_CROP_TOP, 0),
                    formatInteger(format, MediaFormat.KEY_CROP_RIGHT, width - 1),
                    formatInteger(format, MediaFormat.KEY_CROP_BOTTOM, height - 1), videoPF);
                int rotation = formatInteger(format, MediaFormat.KEY_ROTATION, 0);
                nextFrameFormat = new FrameFormat(layout, rotation);
                if (mLayoutDiagnostics.getAndIncrement() < 3)
                    Log.d(TAG, "YuvFrameLayout rawStride=" + rawStride + " rawSlice="
                        + rawSliceHeight + " " + layout);
            } catch (IllegalArgumentException | NullPointerException failure) {
                if (mLayoutRejects.getAndIncrement() < 3)
                    Log.w(TAG, "YuvFrameLayout rejected: " + failure.getMessage());
            }
        }
        mFrameFormat = nextFrameFormat;
    }

    private static int formatInteger(MediaFormat format, String key, int fallback) {
        return format.containsKey(key) ? format.getInteger(key) : fallback;
    }

    /**
     * handler or listener methods
     */
    @Override
    public void processVideoFrame(MediaCodecAdapter codec, int index, long presentationTimeUs) {
        if (mBackendPlaying && mPlayWhenReady && mPlaybackTarget == STATE_PLAYING
            && mState.get() != STATE_PLAYING) {
            mPlaybackEnded = false;
            mState.set(STATE_PLAYING);
            nativeEvent(EVENT_PLAYING);
        }

        if (mNativeObj == 0) {
            return;
        }

        ByteBuffer tmpBuffer = codec.getOutputBuffer(index);
        if (tmpBuffer == null) {
            return;
        }
        FrameFormat frameFormat = mFrameFormat;
        if (frameFormat == null) {
            if (mFrameRejects.getAndIncrement() < 3)
                Log.w(TAG, "YuvFrame rejected: no valid current output layout");
            return;
        }
        Yuv420FrameLayout layout = frameFormat.layout;
        boolean diagnostic = mFrameDiagnostics.getAndIncrement() < 3;
        try {
            mNormalizedFrame = layout.normalize(tmpBuffer, mNormalizedFrame);
            if (diagnostic)
                Log.d(TAG, "YuvFrame normalized capacity=" + tmpBuffer.capacity()
                    + " position=" + tmpBuffer.position() + " limit=" + tmpBuffer.limit()
                    + " remaining=" + tmpBuffer.remaining() + " rotation=" + frameFormat.rotation
                    + " " + layout);
            // The direct output begins at address zero, with an exact normalized limit.
            nativeProcessVideoFrame(mNativeObj, mNormalizedFrame, mNormalizedFrame.remaining(),
                presentationTimeUs, layout.visibleWidth, layout.visibleHeight,
                layout.visibleWidth, layout.visibleHeight, layout.yBytes,
                frameFormat.rotation, layout.pixelFormat);
        } catch (IllegalArgumentException failure) {
            if (mFrameRejects.getAndIncrement() < 3)
                Log.w(TAG, "YuvFrame rejected capacity=" + tmpBuffer.capacity()
                    + " position=" + tmpBuffer.position() + " limit=" + tmpBuffer.limit()
                    + " " + layout + " reason=" + failure.getMessage());
        }
    }

    @Override
    public void onIsPlayingChanged(boolean isPlaying) {
        Log.d(TAG, "[Individual]onIsPlayingChanged: " + isPlaying);
        if (mPlayer == null) return;
        if (isPlaying && mPlaybackTarget == STATE_PLAYING) mPlayWhenReady = true;
        if (!isPlaying) {
            postPlayerAction(player -> {
                    int playbackState = player.getPlaybackState();
                    if (mPlaybackTarget != STATE_STOPPED && !player.getPlayWhenReady()
                        && (playbackState == Player.STATE_READY || playbackState == Player.STATE_BUFFERING)) {
                        if (mAppliedPlaybackIntentVersion == mPlaybackIntentVersion)
                            mPlayWhenReady = false;
                        mState.set(STATE_PAUSED);
                        nativeEvent(EVENT_PAUSED);
                    } else if (playbackState == Player.STATE_IDLE && mState.get() != STATE_STOPPED) {
                        if (isLoopPlaybackActive()) return;
                        mState.set(STATE_STOPPED);
                        nativeEvent(EVENT_STOPPED);
                    }
            });
        }
    }

    /**
     * @param playbackState int STATE_IDLE = 1;
     *                      int STATE_BUFFERING = 2;
     *                      int STATE_READY = 3;
     *                      int STATE_ENDED = 4;
     */
    @Override
    public void onPlaybackStateChanged(int playbackState) {
        Log.d(TAG, "[Individual]onPlaybackStateChanged: " + playbackState);
        if (mPlayer == null) return;
        switch (playbackState) {
            case Player.STATE_READY:
                postPlayerAction(player -> {
                    // An initially paused prepare has no true->false isPlaying edge.
                    // Publish its real READY/not-playing state so its preview can upload.
                    if (player.getPlaybackState() == Player.STATE_READY && !player.getPlayWhenReady())
                        mState.set(STATE_PAUSED);
                    nativeSetDuration(mNativeObj, player.getContentDuration() / 1000.0);
                });
                break;
            case Player.STATE_ENDED:
                if (mPlayer.getPlaybackState() != Player.STATE_ENDED) break;
                mBackendPlaying = false;
                if (!isLoopPlaybackActive()) {
                    mPlaybackEnded = true;
                    if (mAppliedPlaybackIntentVersion == mPlaybackIntentVersion)
                        mPlayWhenReady = false;
                    mState.set(STATE_STOPPED);
                    nativeEvent(EVENT_STOPPED);
                }
                break;
            case Player.STATE_IDLE:
                if (mPlayer.getPlaybackState() != Player.STATE_IDLE) break;
                mBackendPlaying = false;
                if (mManualStopApplying) {
                    publishManualStopIfIdleLocked(mSession);
                    break;
                }
                if (mState.get() != STATE_STOPPED && !isLoopPlaybackActive()) {
                    mState.set(STATE_STOPPED);
                    nativeEvent(EVENT_STOPPED);
                }
                break;
            default:
                ;
        }
    }

    @Override
    public void onPlayerError(PlaybackException error) {
        Log.e(TAG, "onPlayerError: " + error.getMessage());
        httpResponseDiagnostic(error);
        if (mPlayer == null) return;
        mBackendPlaying = false;
        mState.set(STATE_ERROR);
        nativeEvent(EVENT_ERROR);
    }

    public boolean isPlaybackEnded() {
        return mPlaybackEnded;
    }

    @Override
    public void onVideoSizeChanged(VideoSize videoSize) {
        Log.d(TAG, String.format("[Individual]onVideoSizeChanged: (%d,%d)", videoSize.width, videoSize.height));

        if (mPlayer != null)
            handleVideoMetaChanged();
    }

    @Override
    public void onIsLoadingChanged(boolean isLoading) {
        Log.d(TAG, "[Individual]onIsLoadingChanged: " + isLoading);
    }

    public void nativeEvent(int event) {
        if (mNativeObj != 0 && mPlayer != null) {
            nativeFireEvent(mNativeObj, event);
        }
    }

    private boolean isLoopPlaybackActive() {
        // Loop event classification follows the actual player, not a later queued PLAY.
        return mLooping && mPlayer != null && mPlayer.getPlayWhenReady();
    }
}
