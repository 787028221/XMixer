//
// Created by Andy on 2020/4/23.
//

#include "XDecoder.h"
#include "XException.h"
#include "XPacketQueue.h"
#include "XThreadUtils.h"

XDecoder::XDecoder(const std::string &filename)
        : mFilename(filename), mAudioIndex(-1), mEncodedSampleCount(0), mSampleBuffer(nullptr), mSeekToStartTime(false),
          mAborted(false), mSampleQueue(nullptr) {

    av_log_set_flags(AV_LOG_SKIP_REPEATED);

    int ret = openInFile();
    if (ret < 0) {
        throw XException(av_err2str(ret));
    }
}

XDecoder::~XDecoder() {
    if (!mAborted) {
        stop();
    }
}

void XDecoder::start() {
    mReadTid = std::make_unique<std::thread>([this] { readWorkThread(this); });
}

int XDecoder::openInFile() {
    AVFormatContext *ic = nullptr;
    int ret = avformat_open_input(&ic, mFilename.data(), nullptr, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] avformat_open_input failed: %s\n", av_err2str(ret));
        return ret;
    }
    mFormatCtx = std::unique_ptr<AVFormatContext, InputFormatDeleter>(ic);

    ret = avformat_find_stream_info(ic, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] avformat_find_stream_info failed: %s\n", av_err2str(ret));
        return ret;
    }

    mAudioIndex = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (mAudioIndex >= 0) {
        ret = openCodecContext(mAudioIndex);
        if (ret < 0) {
            return ret;
        }
    }

    if (mAudioIndex < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] av_find_best_stream failed: audio stream not found\n");
        return AVERROR_STREAM_NOT_FOUND;
    }

    return 0;
}

int XDecoder::openCodecContext(int streamIndex) {
    if (streamIndex < 0) {
        return AVERROR(EINVAL);
    }

    AVCodecContext *avctx = avcodec_alloc_context3(nullptr);
    if (!avctx) {
        return AVERROR(ENOMEM);
    }

    if (streamIndex == mAudioIndex) {
        mAudioCodecCtx = std::unique_ptr<AVCodecContext, CodecDeleter>(avctx);
    }

    AVStream *stream = mFormatCtx->streams[streamIndex];
    stream->discard = AVDISCARD_DEFAULT;
    int ret = avcodec_parameters_to_context(avctx, stream->codecpar);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] avcodec_parameters_to_context failed: %s\n", av_err2str(ret));
        return ret;
    }
    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    if (!codec) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] avcodec_find_decoder failed: decoder (%s) not found\n",
               avcodec_get_name(avctx->codec_id));
        return AVERROR_DECODER_NOT_FOUND;
    }

    ret = avcodec_open2(avctx, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "[XDecoder] avcodec_open2 failed: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

void XDecoder::readWorkThread(void *opaque) {
    XThreadUtils::configThreadName("readWorkThread");
    av_log(nullptr, AV_LOG_INFO, "[XDecoder] readWorkThread ++++++\n");
    auto decoder = reinterpret_cast<XDecoder *>(opaque);
    if (!decoder || !decoder->mFormatCtx) {
        return;
    }

    AVFormatContext *ic = decoder->mFormatCtx.get();
    int audioStreamIndex = -1;
    XPacketQueue *audioPktq = nullptr;
    if (decoder->mAudioIndex >= 0 && !decoder->mAudioPacketQueue) {
        decoder->mAudioPacketQueue = std::make_unique<XPacketQueue>();
        audioPktq = decoder->mAudioPacketQueue.get();
        audioStreamIndex = decoder->mAudioIndex;
        mAudioTid = std::make_unique<std::thread>([this, decoder] { audioWorkThread(decoder); });
    }

    int ret;
    for (;;) {
        if (decoder->mAborted) {
            break;
        }

        auto pkt = std::make_shared<Packet>();
        ret = av_read_frame(ic, pkt->avpkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF && (decoder->mStatus & S_READ_END) != S_READ_END) {
                decoder->mStatus |= S_READ_END;
                if (audioPktq) {
                    audioPktq->putNullPacket(audioStreamIndex);
                }
            }
            // 读完包以后直接结束
            break;
        }

        if (audioStreamIndex >= 0 && pkt->avpkt->stream_index == audioStreamIndex) {
            audioPktq->put(pkt);
        }
    }

    if (mAudioTid && mAudioTid->joinable()) {
        mAudioTid->join();
    }

    av_log(nullptr, AV_LOG_INFO, "[XDecoder] readWorkThread ------\n");
}

