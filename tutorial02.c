// tutorial02.c
// A pedagogical video player that will stream through every video frame as fast as it can.
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
//
// Use
// 
// gcc -o tutorial02 tutorial02.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial02 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  unsigned char   *out_buffer;
  AVFrame         *pFrameYUV = NULL;
  AVPacket        *packet;
  int             frameFinished;
  float           aspect_ratio;
  struct SwsContext *sws_ctx = NULL;
  int screen_w=0,screen_h=0;
  SDL_Texture     *texture;
  SDL_Window      *screen;
  SDL_Renderer    *renderer;
  SDL_Rect        rect;
  SDL_Event       event;

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Open video file
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; // Couldn't open file
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);
  
  // Find the first video stream
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; // Didn't find a video stream
  
  // Get a pointer to the codec context for the video stream
  pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;
  // Find the decoder for the video stream
  pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }

  // Copy context
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }

  // Open codec
  if(avcodec_open2(pCodecCtx, pCodec, NULL)<0)
    return -1; // Could not open codec
  
  // Allocate video frame
  pFrame=av_frame_alloc();
  pFrameYUV = av_frame_alloc();
  out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  pCodecCtx->width, pCodecCtx->height,1));
  av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
                         AV_PIX_FMT_YUV420P,pCodecCtx->width, pCodecCtx->height,1);
  packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    
    
  screen_w = pCodecCtx->width;
  screen_h = pCodecCtx->height;
  //SDL 2.0 Support for multiple windows
  screen = SDL_CreateWindow("视频播放器", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if(!screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
  }
    
  // Allocate a place to put our YUV image on that screen
  renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) < 0)
        return -1;
    rect.x=0;
    rect.y=0;
    rect.w=screen_w;
    rect.h=screen_h;

  // initialize SWS context for software scaling
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



  // Read frames and save first five frames to disk
  i=0;
  while(av_read_frame(pFormatCtx, packet)>=0) {
    // Is this a packet from the video stream?
    if(packet->stream_index==videoStream) {
      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);
      
      // Did we get a video frame?
      if(frameFinished) {
//	SDL_LockYUVOverlay(bmp);


	// Convert the image into YUV format that SDL uses
    sws_scale(sws_ctx, (const unsigned char* const*)pFrame->data,
              pFrame->linesize,
              0,
              pCodecCtx->height,
              pFrameYUV->data,
              pFrameYUV->linesize
              );
#if OUTPUT_YUV420P
    y_size=pCodecCtx->width*pCodecCtx->height;
    fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y
    fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
    fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
#endif
          
#if 0
    SDL_UpdateTexture( bmp, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
#else
    SDL_UpdateYUVTexture(texture, &rect,
                pFrameYUV->data[0],
                pFrameYUV->linesize[0],
                pFrameYUV->data[1],
                pFrameYUV->linesize[1],
                pFrameYUV->data[2],
                pFrameYUV->linesize[2]);
#endif
    SDL_RenderClear( renderer );
    SDL_RenderCopy( renderer, texture,  NULL, &rect);
    SDL_RenderPresent( renderer );
    SDL_Delay(40);

      }
    }
      
    
    // Free the packet that was allocated by av_read_frame
    av_free_packet(packet);
    SDL_PollEvent(&event);
    switch(event.type) {
    case SDL_QUIT:
      SDL_Quit();
      exit(0);
      break;
    default:
      break;
    }

  }
  
  // Free the YUV frame
  av_frame_free(&pFrame);
  
  // Close the codec
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);
  
  // Close the video file
  avformat_close_input(&pFormatCtx);
  
  return 0;
}
