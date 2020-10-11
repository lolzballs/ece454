#pragma GCC target ("avx2")

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "utilities.h"  // DO NOT REMOVE this line
#include "implementation_reference.h"   // DO NOT REMOVE this line

// I added these
#include <assert.h>
#include <sys/mman.h>
#include <x86intrin.h>


unsigned char *processMoveUp(unsigned char *buffer_frame, unsigned width, unsigned height, int offset);
unsigned char *processMoveDown(unsigned char *buffer_frame, unsigned width, unsigned height, int offset);
unsigned char *processMoveLeft(unsigned char *buffer_frame, unsigned width, unsigned height, int offset);
unsigned char *processMoveRight(unsigned char *buffer_frame, unsigned width, unsigned height, int offset);
unsigned char *processRotateCW(unsigned char *buffer_frame, unsigned width, unsigned height, int rotate_iteration);
unsigned char *processRotateCCW(unsigned char *buffer_frame, unsigned width, unsigned height, int rotate_iteration);
unsigned char *processMirrorX(register unsigned char *buffer_frame, unsigned int width, unsigned int height, int _unused);
unsigned char *processMirrorY(register unsigned char *buffer_frame, unsigned int width, unsigned int height, int _unused);

uint8_t temporary_buffer1[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));
uint8_t temporary_buffer2[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));

static inline uint8_t* acquire_temporary_buffer(uint8_t *frame_buffer) {
    if (frame_buffer == temporary_buffer1) {
        return temporary_buffer2;
    } else if (frame_buffer == temporary_buffer2) {
        return temporary_buffer1;
    }
    return temporary_buffer1;
}

static inline void frame_copy_unaligned(register uint8_t *src, register uint8_t *dst, register size_t size) {
    memcpy(dst, src, size);
    return;
    register size_t offset = 0;

    // Copy byte by byte up until the 32-byte alignment
    register uint8_t *src_bytes = (uint8_t*) (src);
    register uint8_t *dst_bytes = (uint8_t*) (dst);
    while ((size_t) src_bytes % 32 != 0) {
        *dst_bytes = *src_bytes;

        offset += sizeof(uint8_t);
        src_bytes += 1;
        dst_bytes += 1;
    }

    // Copy all 256 bits possible
    register __m256i *src_vect = (__m256i*) (src + offset);
    register __m256i *dst_vect = (__m256i*) (dst + offset);
    offset += sizeof(__m256i);
    while (offset <= size) { // equality occurs when there are exactly 32 bytes left to copy
        asm (
                ".intel_syntax noprefix;"
                "vmovdqa ymm0, [%0];"
                "vmovdqu [%1], ymm0;"
                ".att_syntax;"
                :
                : "r" (src_vect), "r" (dst_vect)
                : "memory", "ymm0"
            );

        offset += sizeof(__m256i);
        src_vect += 1;
        dst_vect += 1;
    }
    // offset will be 32 bytes ahead of the last copied value
    offset -= sizeof(__m256i);
    
    // Copy as many dwords as possible
    register uint32_t *src_dwords = (uint32_t*) (src + offset);
    register uint32_t *dst_dwords = (uint32_t*) (dst + offset);
    offset += sizeof(uint32_t);
    while (offset <= size) { // equality occurs when there are exactly 4 bytes left to copy
        *dst_dwords = *src_dwords;

        offset += sizeof(uint32_t);
        src_dwords += 1;
        dst_dwords += 1;
    }
    offset -= sizeof(uint32_t);

    // Copy remaining bytes
    src_bytes = (uint8_t*) (src + offset);
    dst_bytes = (uint8_t*) (dst + offset);
    while (offset < size) {
        *dst_bytes = *src_bytes;

        offset += sizeof(uint8_t);
        src_bytes += 1;
        dst_bytes += 1;
    }

    // assert(offset == size);
}


