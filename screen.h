/*
 * File name: prgsem.c
 * Date:      2022/05/7 6:03 PM
 * Author:    Aleksandr Zelik
 * Tusk text: https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start
*/

#ifndef __SCREEN_H__
 
#include "xwin_sdl.h"
#include <stdbool.h>
#include <stdint.h>
#define __SCREEN_H__
 
#define RGB_MAX 255
#define PIXEL_SIZE sizeof(Pixel)
#define IMAGE_SIZE sizeof(Image)
#define RGB_PIXEL 3
 
typedef struct {
    uint8_t R;
    uint8_t G; 
    uint8_t B;
} Pixel;
/*
typedef struct{
    uint8_t width, height, chunk_id;
    pixel *pixels;
} chunk;*/
 
typedef struct {
    int width;  // image width
    int height; // image height
    Pixel* pixels;
    uint8_t chunk_size;
    uint8_t chunks_in_row;
    uint8_t chunks_in_col;
    uint8_t q_chunks;
    bool refresh_required;
} Image;
 
void __SavePNGImage__(Image *image);
 
Image* __InitImage__(int width, int height, int chunk_size);
 
Image* __SetBlackScreen__(Image *image);

void __FreeImage__(Image* image);
void __RepaintScreen__(Image* image);
void __CloseImage__();
 
#endif