#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <png.h>
#include <dirent.h>
#include "ws2811.h"

#define LEDS_CHANNEL_1  1440
#define LEDS_CHANNEL_2  1440
#define LEDS_LENGTH     (LEDS_CHANNEL_1 + LEDS_CHANNEL_2)

#define LEDS_BRIGHTNESS 10

ws2811_t ledstring = {
    .freq = WS2811_TARGET_FREQ,
    .dmanum = 10,
    .channel = {
        [0] = {
            .gpionum = 18,
            .count = LEDS_CHANNEL_1,
            .invert = 0,
            .brightness = LEDS_BRIGHTNESS,
            .strip_type = WS2811_STRIP_RGB,
        },
        [1] = {
            .gpionum = 13,
            .count = LEDS_CHANNEL_2,
            .invert = 0,
            .brightness = LEDS_BRIGHTNESS,
            .strip_type = WS2811_STRIP_RGB,
        },
    },
};

typedef struct frame_t {
    uint32_t **pixels;
    size_t width;
    size_t height;

} frame_t;

typedef struct frames_t {
    size_t frames;
    struct timeval pf;
    struct timeval init;
    frame_t *frame;
    size_t line;
    size_t lines;
    int fps;

} frames_t;

/*
typedef struct frameslist_t {
    frame_t **frames;
    size_t length;

} frameslist_t;
*/

void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

void *dieptr(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

void *imgerr(char *str) {
    fprintf(stderr, "image: %s\n", str);
    return NULL;
}

void swap(uint32_t *x, uint32_t *y) {
    long tmp = *x;
    *x = *y;
    *y = tmp;
}

/*
void transposeMatrix(uint32_t matrix[][TILE_WIDTH]) {
   for (int r = 0; r < TILE_WIDTH; ++ r) {
      for (int c = 0; c < r; ++ c) {
         swap(&matrix[r][c], &matrix[c][r]);
      }
   }
}

void rotateAntiClockwise(uint32_t matrix[][TILE_WIDTH]) {
   transposeMatrix(matrix);

   for (int r = 0; r < TILE_WIDTH / 2; ++ r) {
      for (int c = 0; c < TILE_WIDTH; ++ c) {
         swap(&matrix[r][c], &matrix[TILE_WIDTH - r - 1][c]);
      }
   }
}
*/

static frame_t *loadframe(frames_t *frames, char *imgfile) {
    png_structp ctx;
    png_infop info;
    frame_t *frame = NULL;
    FILE *fp;

    unsigned char header[8];    // 8 is the maximum size that can be checked

    printf("[+] loading image\n");

    if(!(fp = fopen(imgfile, "r")))
        dieptr(imgfile);

    if(fread(header, 1, 8, fp) != 8)
        diep("fread");

    if(png_sig_cmp(header, 0, 8))
        return imgerr("unknown file signature (not a png image)");

    if(!(ctx = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
        return imgerr("cannot create png struct");

    if(!(info = png_create_info_struct(ctx)))
        return imgerr("cannot create info struct");

    png_init_io(ctx, fp);
    png_set_sig_bytes(ctx, 8);
    png_read_info(ctx, info);

    int width = png_get_image_width(ctx, info);
    int height = png_get_image_height(ctx, info);

    png_bytep *lines = (png_bytep *) malloc(sizeof(png_bytep) * height);
    for(int y = 0; y < height; y++)
        lines[y] = (png_byte *) malloc(png_get_rowbytes(ctx, info));

    png_read_image(ctx, lines);

    fclose(fp);

    if(png_get_color_type(ctx, info) == PNG_COLOR_TYPE_RGB)
        return imgerr("alpha channel required");

    if(png_get_color_type(ctx, info) != PNG_COLOR_TYPE_RGBA)
        return imgerr("only RGBA supported for now");


    // allocate frame
    if(!(frame = malloc(sizeof(frame_t))))
        diep("malloc");

    frames->lines = height;

    frame->width = width;
    frame->height = height;

    if(!(frame->pixels = (uint32_t **) malloc(sizeof(uint32_t *) * frame->height)))
        diep("malloc");

    for(size_t i = 0; i < frame->height; i++) {
        if(!(frame->pixels[i] = (uint32_t *) malloc(sizeof(uint32_t) * frame->width)))
            diep("malloc");
    }

    printf("[+] loading lines ");

    for(int y = 0; y < height; y++) {
        png_byte *row = lines[y];
        printf(".");
        fflush(stdout);

        for(int x = 0; x < width; x++) {
            png_byte *ptr = &(row[x * 4]);
            uint32_t pixel = 0 | ptr[0] << 16 | ptr[1] << 8 | ptr[2];
            frame->pixels[y][x] = pixel;
        }
    }

    printf("\n");
    frames->frame = frame;

    return frame;
}


float difftv(struct timeval *begin, struct timeval *end) {
    return (begin->tv_sec + begin->tv_usec / 1000000.0) - (end->tv_sec + end->tv_usec / 1000000.0);
}

int newframe(frames_t *f) {
    struct timeval now;
    float dt;

    f->frames += 1;

    gettimeofday(&now, NULL);
    dt = difftv(&now, &f->pf);

    f->fps = 1 / dt;
    f->pf = now;

    return 0;
}

void render() {
    int ret;

    if((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
        exit(EXIT_FAILURE);
    }
}

void render_frame(frames_t *frames) {
    newframe(frames);

    for(int i = 0; i < LEDS_LENGTH; i++) {
        int channel = (i < LEDS_CHANNEL_1) ? 0 : 1;
        int index = (i < LEDS_CHANNEL_1) ? i : i - LEDS_CHANNEL_1;

        // 0xWWGGRRBB

        // printf("TILE %04d = [%d, %d](%d, %d) = %d\n", i, tileid, tilepixel, tileline, tilecol, pixel);
        // // ledstring.channel[channel].leds[index] = frame->pixels[pixel];
        ledstring.channel[channel].leds[index] = frames->frame->pixels[frames->line][i];
        // ledstring.channel[channel].leds[index] = 0xFF000000 | red << 16;
        // ledstring.channel[0].leds[i] = frame->pixels[pixel];
    }

    printf("\r[+] applying frame %lu [%d fps] \033[K", frames->frames, frames->fps);
    fflush(stdout);
    render();

    // usleep(80000);
}

int main(int argc, char *argv[]) {
    int ret;
    frames_t frames;
    char *framespath = "/root/images/frames-black";

    printf("[+] initializing magickey led stage server\n");

    printf("[+] channel 1: gpio %d, length: %d\n", ledstring.channel[0].gpionum, LEDS_CHANNEL_1);
    printf("[+] channel 1: gpio %d, length: %d\n", ledstring.channel[1].gpionum, LEDS_CHANNEL_2);
    printf("[+] total led: %d\n", LEDS_LENGTH);

    if(argc > 1)
        framespath = argv[1];

    printf("[+] rendering path: %s\n", framespath);

    if((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "[-] ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        exit(EXIT_FAILURE);
    }

    memset(&frames, 0, sizeof(frames_t));
    gettimeofday(&frames.init, NULL);
    frames.pf = frames.init;

    char filepath[256];
    sprintf(filepath, "%s/frame.png", framespath);

    loadframe(&frames, filepath);

    while(1) {
        for(frames.line = 0; frames.line < frames.lines; frames.line += 1)
            render_frame(&frames);

        for(frames.line = frames.lines - 1; frames.line > 0; frames.line -= 1)
            render_frame(&frames);
    }

    ws2811_fini(&ledstring);

    return 0;
}