// src, dst mod 32 must be equal
static inline void frame_copy(register uint8_t *src, register uint8_t *dst, register size_t size) {
    register size_t offset = 0;

    // assert((size_t) src % 32 == (size_t) dst % 32);

    // Copy byte by byte up until the 32-byte alignment
    register uint8_t *src_bytes = (uint8_t*) (src);
    register uint8_t *dst_bytes = (uint8_t*) (dst);
    while ((size_t) src_bytes % 32 != 0) {
        *dst_bytes = *src_bytes;

        offset += sizeof(uint8_t);
        src_bytes += 1;
        dst_bytes += 1;
    }

    // Copy all 256 bits possible
    register __m256i *src_vect = (__m256i*) (src + offset);
    register __m256i *dst_vect = (__m256i*) (dst + offset);
    offset += sizeof(__m256i);
    while (offset <= size) { // equality occurs when there are exactly 32 bytes left to copy
        asm (
                ".intel_syntax noprefix;"
                "vmovdqa ymm0, [%0];"
                "vmovntdq [%1], ymm0;"
                ".att_syntax;"
                :
                : "r" (src_vect), "r" (dst_vect)
                : "memory", "ymm0"
            );
        // _mm256_stream_si256(dst_vect, *src_vect);

        offset += sizeof(__m256i);
        src_vect += 1;
        dst_vect += 1;
    }
    // offset will be 32 bytes ahead of the last copied value
    offset -= sizeof(__m256i);
    
    // Copy as many dwords as possible
    register uint32_t *src_dwords = (uint32_t*) (src + offset);
    register uint32_t *dst_dwords = (uint32_t*) (dst + offset);
    offset += sizeof(uint32_t);
    while (offset <= size) { // equality occurs when there are exactly 4 bytes left to copy
        *dst_dwords = *src_dwords;

        offset += sizeof(uint32_t);
        src_dwords += 1;
        dst_dwords += 1;
    }
    offset -= sizeof(uint32_t);

    // Copy remaining bytes
    src_bytes = (uint8_t*) (src + offset);
    dst_bytes = (uint8_t*) (dst + offset);
    while (offset < size) {
        *dst_bytes = *src_bytes;

        offset += sizeof(uint8_t);
        src_bytes += 1;
        dst_bytes += 1;
    }

    // assert(offset == size);
}

