//
// Created by Andy on 2020/4/23.
//

#ifndef NATIVECODE_XAUDIODECODER_H
#define NATIVECODE_XAUDIODECODER_H

#include "XFFHeader.h"
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "XSampleQueue.h"

class XPacketQueue;

class XDecoder {
public:
    XDecoder(const std::string& filename);

    ~XDecoder();

    void start();

    int getSamples(uint8_t* out, int length);
    
    void stop();

private:
    int openInFile();

    int openCodecContext(int streamIndex);

    void closeCodecCtx(int streamIndex);

    void closeInFile();

private:
    int decodeAudioFrame();

    int sampleConvert(AVFrame* src);

private:
    void readWorkThread(void* opaque);

    void audioWorkThread(void* opaque);

private:
    unsigned int mStatus = 0;
    const int S_READ_END = 1 << 0;
    const int S_AUDIO_END = 1 << 1;

private:
    const int OUT_SAMPLE_FMT = AV_SAMPLE_FMT_S16;
    const int OUT_SAMPLE_RATE = 44100;
    const uint64_t OUT_SAMPLE_CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;

private:
    int mAudioIndex;
    std::unique_ptr<AVFormatContext, InputFormatDeleter> mFormatCtx;
    std::unique_ptr<AVCodecContext, CodecDeleter> mAudioCodecCtx;
    std::unique_ptr<SwrContext, SwrContextDeleter> mSwrContext;

    std::unique_ptr<XPacketQueue> mAudioPacketQueue;

    std::unique_ptr<std::thread> mReadTid;
    std::mutex mMutex;

    std::unique_ptr<std::thread> mAudioTid;

    uint8_t* mSampleBuffer;

    int mEncodedSampleCount;

    bool mSeekToStartTime;

    std::string mFilename;

    bool mAborted;

    rbuf_t* mSampleQueue;
};


#endif //NATIVECODE_XAUDIODECODER_H
