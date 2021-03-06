/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VIDEO_SESSION_H_
#define VIDEO_SESSION_H_

#include "nsAutoPtr.h"
#include "mozilla/Attributes.h"

#include "MediaConduitInterface.h"
#include "MediaEngineWrapper.h"

// conflicts with #include of scoped_ptr.h
#undef FF
// Video Engine Includes
#include "webrtc/common_types.h"
#ifdef FF
#undef FF // Avoid name collision between scoped_ptr.h and nsCRTGlue.h.
#endif
#include "webrtc/modules/video_coding/codecs/interface/video_codec_interface.h"
#include "webrtc/video_engine/include/vie_base.h"
#include "webrtc/video_engine/include/vie_capture.h"
#include "webrtc/video_engine/include/vie_codec.h"
#include "webrtc/video_engine/include/vie_external_codec.h"
#include "webrtc/video_engine/include/vie_render.h"
#include "webrtc/video_engine/include/vie_network.h"
#include "webrtc/video_engine/include/vie_rtp_rtcp.h"

/** This file hosts several structures identifying different aspects
 * of a RTP Session.
 */

 using  webrtc::ViEBase;
 using  webrtc::ViENetwork;
 using  webrtc::ViECodec;
 using  webrtc::ViECapture;
 using  webrtc::ViERender;
 using  webrtc::ViEExternalCapture;
 using  webrtc::ViEExternalCodec;