static inline void frame_clear(register uint8_t *dst, register size_t size) {
    register size_t offset;

    register __m256i *dst_vect = (__m256i*) dst;
    register __m256i val_vect = _mm256_set1_epi64x(0xFFFFFFFFFFFFFFFF);
    offset = sizeof(__m256i);
    while (offset <= size) {
        _mm256_storeu_si256(dst_vect, val_vect);

        offset += sizeof(__m256i);
        dst_vect += 1;
    }
    offset -= sizeof(__m256i);

    register uint32_t *dst_dwords = (uint32_t*) (dst + offset);
    register uint32_t val_dword = 0xFFFFFFFF;
    offset += sizeof(uint32_t);
    while (offset <= size) {
        *dst_dwords = val_dword;

        offset += sizeof(uint32_t);
        dst_dwords += 1;
    }
    offset -= sizeof(uint32_t);

    register uint8_t *dst_bytes = (uint8_t*) (dst + offset);
    while (offset < size) {
        *dst_bytes = 0xFF;

        offset += sizeof(uint8_t);
        dst_bytes += 1;
    }

    // assert(offset == size);
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param offset - number of pixels to shift the object in bitmap image up
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note1: White pixels RGB(255,255,255) are treated as background. Object in the image refers to non-white pixels.
 * Note2: You can assume the object will never be moved off the screen
 **********************************************************************************************************************/
unsigned char *processMoveUp(register unsigned char *buffer_frame, unsigned width, unsigned height, int offset) {
    // handle negative offsets
    if (offset < 0){
        return processMoveDown(buffer_frame, width, height, offset * -1);
    }

    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned height_limit = (height - offset) * width3;
    unsigned offsetwidth3 = offset * width3;

    frame_copy_unaligned(buffer_frame + offsetwidth3, render_buffer, height_limit);

    // fill left over pixels with white pixels
    frame_clear(render_buffer + height_limit, offsetwidth3);

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param offset - number of pixels to shift the object in bitmap image left
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note1: White pixels RGB(255,255,255) are treated as background. Object in the image refers to non-white pixels.
 * Note2: You can assume the object will never be moved off the screen
 **********************************************************************************************************************/
unsigned char *processMoveRight(register unsigned char *buffer_frame, unsigned width, unsigned height, int offset) {
    // handle negative offsets
    if (offset < 0){
        return processMoveLeft(buffer_frame, width, height, offset * -1);
    }

    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned offset3 = offset * 3;
    unsigned size3 = width3 * height;
    unsigned numbytes = width3 - offset3;

    uint8_t *buffer_frame_start = buffer_frame;
    uint8_t *render_buffer_start = render_buffer;
    for (int row = 0; row < height; row++) {
        frame_clear(render_buffer_start, offset3);
        render_buffer_start += offset3;
        frame_copy_unaligned(buffer_frame_start, render_buffer_start, numbytes);
        render_buffer_start += numbytes;
        buffer_frame_start += width3;
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param offset - number of pixels to shift the object in bitmap image up
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note1: White pixels RGB(255,255,255) are treated as background. Object in the image refers to non-white pixels.
 * Note2: You can assume the object will never be moved off the screen
 **********************************************************************************************************************/
unsigned char *processMoveDown(register unsigned char *buffer_frame, unsigned width, unsigned height, int offset) {
    // handle negative offsets
    if (offset < 0){
        return processMoveUp(buffer_frame, width, height, offset * -1);
    }

    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned offsetwidth3 = offset * width3;
    unsigned numbytes = (height - offset) * width3;

    // fill left over pixels with white pixels
    frame_clear(render_buffer, offsetwidth3);
    frame_copy_unaligned(buffer_frame, render_buffer + offsetwidth3, numbytes);

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param offset - number of pixels to shift the object in bitmap image right
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note1: White pixels RGB(255,255,255) are treated as background. Object in the image refers to non-white pixels.
 * Note2: You can assume the object will never be moved off the screen
 **********************************************************************************************************************/
unsigned char *processMoveLeft(register unsigned char *buffer_frame, unsigned width, unsigned height, int offset) {
    // handle negative offsets
    if (offset < 0){
        return processMoveRight(buffer_frame, width, height, offset * -1);
    }

    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned offset3 = offset * 3;
    unsigned size3 = width3 * height;
    unsigned numbytes = width3 - offset3;

    uint8_t *buffer_frame_start = buffer_frame + offset3;
    uint8_t *render_buffer_start = render_buffer;
    for (int row = 0; row < height; row++) {
        frame_copy_unaligned(buffer_frame_start, render_buffer_start, numbytes);
        render_buffer_start += numbytes;
        frame_clear(render_buffer_start, offset3);
        render_buffer_start += offset3;
        buffer_frame_start += width3;
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param rotate_iteration - rotate object inside frame buffer clockwise by 90 degrees, <iteration> times
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note: You can assume the frame will always be square and you will be rotating the entire image
 **********************************************************************************************************************/
unsigned char *processRotateCW(unsigned char *buffer_frame, unsigned width, unsigned height,
                               int rotate_iteration) {
    rotate_iteration = rotate_iteration % 4;
    if (rotate_iteration == 0) {
        return buffer_frame;
    }

    // handle negative offsets
    if (rotate_iteration < 0){
        return processRotateCCW(buffer_frame, width, height, rotate_iteration * -1);
    }

    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    int width3 = width * 3;
    int height3 = height * 3;
    int size = width * height;
    int size3 = size * 3;

    if (rotate_iteration == 1) {
        int position_render_buffer = width3 - 3;
        int position_buffer_frame = 0;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                *((uint16_t*)&render_buffer[position_render_buffer]) = *((uint16_t*)&buffer_frame[position_buffer_frame]);
                render_buffer[position_render_buffer+2] = buffer_frame[position_buffer_frame+2];

                position_render_buffer += width3;
                position_buffer_frame += 3;
            }

            position_render_buffer -= 3 + size3;
        }
    } else if (rotate_iteration == 2) {
        render_buffer = processMirrorY(buffer_frame, width, height, 0);
        return processMirrorX(render_buffer, width , height, 0);
    } else if (rotate_iteration == 3) {
        int position_render_buffer = size3 - width3;
        int position_buffer_frame = 0;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < height; column++) {
                *((uint16_t*)&render_buffer[position_render_buffer]) = *((uint16_t*)&buffer_frame[position_buffer_frame]);
                render_buffer[position_render_buffer+2] = buffer_frame[position_buffer_frame+2];

                position_render_buffer -= width3;
                position_buffer_frame += 3;
            }

            position_render_buffer += 3 + size3;
        }
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param rotate_iteration - rotate object inside frame buffer counter clockwise by 90 degrees, <iteration> times
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note: You can assume the frame will always be square and you will be rotating the entire image
 **********************************************************************************************************************/
unsigned char *processRotateCCW(register unsigned char *buffer_frame, unsigned width, unsigned height,
                                int rotate_iteration) {

    rotate_iteration = rotate_iteration % 4;
    if (rotate_iteration == 0) {
        return buffer_frame;
    }
    // handle negative offsets
    if (rotate_iteration < 0){
        return processRotateCW(buffer_frame, width, height, rotate_iteration * -1);
    }

    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    int width3 = width * 3;
    int height3 = height * 3;
    int size = width * height;
    int size3 = size * 3;

    if (rotate_iteration == 3) {
        int position_render_buffer = width3 - 3;
        int position_buffer_frame = 0;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                *((uint16_t*)&render_buffer[position_render_buffer]) = *((uint16_t*)&buffer_frame[position_buffer_frame]);
                render_buffer[position_render_buffer+2] = buffer_frame[position_buffer_frame+2];

                position_render_buffer += width3;
                position_buffer_frame += 3;
            }

            position_render_buffer -= 3 + size3;
        }
    } else if (rotate_iteration == 2) {
        render_buffer = processMirrorY(buffer_frame, width, height, 0);
        return processMirrorX(render_buffer, width , height, 0);
    } else if (rotate_iteration == 1) {
        int position_render_buffer = size3 - width3;
        int position_buffer_frame = 0;
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < height; column++) {
                *((uint16_t*)&render_buffer[position_render_buffer]) = *((uint16_t*)&buffer_frame[position_buffer_frame]);
                render_buffer[position_render_buffer+2] = buffer_frame[position_buffer_frame+2];

                position_render_buffer -= width3;
                position_buffer_frame += 3;
            }

            position_render_buffer += 3 + size3;
        }
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param _unused - this field is unused
 * @return
 **********************************************************************************************************************/
unsigned char *processMirrorX(register unsigned char *buffer_frame, unsigned int width, unsigned int height, int _unused) {
    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned size3 = width3 * height;

    uint8_t *buffer_frame_start = buffer_frame;
    uint8_t *render_buffer_start = render_buffer + size3;
    for (int row = 0; row < height; row++) {
        render_buffer_start -= width3;
        frame_copy_unaligned(buffer_frame_start, render_buffer_start, width3);
        buffer_frame_start += width3;
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param _unused - this field is unused
 * @return
 **********************************************************************************************************************/
unsigned char *processMirrorY(register unsigned char *buffer_frame, unsigned width, unsigned height, int _unused) {
    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);
    memset(render_buffer, 0xFF, width * height * 3);
    register __m128i shuffle_order = _mm_setr_epi8(12, 13, 14, 9, 10, 11, 6, 7, 8, 3, 4, 5, 0, 1, 2, 15);

    unsigned width3 = width * 3;
    unsigned render_buffer_fixup = width3 + width3 - 3;

    uint8_t *buffer_frame_start = buffer_frame;
    uint8_t *render_buffer_start = render_buffer + width3 - 3;
    // store shifted pixels to temporary buffer
    for (int row = 0; row < height; row++) {
        int column = 1;

        render_buffer_start[0] = buffer_frame_start[0];
        render_buffer_start[1] = buffer_frame_start[1];
        render_buffer_start[2] = buffer_frame_start[2];

        buffer_frame_start -= 12;

        // We will mirror 5 pixels at a time, but we need to fix the 16 byte
        for (column += 5; column <= width; column += 5) {
            buffer_frame_start += 15;
            render_buffer_start -= 15;

            register __m128i pixels = _mm_loadu_si128((__m128i*) buffer_frame_start);
            register __m128i mirrored = _mm_shuffle_epi8(pixels, shuffle_order);
            _mm_storeu_si128((__m128i*) render_buffer_start, mirrored);

            render_buffer_start[15] = *(buffer_frame_start - 3);
        }

        for (column -= 5; column < width; column++) {
            render_buffer_start -= 3;
            buffer_frame_start += 3;
            render_buffer_start[0] = buffer_frame_start[0];
            render_buffer_start[1] = buffer_frame_start[1];
            render_buffer_start[2] = buffer_frame_start[2];
        }
        
        render_buffer_start += render_buffer_fixup;
        buffer_frame_start += 15;
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

/***********************************************************************************************************************
 * WARNING: Do not modify the implementation_driver and team info prototype (name, parameter, return value) !!!
 *          Do not forget to modify the team_name and team member information !!!
 **********************************************************************************************************************/
void print_team_info(){
    // Please modify this field with something interesting
    char team_name[] = "AVXNGERS";

    // Please fill in your information
    char student_first_name[] = "Benjamin";
    char student_last_name[] = "Cheng";
    char student_student_number[] = "1004838045";

    // Printing out team information
    printf("*******************************************************************************************************\n");
    printf("Team Information:\n");
    printf("\tteam_name: %s\n", team_name);
    printf("\tstudent_first_name: %s\n", student_first_name);
    printf("\tstudent_last_name: %s\n", student_last_name);
    printf("\tstudent_student_number: %s\n", student_student_number);
}

/***********************************************************************************************************************
 * WARNING: Do not modify the implementation_driver and team info prototype (name, parameter, return value) !!!
 *          You can modify anything else in this file
 ***********************************************************************************************************************
 * @param sensor_values - structure stores parsed key value pairs of program instructions
 * @param sensor_values_count - number of valid sensor values parsed from sensor log file or commandline console
 * @param frame_buffer - pointer pointing to a buffer storing the imported  24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param grading_mode - turns off verification and turn on instrumentation
 ***********************************************************************************************************************
 *
 **********************************************************************************************************************/
void implementation_driver(struct kv *sensor_values, int sensor_values_count, unsigned char *frame_buffer,
                           unsigned int width, unsigned int height, bool grading_mode) {
    int processed_frames = 0;
    for (int sensorValueIdx = 0; sensorValueIdx < sensor_values_count; sensorValueIdx++) {
//        printf("Processing sensor value #%d: %s, %d\n", sensorValueIdx, sensor_values[sensorValueIdx].key,
//               sensor_values[sensorValueIdx].value);
        if (!strcmp(sensor_values[sensorValueIdx].key, "W")) {
            frame_buffer = processMoveUp(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "A")) {
            frame_buffer = processMoveLeft(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "S")) {
            frame_buffer = processMoveDown(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "D")) {
            frame_buffer = processMoveRight(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "CW")) {
            frame_buffer = processRotateCW(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "CCW")) {
            frame_buffer = processRotateCCW(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "MX")) {
            frame_buffer = processMirrorX(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        } else if (!strcmp(sensor_values[sensorValueIdx].key, "MY")) {
            frame_buffer = processMirrorY(frame_buffer, width, height, sensor_values[sensorValueIdx].value);
//            printBMP(width, height, frame_buffer);
        }

        processed_frames += 1;
        if (processed_frames % 25 == 0) {
            verifyFrame(frame_buffer, width, height, grading_mode);
        }
    }
    return;
}
