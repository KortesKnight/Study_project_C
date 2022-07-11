/*
 * File name: prgsem.c
 * Date:      2022/05/7 6:03 PM
 * Author:    Aleksandr Zelik
 * Tusk text: https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <png.h>
 
#include "screen.h"
#include "xwin_sdl.h"
 
void __SavePNGImage__ (Image *img) {
    FILE * fd = fopen("fracktal_screen.png","wb");
    if (!fd) exit(1);
 
 
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    size_t x, y;
    png_byte ** row_pointers = NULL;
    int depth = 8;
     
 
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) goto png_create_write_struct_failed;
 
    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL) goto png_create_info_struct_failed;
     
    if (setjmp (png_jmpbuf (png_ptr)))goto png_failure;
    
 
    //image attributes
    png_set_IHDR (png_ptr,
                  info_ptr,
                  img->width,
                  img->height,
                  depth,
                  PNG_COLOR_TYPE_RGB,
                  PNG_INTERLACE_NONE,
                  PNG_COMPRESSION_TYPE_DEFAULT,
                  PNG_FILTER_TYPE_DEFAULT);
     
    // Initialize rows of PNG. 
 
    row_pointers = png_malloc (png_ptr, img->height * sizeof (png_byte *));
    for (y = 0; y < img->height; y++) {
        png_byte *row = png_malloc (png_ptr, img->width * PIXEL_SIZE);
        row_pointers[y] = row;
        for (x = 0; x < img->width; x++) {
            Pixel pixel = img->pixels[img->width *y +x];
            *row++ = pixel.R;
            *row++ = pixel.G;
            *row++ = pixel.B;
        }
    }
     
    // Write the image data to "fp". 
 
    png_init_io (png_ptr, fd);
    png_set_rows (png_ptr, info_ptr, row_pointers);
    png_write_png (png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
 
    // The routine has successfully written the file, so we set "status" to a value which indicates success.
 
     
    for (y = 0; y < img->height; y++) {
        png_free (png_ptr, row_pointers[y]);
    }
    png_free (png_ptr, row_pointers);
     
    png_failure:
    png_create_info_struct_failed:
        png_destroy_write_struct (&png_ptr, &info_ptr);
    png_create_write_struct_failed:
        fclose (fd);
}

Image* __InitImage__(int width, int height, int chunk_size) {
 
    Pixel* pixels_of_img = (Pixel*) malloc(width * height * PIXEL_SIZE);

    Image* img = (Image*) malloc(IMAGE_SIZE);
    
    // calculate how many chunks we will need in each row
    if (width % chunk_size == 0) {
        img->chunks_in_row = (uint8_t)(width / chunk_size);
    } else {
        img->chunks_in_row = (uint8_t)((width / chunk_size) + 1);
    }
    
    // calculate how many chunks we will need in each column
    if (height / chunk_size == 0) {
        img->chunks_in_col = (uint8_t)(height / chunk_size);
    } else {
        img->chunks_in_col = (uint8_t)((height / chunk_size) + 1);
    }
    
    // calculate how many chunks we will need to have
    img->q_chunks = img->chunks_in_row * img->chunks_in_col;

    // inicializate others parameters
    img->chunk_size = chunk_size;
    img->height = height;
    img->width = width;
    img->pixels = pixels_of_img;
    img->refresh_required = false;

    // inicilizate window
    xwin_init(img->width, img->height);
    
    // fill image buffer by black pixels
    img = __SetBlackScreen__(img);

    return img;
}
 
Image* __SetBlackScreen__(Image *image) {

    // fill image buffer by black pixels
    for (int index_pixel = 0; index_pixel < image->width * image->height; index_pixel++) {
        image->pixels[index_pixel] = (Pixel) { .R = 0, .G = 0, .B = 0};
    }

    // redraw image in the window
    xwin_redraw(image->width, image->height, (unsigned char*) image->pixels);
    return image;
}

void __FreeImage__(Image* img) {
    free(img->pixels);
    free(img);
}

void __RepaintScreen__(Image* image) {
    xwin_redraw(image->width, image->height, (unsigned char*) image->pixels);
}
 
void __CloseImage__() {
    xwin_close();
}