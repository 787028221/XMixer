//
// Created by Andy on 2020/4/29.
//

#ifndef XAUDIOENCODER_H
#define XAUDIOENCODER_H

#define OUT_TO_FILE 0

#include "XFFHeader.h"
#include <string>
#include <vector>

class XDecoder;

class XMixer {
public:
    XMixer();

    ~XMixer();

    void add(const std::string& filename);

    void mix(const std::string& outPath);

private:
    int openOutFile(const std::string& filename);

    int addAudioStream();

    int encodeAudioFrame();

private:
    const int OUT_SAMPLE_FMT = AV_SAMPLE_FMT_S16;
    const int OUT_SAMPLE_RATE = 44100;
    const int OUT_SAMPLE_CHANNELS = 2;
    const uint64_t OUT_SAMPLE_CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;

private:
    int mAudioIndex;
    std::shared_ptr<AVFormatContext> mFormatCtx;
    std::shared_ptr<AVCodecContext> mAudioCodecCtx;

    int mEncodeSampleCount;

    std::vector<std::shared_ptr<XDecoder>> mDecoderList;

    long mDuration;

    std::unique_ptr<Frame> mAudioFrame;

#if OUT_TO_FILE
    FILE* mFile;
#endif
    
};

#endif //XAUDIOENCODER_H