void XDecoder::audioWorkThread(void *opaque) {
    XThreadUtils::configThreadName("audioWorkThread");
    av_log(nullptr, AV_LOG_INFO, "[XDecoder] audioWorkThread ++++++\n");
    auto decoder = reinterpret_cast<XDecoder *>(opaque);
    if (!decoder || !decoder->mAudioCodecCtx) {
        return;
    }

    if (!decoder->mSampleQueue) {
        decoder->mSampleQueue = rbuf_create(4096);
        rbuf_set_mode(decoder->mSampleQueue, RBUF_MODE_BLOCKING);
    }
    int ret;
    for (;;) {
        if (decoder->mAborted) {
            break;
        }

        if (rbuf_available(decoder->mSampleQueue) > 0) {
            ret = decoder->decodeAudioFrame();
            if (ret < 0) {
                break;
            }
        }
    }

    av_log(nullptr, AV_LOG_INFO, "[XDecoder] audioWorkThread ------\n");
}

int XDecoder::decodeAudioFrame() {
    int ret = AVERROR(EAGAIN);
    for (;;) {
        // get packet
        auto pkt = mAudioPacketQueue->get();
        if (!pkt) {
            mStatus |= S_AUDIO_END;
            return -1;
        }

        // send packet
        ret = avcodec_send_packet(mAudioCodecCtx.get(), pkt->avpkt);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        // receive frame
        do {
            auto frame = std::make_shared<Frame>();
            ret = avcodec_receive_frame(mAudioCodecCtx.get(), frame->avframe);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                return ret;
            }

            if (ret == AVERROR_EOF) {
                mStatus |= S_AUDIO_END;
                avcodec_flush_buffers(mAudioCodecCtx.get());
                return ret;
            }

            if (ret >= 0) {
                return sampleConvert(frame->avframe);
            }

            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_log(nullptr, AV_LOG_FATAL, "[XFFProducer] avcodec_receive_frame failed: %s\n", av_err2str(ret));
                return ret;
            }

        } while (ret != AVERROR(EAGAIN));
    }
    return 0;
}

void XDecoder::closeCodecCtx(int streamIndex) {
    if (streamIndex >= 0 && streamIndex == mAudioIndex) {
        if (mAudioCodecCtx) {
            mAudioCodecCtx.reset();
        }
    }
}

void XDecoder::closeInFile() {
    closeCodecCtx(mAudioIndex);

    if (mFormatCtx) {
        mFormatCtx.reset();
    }
}


int XDecoder::sampleConvert(AVFrame *src) {

    if (!mSwrContext) {
        SwrContext *swr = swr_alloc();
        if (!swr) {
            return AVERROR(ENOMEM);
        }

        av_opt_set_channel_layout(swr, "in_channel_layout", src->channel_layout, 0);
        av_opt_set_int(swr, "in_sample_rate", src->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt", static_cast<AVSampleFormat>(src->format), 0);

        av_opt_set_channel_layout(swr, "out_channel_layout", OUT_SAMPLE_CHANNEL_LAYOUT, 0);
        av_opt_set_int(swr, "out_sample_rate", OUT_SAMPLE_RATE, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", static_cast<AVSampleFormat >(OUT_SAMPLE_FMT), 0);

        if (swr && swr_init(swr) < 0) {
            swr_free(&swr);
            return AVERROR(EINVAL);
        }

        mSwrContext = std::unique_ptr<SwrContext, SwrContextDeleter>(swr);
    }

    const uint8_t** in = (const uint8_t **) src->extended_data;
    uint8_t *data = nullptr;
    unsigned int dataSize = 0;

    int out_count = src->nb_samples * OUT_SAMPLE_RATE / src->sample_rate + 256;
    int out_size = av_samples_get_buffer_size(nullptr, av_get_channel_layout_nb_channels(OUT_SAMPLE_CHANNEL_LAYOUT), out_count, static_cast<AVSampleFormat >(OUT_SAMPLE_FMT), 1);

    av_fast_malloc(&data, &dataSize, out_size);
    if (!data) {
        return AVERROR(ENOMEM);
    }

    int len = swr_convert(mSwrContext.get(), &data, out_count, in, src->nb_samples);
    int size = len * 2 * av_get_bytes_per_sample(static_cast<AVSampleFormat >(OUT_SAMPLE_FMT));

    rbuf_write(mSampleQueue, data, size);

    return size;
}

int XDecoder::getSamples(uint8_t *out, int length) {
    if (!mSampleQueue) {
        return AVERROR(ENOMEM);
    }

    if ((mStatus & S_AUDIO_END) == S_AUDIO_END && rbuf_used(mSampleQueue) <= 0) {
        return -1;
    }

    return rbuf_read(mSampleQueue, out, length);
}

void XDecoder::stop() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mAborted = true;
    }

    if (mReadTid && mReadTid->joinable()) {
        mReadTid->join();
    }

    closeInFile();

    if (mSampleQueue) {
        rbuf_destroy(mSampleQueue);
        mSampleQueue = nullptr;
    }
}