namespace mozilla {

class WebrtcAudioConduit;

// Interface of external video encoder for WebRTC.
class WebrtcVideoEncoder:public VideoEncoder
                         ,public webrtc::VideoEncoder
{};

// Interface of external video decoder for WebRTC.
class WebrtcVideoDecoder:public VideoDecoder
                         ,public webrtc::VideoDecoder
{};

/**
 * Concrete class for Video session. Hooks up
 *  - media-source and target to external transport
 */
class WebrtcVideoConduit:public VideoSessionConduit
                         ,public webrtc::Transport
                         ,public webrtc::ExternalRenderer
{
public:
  //VoiceEngine defined constant for Payload Name Size.
  static const unsigned int CODEC_PLNAME_SIZE;

  /**
   * Set up A/V sync between this (incoming) VideoConduit and an audio conduit.
   */
  void SyncTo(WebrtcAudioConduit *aConduit);

  /**
   * Function to attach Renderer end-point for the Media-Video conduit.
   * @param aRenderer : Reference to the concrete Video renderer implementation
   * Note: Multiple invocations of this API shall remove an existing renderer
   * and attaches the new to the Conduit.
   */
  virtual MediaConduitErrorCode AttachRenderer(mozilla::RefPtr<VideoRenderer> aVideoRenderer);
  virtual void DetachRenderer();

  /**
   * APIs used by the registered external transport to this Conduit to
   * feed in received RTP Frames to the VideoEngine for decoding
   */
  virtual MediaConduitErrorCode ReceivedRTPPacket(const void *data, int len);

  /**
   * APIs used by the registered external transport to this Conduit to
   * feed in received RTP Frames to the VideoEngine for decoding
   */
  virtual MediaConduitErrorCode ReceivedRTCPPacket(const void *data, int len);

   /**
   * Function to configure send codec for the video session
   * @param sendSessionConfig: CodecConfiguration
   * @result: On Success, the video engine is configured with passed in codec for send
   *          On failure, video engine transmit functionality is disabled.
   * NOTE: This API can be invoked multiple time. Invoking this API may involve restarting
   *        transmission sub-system on the engine.
   */
  virtual MediaConduitErrorCode ConfigureSendMediaCodec(const VideoCodecConfig* codecInfo);

  /**
   * Function to configure list of receive codecs for the video session
   * @param sendSessionConfig: CodecConfiguration
   * @result: On Success, the video engine is configured with passed in codec for send
   *          Also the playout is enabled.
   *          On failure, video engine transmit functionality is disabled.
   * NOTE: This API can be invoked multiple time. Invoking this API may involve restarting
   *        transmission sub-system on the engine.
   */
   virtual MediaConduitErrorCode ConfigureRecvMediaCodecs(
                               const std::vector<VideoCodecConfig* >& codecConfigList);

  /**
   * Register Transport for this Conduit. RTP and RTCP frames from the VideoEngine
   * shall be passed to the registered transport for transporting externally.
   */
  virtual MediaConduitErrorCode AttachTransport(mozilla::RefPtr<TransportInterface> aTransport);

  /**
   * Function to select and change the encoding resolution based on incoming frame size
   * and current available bandwidth.
   * @param width, height: dimensions of the frame
   */
  virtual bool SelectSendResolution(unsigned short width,
                                    unsigned short height);

  /**
   * Function to deliver a capture video frame for encoding and transport
   * @param video_frame: pointer to captured video-frame.
   * @param video_frame_length: size of the frame
   * @param width, height: dimensions of the frame
   * @param video_type: Type of the video frame - I420, RAW
   * @param captured_time: timestamp when the frame was captured.
   *                       if 0 timestamp is automatcally generated by the engine.
   *NOTE: ConfigureSendMediaCodec() SHOULD be called before this function can be invoked
   *       This ensures the inserted video-frames can be transmitted by the conduit
   */
  virtual MediaConduitErrorCode SendVideoFrame(unsigned char* video_frame,
                                                unsigned int video_frame_length,
                                                unsigned short width,
                                                unsigned short height,
                                                VideoType video_type,
                                                uint64_t capture_time);

  /**
   * Set an external encoder object |encoder| to the payload type |pltype|
   * for sender side codec.
   */
  virtual MediaConduitErrorCode SetExternalSendCodec(VideoCodecConfig* config,
                                                     VideoEncoder* encoder);

  /**
   * Set an external decoder object |decoder| to the payload type |pltype|
   * for receiver side codec.
   */
  virtual MediaConduitErrorCode SetExternalRecvCodec(VideoCodecConfig* config,
                                                     VideoDecoder* decoder);


  /**
   * Webrtc transport implementation to send and receive RTP packet.
   * VideoConduit registers itself as ExternalTransport to the VideoEngine
   */
  virtual int SendPacket(int channel, const void *data, int len) ;

  /**
   * Webrtc transport implementation to send and receive RTCP packet.
   * VideoConduit registers itself as ExternalTransport to the VideoEngine
   */
  virtual int SendRTCPPacket(int channel, const void *data, int len) ;


  /**
   * Webrtc External Renderer Implementation APIs.
   * Raw I420 Frames are delivred to the VideoConduit by the VideoEngine
   */
  virtual int FrameSizeChange(unsigned int, unsigned int, unsigned int);

  virtual int DeliverFrame(unsigned char*,int, uint32_t , int64_t,
                           void *handle);

  /**
   * Does DeliverFrame() support a null buffer and non-null handle
   * (video texture)?
   * B2G support it (when using HW video decoder with graphic buffer output).
   * XXX Investigate!  Especially for Android
   */
  virtual bool IsTextureSupported() {
#ifdef WEBRTC_GONK
    return true;
#else
    return false;
#endif
  }

  unsigned short SendingWidth() {
    return mSendingWidth;
  }

  unsigned short SendingHeight() {
    return mSendingHeight;
  }

  unsigned int SendingMaxFs() {
    if(mCurSendCodecConfig) {
      return mCurSendCodecConfig->mMaxFrameSize;
    }
    return 0;
  }

  unsigned int SendingMaxFr() {
    if(mCurSendCodecConfig) {
      return mCurSendCodecConfig->mMaxFrameRate;
    }
    return 0;
  }

  WebrtcVideoConduit():
                      mOtherDirection(nullptr),
                      mShutDown(false),
                      mVideoEngine(nullptr),
                      mTransport(nullptr),
                      mRenderer(nullptr),
                      mPtrExtCapture(nullptr),
                      mEngineTransmitting(false),
                      mEngineReceiving(false),
                      mChannel(-1),
                      mCapId(-1),
                      mCurSendCodecConfig(nullptr),
                      mSendingWidth(0),
                      mSendingHeight(0),
                      mReceivingWidth(640),
                      mReceivingHeight(480),
                      mVideoLatencyTestEnable(false),
                      mVideoLatencyAvg(0),
                      mMinBitrate(200),
                      mStartBitrate(300),
                      mMaxBitrate(2000)
  {
  }

  virtual ~WebrtcVideoConduit() ;

  MediaConduitErrorCode Init(WebrtcVideoConduit *other);

  int GetChannel() { return mChannel; }
  webrtc::VideoEngine* GetVideoEngine() { return mVideoEngine; }
  bool GetLocalSSRC(unsigned int* ssrc);
  bool GetRemoteSSRC(unsigned int* ssrc);
  bool GetAVStats(int32_t* jitterBufferDelayMs,
                  int32_t* playoutBufferDelayMs,
                  int32_t* avSyncOffsetMs);
  bool GetRTPStats(unsigned int* jitterMs, unsigned int* cumulativeLost);
  bool GetRTCPReceiverReport(DOMHighResTimeStamp* timestamp,
                             uint32_t* jitterMs,
                             uint32_t* packetsReceived,
                             uint64_t* bytesReceived,
                             uint32_t* cumulativeLost,
                             int32_t* rttMs);
  bool GetRTCPSenderReport(DOMHighResTimeStamp* timestamp,
                           unsigned int* packetsSent,
                           uint64_t* bytesSent);
  uint64_t MozVideoLatencyAvg();

private:

  WebrtcVideoConduit(const WebrtcVideoConduit& other) MOZ_DELETE;
  void operator=(const WebrtcVideoConduit& other) MOZ_DELETE;

  //Local database of currently applied receive codecs
  typedef std::vector<VideoCodecConfig* > RecvCodecList;

  //Function to convert between WebRTC and Conduit codec structures
  void CodecConfigToWebRTCCodec(const VideoCodecConfig* codecInfo,
                                webrtc::VideoCodec& cinst);

  // Function to copy a codec structure to Conduit's database
  bool CopyCodecToDB(const VideoCodecConfig* codecInfo);

  // Functions to verify if the codec passed is already in
  // conduits database
  bool CheckCodecForMatch(const VideoCodecConfig* codecInfo) const;
  bool CheckCodecsForMatch(const VideoCodecConfig* curCodecConfig,
                           const VideoCodecConfig* codecInfo) const;

  //Checks the codec to be applied
  MediaConduitErrorCode ValidateCodecConfig(const VideoCodecConfig* codecInfo, bool send) const;

  //Utility function to dump recv codec database
  void DumpCodecDB() const;

  // Video Latency Test averaging filter
  void VideoLatencyUpdate(uint64_t new_sample);

  // The two sides of a send/receive pair of conduits each keep a pointer to the other.
  // They also share a single VideoEngine and mChannel.  Shutdown must be coordinated
  // carefully to avoid double-freeing or accessing after one frees.
  WebrtcVideoConduit*  mOtherDirection;
  // The other side has shut down our mChannel and related items already
  bool mShutDown;

  // A few of these are shared by both directions.  They're released by the last
  // conduit to die.
  webrtc::VideoEngine* mVideoEngine;          // shared
  mozilla::RefPtr<TransportInterface> mTransport;
  mozilla::RefPtr<VideoRenderer> mRenderer;

  ScopedCustomReleasePtr<webrtc::ViEBase> mPtrViEBase;
  ScopedCustomReleasePtr<webrtc::ViECapture> mPtrViECapture;
  ScopedCustomReleasePtr<webrtc::ViECodec> mPtrViECodec;
  ScopedCustomReleasePtr<webrtc::ViENetwork> mPtrViENetwork;
  ScopedCustomReleasePtr<webrtc::ViERender> mPtrViERender;
  ScopedCustomReleasePtr<webrtc::ViERTP_RTCP> mPtrRTP;
  ScopedCustomReleasePtr<webrtc::ViEExternalCodec> mPtrExtCodec;

  webrtc::ViEExternalCapture* mPtrExtCapture; // shared

  // Engine state we are concerned with.
  bool mEngineTransmitting; //If true ==> Transmit Sub-system is up and running
  bool mEngineReceiving;    // if true ==> Receive Sus-sysmtem up and running

  int mChannel; // Video Channel for this conduit
  int mCapId;   // Capturer for this conduit
  RecvCodecList    mRecvCodecList;
  VideoCodecConfig* mCurSendCodecConfig;
  unsigned short mSendingWidth;
  unsigned short mSendingHeight;
  unsigned short mReceivingWidth;
  unsigned short mReceivingHeight;
  bool mVideoLatencyTestEnable;
  uint64_t mVideoLatencyAvg;
  uint32_t mMinBitrate;
  uint32_t mStartBitrate;
  uint32_t mMaxBitrate;

  static const unsigned int sAlphaNum = 7;
  static const unsigned int sAlphaDen = 8;
  static const unsigned int sRoundingPadding = 1024;

  mozilla::RefPtr<WebrtcAudioConduit> mSyncedTo;

  nsAutoPtr<VideoCodecConfig> mExternalSendCodec;
  nsAutoPtr<VideoCodecConfig> mExternalRecvCodec;
};

} // end namespace

#endif
