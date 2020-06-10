//
// Created by Andy on 2020/4/23.
//

#ifndef NATIVECODE_XFFHEADER_H
#define NATIVECODE_XFFHEADER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

struct InputFormatDeleter {
    void operator()(AVFormatContext* ic) {
        avformat_close_input(&ic);
    }
};

struct OutputFormatDeleter {
    void operator()(AVFormatContext* ic) {
        avformat_free_context(ic);
    }
};

struct CodecDeleter {
    void operator()(AVCodecContext* avctx) {
        avcodec_free_context(&avctx);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* sws) {
        sws_freeContext(sws);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* swr) {
        swr_free(&swr);
    }
};

struct Packet {

    AVPacket* avpkt = nullptr;
    int flag;

    Packet() {
        this->avpkt = av_packet_alloc();
        av_init_packet(this->avpkt);
        this->avpkt->data = nullptr;
        this->avpkt->size = 0;
        this->flag = 0;
    }

    ~Packet() {
        if (this->avpkt) {
            av_packet_free(&this->avpkt);
        }
    }
};

struct Frame {
    AVFrame* avframe = nullptr;

    Frame() {
        avframe = av_frame_alloc();
    }

    ~Frame() {
        if (avframe) {
            av_frame_free(&avframe);
        }
    }
};

#include <pthread.h>
inline void configThreadName(const char* name) {
#if __APPLE__
    pthread_setname_np(name);
#elif __ANDROID__
    pthread_setname_np(pthread_self(), name);
#endif
}

#endif //NATIVECODE_XFFHEADER_H
