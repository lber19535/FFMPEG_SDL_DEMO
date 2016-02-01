// SDL_FFMPEG_DEMO.cpp : Defines the entry point for the console application.
// The lib version:
// SDL 2.0.4 avcodec-57 avdevice-57 avfilter-6 avformat-57 avutil-55 postproc-54 swresample-2 swscale-4

#include "stdafx.h"
#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "SDL.h"
#undef main
#include "SDL_thread.h"
}


// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

SDL_Window *window = nullptr;
SDL_Renderer *renderer;
SDL_Texture     *texture = nullptr;
struct SwsContext *sws_ctx = nullptr;

int play_on_sdl(AVCodecContext *pCodecCtx, AVFrame *frame) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        std::cout << "could not init sdl" << SDL_GetError() << std::endl;
    }

    if (window == nullptr) {
        window = SDL_CreateWindow("My Video Window",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            pCodecCtx->width,
            pCodecCtx->height,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

        texture = SDL_CreateTexture(renderer,
            SDL_PIXELFORMAT_YV12,
            SDL_TEXTUREACCESS_TARGET,
            pCodecCtx->width,
            pCodecCtx->height);

        sws_ctx = sws_getContext(pCodecCtx->width,
            pCodecCtx->height,
            pCodecCtx->pix_fmt,
            pCodecCtx->width,
            pCodecCtx->height,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR,
            NULL,
            NULL,
            NULL
            );

        std::cout << "SDL: could not set video mode - exiting\n" << std::endl;
        //exit(1);
    }

    AVFrame *pFrameYUV = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,
        pCodecCtx->height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
        pCodecCtx->width, pCodecCtx->height, 1);

    sws_scale(sws_ctx, frame->data,
        frame->linesize, 0, pCodecCtx->height,
        pFrameYUV->data, pFrameYUV->linesize);
    
    SDL_UpdateYUVTexture(texture, 
        nullptr, 
        pFrameYUV->data[0], 
        pFrameYUV->linesize[0], 
        pFrameYUV->data[1], 
        pFrameYUV->linesize[1], 
        pFrameYUV->data[2],
        pFrameYUV->linesize[2]);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    return 0;
}

SDL_Event event;
bool quit;
bool play;

void handle_input() {

    if (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYUP:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                quit = true;
                std::cout << "exit" << std::endl;
                break;
            case SDLK_SPACE:
                play = !play;
                std::cout << "play" << std::endl;
                break;
            default:
                break;
            }
            break;
        case SDL_QUIT:
            quit = true;
            break;
        default:
            break;
        }
    }
}

int main()
{
    AVFormatContext   *pFormatCtx = nullptr;
    int               i, videoStream;
    AVCodecContext    *pCodecCtxOrig = nullptr;
    AVCodecContext    *pCodecCtx = nullptr;
    AVCodec           *pCodec = nullptr;
    AVFrame           *pFrame = nullptr;
    AVFrame           *pFrameRGB = nullptr;
    AVPacket          packet;
    int               frameFinished;
    int               numBytes;
    uint8_t           *buffer = nullptr;
    struct SwsContext *sws_ctx = nullptr;

    // Register all formats and codecs
    av_register_all();

    // Open video file
    char *filename = "C://Users//bill_lv//Desktop//抽象编程//VID_20160127_162206.mp4";
    if (avformat_open_input(&pFormatCtx, filename, nullptr, nullptr) != 0)
        return -1; // Couldn't open file

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
        return -1; // Couldn't find stream information

    av_dump_format(pFormatCtx, 0, filename, 0);

    // Find the first video stream
    videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    if (videoStream == -1)
        return -1; // Didn't find a video stream

    // Get a pointer to the codec context for the video stream
    pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if (pCodec == nullptr) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1; // Codec not found
    }
    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }

    // Open codec
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
        return -1; // Could not open codec

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Allocate an AVFrame structure
    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == nullptr)
        return -1;

    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width,
        pCodecCtx->height,1);
    buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,AV_PIX_FMT_RGB24,
        pCodecCtx->width, pCodecCtx->height,1);

    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
        );

    // Read frames and save first five frames to disk
    i = 0;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        handle_input();
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // Did we get a video frame?
            if (frameFinished) {
                // Convert the image from its native format to RGB
                sws_scale(sws_ctx, pFrame->data,
                    pFrame->linesize, 0, pCodecCtx->height,
                    pFrameRGB->data, pFrameRGB->linesize);

                // Save the frame to disk
                if (++i <= 10){
                    play_on_sdl(pCodecCtx, pFrame);
                }
                else
                {
                    break;
                }
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
    }

    // Free the RGB image
    av_free(buffer);
    av_frame_free(&pFrameRGB);

    // Free the YUV frame
    av_frame_free(&pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    getchar();
    return 0;
}
