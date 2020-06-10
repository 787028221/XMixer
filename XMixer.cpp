//
// Created by Andy on 2020/4/29.
//

#include "XMixer.h"
#include "XDecoder.h"
#include "XException.h"

XMixer::XMixer()
        : mAudioIndex(-1), mEncodeSampleCount(0), mDuration(0) {
#if OUT_TO_FILE
    mFile = fopen("/Users/andy/Desktop/output.pcm", "wb+");
#endif
}

XMixer::~XMixer() {

}

void XMixer::add(const std::string& filename) {
    try {
        auto decoder = std::make_shared<XDecoder>(filename);
        decoder->start();
        mDecoderList.emplace_back(decoder);
    } catch (std::exception& e) {
        throw XException("添加素材失败: 创建解码器失败!");
    }
}

void XMixer::mix(const std::string& outPath) {

    int ret = openOutFile(outPath);
    if (ret < 0) {
        return;
    }

    // alloc output AVFrame
    mAudioFrame = std::make_unique<Frame>();
    mAudioFrame->avframe->nb_samples = mAudioCodecCtx->frame_size;
    mAudioFrame->avframe->format = mAudioCodecCtx->sample_fmt;

    int bufferSize = av_samples_get_buffer_size(nullptr, mAudioCodecCtx->channels, mAudioCodecCtx->frame_size, mAudioCodecCtx->sample_fmt, 1);
    uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(bufferSize));

    avcodec_fill_audio_frame(mAudioFrame->avframe, mAudioCodecCtx->channels, mAudioCodecCtx->sample_fmt, buffer, bufferSize, 1);

    int size = mDecoderList.size();
    int readed = 0;
    for (;;) {
        for (int i = 0; i < size; ++i) {
            auto decoder = mDecoderList.at(i);
            readed = decoder->getSamples(buffer, bufferSize);
        }

        if (readed < 0 && readed != AVERROR(ENOMEM)) {
            break;
        }

        if (readed > 0) {
#if OUT_TO_FILE
            fwrite(buffer, 1, bufferSize, mFile);
#else
            ret = encodeAudioFrame();
#endif
            av_log(nullptr, AV_LOG_INFO, "[XMixer] encode samples: %d\n", readed);
            if (ret < 0) {
                av_log(nullptr, AV_LOG_FATAL, "[XMixer] encode audio frame ret: %d, str: %s\n", ret, av_err2str(ret));
                break;
            }
        }
    }

    if (buffer) {
        av_free(buffer);
        buffer = nullptr;
    }

    ret = av_write_trailer(mFormatCtx.get());
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] av_write_trailer failed: %s\n", av_err2str(ret));
    }

    if (mAudioCodecCtx) {
        mAudioCodecCtx.reset();
    }

    if (mFormatCtx) {
        avio_close(mFormatCtx->pb);
        mFormatCtx.reset();
    }

#if OUT_TO_FILE
    fclose(mFile);
#endif

    av_log(nullptr, AV_LOG_INFO, "[XMixer] 合成完成: %s\n", outPath.data());
}

int XMixer::openOutFile(const std::string &filename) {
    AVFormatContext *ic = nullptr;
    int ret = avformat_alloc_output_context2(&ic, nullptr, nullptr, filename.data());
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avformat_alloc_output_context2 failed: %s\n", av_err2str(ret));
        return ret;
    }
    mFormatCtx = std::shared_ptr<AVFormatContext>(ic, OutputFormatDeleter());

    ret = addAudioStream();
    if (ret < 0) {
        return ret;
    }

    ret = avio_open(&ic->pb, filename.data(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avio_open failed: %s\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_write_header(ic, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avformat_write_header failed: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

int XMixer::addAudioStream() {
    if (!mFormatCtx) {
        return AVERROR(EINVAL);
    }

    AVCodec *codec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!codec) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] cannot find (%s) encoder\n", avcodec_get_name(AV_CODEC_ID_AAC));
        return AVERROR_ENCODER_NOT_FOUND;
    }

    AVCodecContext *avctx = avcodec_alloc_context3(codec);
    if (!avctx) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avcodec_alloc_context3 failed\n");
        return AVERROR(ENOMEM);
    }
    mAudioCodecCtx = std::shared_ptr<AVCodecContext>(avctx, CodecDeleter());

    avctx->sample_fmt = static_cast<AVSampleFormat>(OUT_SAMPLE_FMT);
    avctx->sample_rate = OUT_SAMPLE_RATE;
    avctx->channel_layout = AV_CH_LAYOUT_STEREO;
    avctx->channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
    avctx->time_base = {1, avctx->sample_rate};

    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret = avcodec_open2(avctx, nullptr, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avcodec_open2 failed: %s\n", av_err2str(ret));
        return ret;
    }

    AVStream *stream = avformat_new_stream(mFormatCtx.get(), codec);
    if (!stream) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] cannot new audio stream\n");
        return AVERROR(ENOMEM);
    }
    mAudioIndex = stream->index;
    stream->time_base = avctx->time_base;

    ret = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avcodec_parameters_from_context failed: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

int XMixer::encodeAudioFrame() {

    mAudioFrame->avframe->pts = mEncodeSampleCount;
    int ret = avcodec_send_frame(mAudioCodecCtx.get(), mAudioFrame->avframe);
    if (ret < 0 && ret != AVERROR(EOF) && ret != AVERROR(EAGAIN)) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avcodec_send_frame failed: %s\n", av_err2str(ret));
        return ret;
    }
    mEncodeSampleCount += mAudioFrame->avframe->nb_samples;

    auto pkt = std::make_unique<Packet>();
    ret = avcodec_receive_packet(mAudioCodecCtx.get(), pkt->avpkt);
    if (ret >= 0) {
        AVStream *stream = mFormatCtx->streams[mAudioIndex];
        av_packet_rescale_ts(pkt->avpkt, mAudioCodecCtx->time_base, stream->time_base);
        pkt->avpkt->stream_index = stream->index;

        ret = av_interleaved_write_frame(mFormatCtx.get(), pkt->avpkt);
        if (ret < 0) {
            av_log(nullptr, AV_LOG_FATAL, "[XMixer] av_interleaved_write_frame failed: %s\n", av_err2str(ret));
            return ret;
        }
        return 1;
    }

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        av_log(nullptr, AV_LOG_FATAL, "[XMixer] avcodec_receive_packet failed: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}