//
// Created by JeffMony Lee on 2020/10/27.
//

#include <jni.h>
#include <string>
#include <complex.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "android_log.h"
#include "ffmpeg_options_const.h"
#include "process_error.h"
}

extern "C"
JNIEXPORT void JNICALL
Java_com_jeffmony_ffmpeglib_FFmpegVideoUtils_printVideoInfo(JNIEnv *env, jclass clazz,
                                                            jstring src_path) {
    if (use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }
    AVFormatContext *ifmt_ctx = NULL;
    int ret;
    const char *in_filename = env->GetStringUTFChars(src_path, 0);
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        LOGE("Cannot open input file");
        goto end;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        LOGE("Failed to retrieve input stream information");
        goto end;
    }
    av_dump_format(ifmt_ctx, 0, in_filename, 0);

end:
    avformat_close_input(&ifmt_ctx);

}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_jeffmony_ffmpeglib_FFmpegVideoUtils_getVideoInfo(JNIEnv *env, jclass clazz,
                                                          jstring input_path) {
    if (use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }

    const char *in_filename = env->GetStringUTFChars(input_path, 0);
    AVFormatContext *ifmt_ctx = NULL;
    int ret;
    int i;
    int video_index = -1, audio_index = -1;
    int width, height;
    int64_t duration;
    AVCodecID video_id = AV_CODEC_ID_NONE, audio_id = AV_CODEC_ID_NONE;
    const AVCodec *video_codec, *audio_codec;
    AVDictionary *option = NULL;
    av_dict_set(&option, "allowed_extensions", "ALL", 0);
    if ((ret == avformat_open_input(&ifmt_ctx, in_filename, 0, &option)) < 0) {
        LOGE("Could not open input file '%s'", in_filename);
        avformat_close_input(&ifmt_ctx);
        LOGE("avformat_open_input failed, ret=%d", ret);
        return NULL;
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, &option)) < 0) {
        LOGE("Failed to retrieve input stream information");
        avformat_close_input(&ifmt_ctx);
        LOGE("avformat_find_stream_info failed, ret=%d", ret);
        return NULL;
    }

    duration = ifmt_ctx->duration;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            width = in_codecpar->width;
            height = in_codecpar->height;
            video_id = in_codecpar->codec_id;
        } else if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_index = i;
            audio_id = in_codecpar->codec_id;
        }
    }

    if (video_index == -1) {
        LOGE("Cannot find the video track index");
        return NULL;
    }

    if (audio_index == -1) {
        LOGE("Cannot find the audio track index");
        return NULL;
    }

    video_codec = avcodec_find_decoder(video_id);
    audio_codec = avcodec_find_decoder(audio_id);

    jclass objClass = env->FindClass("com/jeffmony/ffmpeglib/model/VideoInfo");
    jmethodID mid = env->GetMethodID(objClass, "<init>", "()V");
    jobject constObj = env->NewObject(objClass, mid);

    mid = env->GetMethodID(objClass, "setDuration", "(J)V");
    env->CallVoidMethod(constObj, mid, duration);

    mid = env->GetMethodID(objClass, "setWidth", "(I)V");
    env->CallVoidMethod(constObj, mid, width);

    mid = env->GetMethodID(objClass, "setHeight", "(I)V");
    env->CallVoidMethod(constObj, mid, height);


    mid = env->GetMethodID(objClass, "setVideoFormat", "(Ljava/lang/String;)V");
    if (video_codec && video_codec->name) {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(video_codec->name));
    } else {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(""));
    }

    mid = env->GetMethodID(objClass, "setAudioFormat", "(Ljava/lang/String;)V");
    if (audio_codec && audio_codec->name) {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(audio_codec->name));
    } else {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(""));
    }

    mid = env->GetMethodID(objClass, "setContainerFormat", "(Ljava/lang/String;)V");
    if (ifmt_ctx->iformat && ifmt_ctx->iformat->name) {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(ifmt_ctx->iformat->name));
    } else {
        env->CallVoidMethod(constObj, mid, env->NewStringUTF(""));
    }

    avformat_close_input(&ifmt_ctx);
    av_dict_free(&option);
    return constObj;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_jeffmony_ffmpeglib_FFmpegVideoUtils_cutVideo(JNIEnv *env, jclass clazz, jdouble start,
                                                      jdouble end, jstring input_path,
                                                      jstring output_path) {
    if (use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }
    const char *in_filename = env->GetStringUTFChars(input_path, 0);
    const char *out_filename = env->GetStringUTFChars(output_path, 0);
    double start_s = start;
    double end_s = end;

    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVOutputFormat *ofmt = NULL;
    AVPacket pkt;

    int ret;
    int i;

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        LOGE("Cannot open input file");
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        LOGE("Cannot find input file stream info");
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);

    if (!ofmt_ctx) {
        LOGE("Cannot alloc output file ctx");
        avformat_close_input(&ifmt_ctx);
        return AVERROR_UNKNOWN;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream");
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            return AVERROR_UNKNOWN;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) {
            LOGE("Failed to copy context from input to output stream codec context");
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            return ret;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not open output file '%s'", out_filename);
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            return ret;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOGE("Error occurred when opening output file");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        return ret;
    }

    ret = av_seek_frame(ifmt_ctx, -1, start_s * AV_TIME_BASE, AVSEEK_FLAG_ANY);
    if (ret < 0) {
        LOGE("Error seek");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        return ret;
    }

    int64_t *dts_start_from = static_cast<int64_t *>(malloc(
            sizeof(int64_t) * ifmt_ctx->nb_streams));
    memset(dts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);
    int64_t *pts_start_from = static_cast<int64_t *>(malloc(
            sizeof(int64_t) * ifmt_ctx->nb_streams));
    memset(pts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);


    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            avformat_close_input(&ifmt_ctx);

            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);

            break;
        }
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        if (av_q2d(in_stream->time_base) * pkt.pts > end_s) {
            av_packet_unref(&pkt);
            break;
        }

        if (dts_start_from[pkt.stream_index] == 0) {
            dts_start_from[pkt.stream_index] = pkt.dts;
            LOGE("dts_start_from: %s\n", av_ts2str(dts_start_from[pkt.stream_index]));
        }
        if (pts_start_from[pkt.stream_index] == 0) {
            pts_start_from[pkt.stream_index] = pkt.pts;
            LOGE("pts_start_from: %s\n", av_ts2str(pts_start_from[pkt.stream_index]));
        }

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts - pts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts - dts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        if (pkt.pts < 0) {
            pkt.pts = 0;
        }
        if (pkt.dts < 0) {
            pkt.dts = 0;
        }
        pkt.duration = (int)av_rescale_q((int64_t)pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            LOGE("Error muxing packet");
            avformat_close_input(&ifmt_ctx);

            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            break;
        }
        av_packet_unref(&pkt);
    }
    free(dts_start_from);
    free(pts_start_from);

    av_write_trailer(ofmt_ctx);

    avformat_close_input(&ifmt_ctx);

    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    return 1;
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_jeffmony_ffmpeglib_FFmpegVideoUtils_transformVideo(JNIEnv *env, jclass clazz, jstring input_path,
                                                            jstring output_path) {
    if (use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }
    const char *in_filename = env->GetStringUTFChars(input_path, 0);
    const char *out_filename = env->GetStringUTFChars(output_path, 0);
    LOGI("Input_path=%s, Output_path=%s", in_filename, out_filename);
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size;
    int width = 0, height = 0;
    int64_t last_dts = 0;

    AVDictionary *options = NULL;
    av_dict_set(&options, PROTOCOL_OPTION_KEY, PROTOCOL_OPTION_VALUE, 0);
    av_dict_set(&options, FORMAT_EXTENSION_KEY, FORMAT_EXTENSION_VALUE, 0);
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &options)) < 0) {
        LOGE("Could not open input file '%s'", in_filename);
        LOGE("Error occurred: %s\n", av_err2str(ret));
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, &options)) < 0) {
        LOGE("Failed to retrieve input stream information");
        LOGE("Error occurred: %s\n", av_err2str(ret));
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    av_dump_format(ifmt_ctx, 1, in_filename, 0);
    ifmt_ctx->iformat->flags |= AVFMT_NODIMENSIONS;

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        LOGE("Could not create output context\n");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        return ERR_ALLOC_OUTPUT_CTX;
    }
    LOGI("Output format=%s", ofmt_ctx->oformat->name);

    ofmt_ctx->oformat->flags |= AVFMT_TS_NONSTRICT;
    ofmt_ctx->oformat->flags |= AVFMT_NODIMENSIONS;

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = (int *) av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        LOGE("Could not alloc stream mapping\n");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        av_freep(&stream_mapping);
        return ERR_MALLOC_MAPPING;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            width = in_codecpar->width;
            height = in_codecpar->height;

            if (width == 0 && height == 0) {
                LOGE("Input file has no width or height\n");
                avformat_close_input(&ifmt_ctx);
                avformat_free_context(ofmt_ctx);
                av_freep(&stream_mapping);
                return ERR_DIMENSIONS_NOT_SET;
            }
        }

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ERR_CREATE_NEW_STREAM;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            LOGE("Failed to copy codec parameters\n");
            LOGE("Error occurred: %s\n", av_err2str(ret));
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
        out_stream->codecpar->codec_tag = 0;
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        LOGI("Open output file");
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not  '%s'", out_filename);
            LOGE("Error occurred: %s\n, width=%d, height=%d", av_err2str(ret), width, height);
            avformat_close_input(&ifmt_ctx);
            /* close output */
            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
    }

    ret = avformat_write_header(ofmt_ctx, &options);
    if (ret < 0) {
        LOGE("Error occurred when opening output file, ret=%d\n", ret);
        LOGE("Error occurred: %s\n, width=%d, height=%d", av_err2str(ret), width, height);
        avformat_close_input(&ifmt_ctx);
        /* close output */
        if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        av_freep(&stream_mapping);
        return ret;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        if (pkt.pts == AV_NOPTS_VALUE) {
            if (pkt.dts != AV_NOPTS_VALUE) {
                pkt.pts = pkt.dts;
                last_dts = pkt.dts;
            } else {
                pkt.pts = last_dts + 1;
                pkt.dts = pkt.pts;
                last_dts = pkt.pts;
            }
        } else {
            if (pkt.dts != AV_NOPTS_VALUE) {
                last_dts = pkt.dts;
            } else {
                pkt.dts = pkt.pts;
                last_dts = pkt.dts;
            }
        }

        if (pkt.pts < pkt.dts) {
            pkt.pts = pkt.dts;
        }

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            LOGE("Error muxing packet\n");
            avformat_close_input(&ifmt_ctx);
            /* close output */
            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);

    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    av_freep(&stream_mapping);
    av_dict_free(&options);
    return 1;
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_jeffmony_ffmpeglib_FFmpegVideoUtils_transformVideoWithDimensions(JNIEnv *env, jclass clazz,
                                                                          jstring input_path,
                                                                          jstring output_path,
                                                                          jint width, jint height) {
    if (use_log_report) {
        av_log_set_callback(ffp_log_callback_report);
    } else {
        av_log_set_callback(ffp_log_callback_brief);
    }
    const char *in_filename = env->GetStringUTFChars(input_path, 0);
    const char *out_filename = env->GetStringUTFChars(output_path, 0);
    LOGI("Input_path=%s, Output_path=%s", in_filename, out_filename);
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size;

    AVDictionary *options = NULL;
    av_dict_set(&options, PROTOCOL_OPTION_KEY, PROTOCOL_OPTION_VALUE, 0);
    av_dict_set(&options, FORMAT_EXTENSION_KEY, FORMAT_EXTENSION_VALUE, 0);
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, &options)) < 0) {
        LOGE("Could not open input file '%s'", in_filename);
        LOGE("Error occurred: %s\n", av_err2str(ret));
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, &options)) < 0) {
        LOGE("Failed to retrieve input stream information");
        LOGE("Error occurred: %s\n", av_err2str(ret));
        avformat_close_input(&ifmt_ctx);
        return ret;
    }

    av_dump_format(ifmt_ctx, 1, in_filename, 0);

    ifmt_ctx->iformat->flags |= AVFMT_NODIMENSIONS;

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        LOGE("Could not create output context\n");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        return ERR_ALLOC_OUTPUT_CTX;
    }
    LOGI("Output format=%s", ofmt_ctx->oformat->name);

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = (int *) av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        LOGE("Could not alloc stream mapping\n");
        avformat_close_input(&ifmt_ctx);
        avformat_free_context(ofmt_ctx);
        av_freep(&stream_mapping);
        return ERR_MALLOC_MAPPING;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ERR_CREATE_NEW_STREAM;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            LOGE("Failed to copy codec parameters\n");
            LOGE("Error occurred: %s\n", av_err2str(ret));
            avformat_close_input(&ifmt_ctx);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
        out_stream->codecpar->codec_tag = 0;
        if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            out_stream->codecpar->width = width;
            out_stream->codecpar->height = height;
            in_stream->codecpar->width = width;
            in_stream->codecpar->height = height;
        }
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        LOGI("Open output file");
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not open output file '%s'", out_filename);
            LOGE("Error occurred: %s\n", av_err2str(ret));
            avformat_close_input(&ifmt_ctx);
            /* close output */
            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
    }

    ret = avformat_write_header(ofmt_ctx, &options);
    if (ret < 0) {
        LOGE("Error occurred when opening output file, ret=%d\n", ret);
        LOGE("Error occurred: %s\n", av_err2str(ret));
        avformat_close_input(&ifmt_ctx);
        /* close output */
        if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        av_freep(&stream_mapping);
        return ret;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        if (pkt.pts == AV_NOPTS_VALUE) {
            pkt.pts = pkt.dts;
        }
        if (pkt.dts == AV_NOPTS_VALUE) {
            pkt.dts = pkt.pts;
        }

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            LOGE("Error muxing packet\n");
            avformat_close_input(&ifmt_ctx);
            /* close output */
            if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            av_freep(&stream_mapping);
            return ret;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);

    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    av_freep(&stream_mapping);
    av_dict_free(&options);
    return 1;
}