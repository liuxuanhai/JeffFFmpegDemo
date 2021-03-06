package com.jeffmony.ffmpeglib;

import com.jeffmony.ffmpeglib.model.VideoInfo;

public class FFmpegVideoUtils {

    static {
        System.loadLibrary("jeffmony");
        System.loadLibrary("avcodec");
        System.loadLibrary("avfilter");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("postproc");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
    }

    public static native void printVideoInfo(String srcPath);

    public static native VideoInfo getVideoInfo(String inputPath);

    public static native int transformVideo(String inputPath, String outputPath);

    public static native int transformVideoWithDimensions(String inputPath, String outputPath, int width, int height);

    public static native int cutVideo(double start, double end, String inputPath, String outputPath);
}
