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

#define MAX(a, b) ((a > b) ? a : b)


uint8_t *mirror_x(uint8_t *buffer_frame);
uint8_t *mirror_y(uint8_t *buffer_frame);

uint8_t temporary_buffer1[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));
uint8_t temporary_buffer2[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));
unsigned image_width;
unsigned image_width3;
unsigned image_size;
unsigned image_size3;

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

static inline uint8_t* rotate_left(uint8_t *buffer_frame) {
    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    uint8_t *render_buffer_start = render_buffer + image_size3 - image_width3;
    uint8_t *buffer_frame_start = buffer_frame;
    for (int row = 0; row < image_width; row++) {
        for (int column = 0; column < image_width; column++) {
            *((uint16_t*) render_buffer_start) = *((uint16_t*)buffer_frame_start);
            render_buffer_start[2] = buffer_frame_start[2];

            render_buffer_start -= image_width3;
            buffer_frame_start += 3;
        }

        render_buffer_start += 3 + image_size3;
    }

    return render_buffer;
}

static inline uint8_t* rotate_right(uint8_t *buffer_frame) {
    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    int column_limit = image_width - 3;

    register uint8_t *render_buffer_start = render_buffer + image_width3 - 3;
    register uint8_t *buffer_frame_start = buffer_frame;
    for (int row = 0; row < image_width; row++) {
        int column = 0;

        for (; column < column_limit; column += 4) {
            *((uint16_t*) render_buffer_start) = *((uint16_t*)buffer_frame_start);
            render_buffer_start[2] = buffer_frame_start[2];

            *((uint16_t*) (render_buffer_start + image_width3)) = *((uint16_t*)(buffer_frame_start + 3));
            render_buffer_start[image_width3 + 2] = buffer_frame_start[5];

            *((uint16_t*) (render_buffer_start + 2 * image_width3)) = *((uint16_t*)(buffer_frame_start + 6));
            render_buffer_start[2 * image_width3 + 2] = buffer_frame_start[8];

            *((uint16_t*) (render_buffer_start + 3 * image_width3)) = *((uint16_t*)(buffer_frame_start + 9));
            render_buffer_start[3 * image_width3 + 2] = buffer_frame_start[11];

            render_buffer_start += 4 * image_width3;
            buffer_frame_start += 12;
        }

        for (; column < image_width; column++) {
            *((uint16_t*) render_buffer_start) = *((uint16_t*)buffer_frame_start);
            render_buffer_start[2] = buffer_frame_start[2];

            render_buffer_start += image_width3;
            buffer_frame_start += 3;
        }

        render_buffer_start -= 3 + image_size3;
    }

    return render_buffer;
}

static inline uint8_t* rotate_180(uint8_t *buffer_frame) {
    uint8_t *render_buffer = mirror_y(buffer_frame);
    return mirror_x(render_buffer);
}

static inline uint8_t* translate(uint8_t *buffer_frame, unsigned x, unsigned y, unsigned clip[4]) {
    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);
    unsigned clipped_height = image_width - clip[0] - clip[1];
    unsigned clipped_width = image_width - clip[2] - clip[3];
    unsigned y_start = clip[0] - y;
    unsigned y_end = y_start + clipped_height;
    unsigned x_start = clip[2] + x;
    unsigned x_end = x_end + clipped_width;

    frame_clear(render_buffer, y_start * image_width3);

    uint8_t *render_buffer_start = render_buffer + y_start * image_width3;
    uint8_t *buffer_frame_start = buffer_frame + clip[0] * image_width3 + clip[2] * 3;
    for (int row = 0; row < clipped_height; row++) {
        frame_clear(render_buffer_start, x_start * 3);
        frame_copy_unaligned(buffer_frame_start, render_buffer_start + x_start * 3, clipped_width * 3);
        frame_clear(render_buffer_start + x_start * 3 + clipped_width * 3, image_width3 - x_start * 3 - clipped_width * 3);

        render_buffer_start += image_width3;
        buffer_frame_start += image_width3;
    }

    frame_clear(render_buffer_start, (clip[1] + y) * image_width3);

    return render_buffer;
}

static inline uint8_t* rotate(uint8_t *buffer_frame, int rot) {
    if (rot < 0) { // CCW
        rot = -rot;
        rot = rot % 4;

        if (rot == 3) {
            return rotate_right(buffer_frame);
        } else if (rot == 2) {
            return rotate_180(buffer_frame);
        } else if (rot == 1) {
            return rotate_left(buffer_frame);
        } else {
            return buffer_frame;
        }
    } else { // CW
        rot = rot % 4;
        if (rot == 1) {
            return rotate_right(buffer_frame);
        } else if (rot == 2) {
            return rotate_180(buffer_frame);
        } else if (rot == 3) {
            return rotate_left(buffer_frame);
        } else {
            return buffer_frame;
        }
    }
}


static inline uint8_t* mirror(uint8_t *buffer_frame, int mx, int my) {
    if (mx % 2 == 1) {
        buffer_frame = mirror_x(buffer_frame);
    }
    if (my % 2 == 1) {
        buffer_frame = mirror_y(buffer_frame);
    }
    return buffer_frame;
}

unsigned char *mirror_x(register unsigned char *buffer_frame) {
    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);

    uint8_t *buffer_frame_start = buffer_frame;
    uint8_t *render_buffer_start = render_buffer + image_size3;
    for (int row = 0; row < image_width; row++) {
        render_buffer_start -= image_width3;
        frame_copy_unaligned(buffer_frame_start, render_buffer_start, image_width3);
        buffer_frame_start += image_width3;
    }

    // return a pointer to the updated image buffer
    return render_buffer;
}

unsigned char *mirror_y(register unsigned char *buffer_frame) {
    register uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);
    register __m128i shuffle_order = _mm_setr_epi8(12, 13, 14, 9, 10, 11, 6, 7, 8, 3, 4, 5, 0, 1, 2, 15);

    unsigned render_buffer_fixup = image_width3 + image_width3 - 3;
    unsigned column_limit = image_width - 4;

    register uint8_t *buffer_frame_start = buffer_frame;
    register uint8_t *render_buffer_start = render_buffer + image_width3 - 3;
    // store shifted pixels to temporary buffer
    for (int row = 0; row < image_width; row++) {
        int column = 1;

        render_buffer_start[0] = buffer_frame_start[0];
        render_buffer_start[1] = buffer_frame_start[1];
        render_buffer_start[2] = buffer_frame_start[2];

        buffer_frame_start += 3;
        render_buffer_start -= 15;

        // We will mirror 5 pixels at a time, but we need to fix the 16 byte
        for (; column < column_limit; column += 5) {
            asm (
                ".intel_syntax noprefix;"
                "vmovdqu xmm0, [%0];"
                "vpshufb xmm0, xmm0, %2;"
                "vmovdqu [%1], xmm0;"
                ".att_syntax;"
                :
                : "r" ((__m256i*) buffer_frame_start), "r" ((__m256i*) render_buffer_start), "v" (shuffle_order)
                : "memory", "xmm0"
            );
            // register __m128i pixels = _mm_loadu_si128((__m128i*) buffer_frame_start);
            // register __m128i mirrored = _mm_shuffle_epi8(pixels, shuffle_order);
            // _mm_storeu_si128((__m128i*) render_buffer_start, mirrored);

            render_buffer_start[15] = *(buffer_frame_start - 3);

            buffer_frame_start += 15;
            render_buffer_start -= 15;
        }

        render_buffer_start += 12;

        for (; column < image_width; column++) {
            render_buffer_start[0] = buffer_frame_start[0];
            render_buffer_start[1] = buffer_frame_start[1];
            render_buffer_start[2] = buffer_frame_start[2];

            render_buffer_start -= 3;
            buffer_frame_start += 3;
        }
        
        render_buffer_start += 2 * image_width3;
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
    image_width = width;
    image_width3 = width * 3;
    image_size = width * width;
    image_size3 = width * width * 3;

    int state = 0; // 0 - translation, 1 - rotation, 2 - mirror
    int next_state = 0; // 4 - verify
    int x = 0;
    int y = 0;
    int rot = 0;
    int mx = 0;
    int my = 0;
    unsigned clip[4] = { 0, 0, 0, 0 }; // top bottom left right

    int processed_frames = 0;
    for (int sensorValueIdx = 0; sensorValueIdx < sensor_values_count; sensorValueIdx++) {
        struct kv sensor_value = sensor_values[sensorValueIdx];
        char key0 = sensor_value.key[0];
        char key1 = sensor_value.key[1];
        int val = sensor_value.value;
        if (key0 == 'W') {
            y += val;
            next_state = 0;
            if (y > 0) {
                clip[0] = MAX(clip[0], y);
            } else {
                clip[1] = MAX(clip[1], -y);
            }
        } else if (key0 == 'A') {
            x -= val;
            next_state = 0;
            if (x > 0) {
                clip[3] = MAX(clip[3], x);
            } else {
                clip[2] = MAX(clip[2], -x);
            }
        } else if (key0 == 'S') {
            y -= sensor_value.value;
            next_state = 0;
            if (y > 0) {
                clip[0] = MAX(clip[0], y);
            } else {
                clip[1] = MAX(clip[1], -y);
            }
        } else if (key0 == 'D') {
            x += sensor_value.value;
            next_state = 0;
            if (x > 0) {
                clip[3] = MAX(clip[3], x);
            } else {
                clip[2] = MAX(clip[2], -x);
            }
        } else if (key0 == 'C') {
            if (key1 == 'W') { // CW
                rot += sensor_value.value;
            } else if (key1 == 'C') { // CCW
                rot -= sensor_value.value;
            }
            next_state = 1;
        } else if (key0 == 'M') {
            if (key1 == 'X') {
                mx += 1;
            } else if (key1 == 'Y') {
                my += 1;
            }
            next_state = 2;
        }

        processed_frames += 1;

        if (next_state != state) {
            if (state == 0) {
                frame_buffer = translate(frame_buffer, x, y, clip);
                x = 0;
                y = 0;
                memset(clip, 0, 4 * sizeof(unsigned));
                state = next_state;
            } else if (state == 1) {
                frame_buffer = rotate(frame_buffer, rot);
                rot = 0;
            } else if (state == 2) {
                if (mx % 2 == 1) {
                    frame_buffer = mirror_x(frame_buffer);
                }
                if (my % 2 == 1) {
                    frame_buffer = mirror_y(frame_buffer);
                }
                mx = 0;
                my = 0;
            }
            state = next_state;
        }

        if (processed_frames % 25 == 0) {
            if (state == 0) {
                frame_buffer = translate(frame_buffer, x, y, clip);
                x = 0;
                y = 0;
                memset(clip, 0, 4 * sizeof(unsigned));
            } else if (state == 1) {
                frame_buffer = rotate(frame_buffer, rot);
                rot = 0;
            } else if (state == 2) {
                if (mx % 2 == 1) {
                    frame_buffer = mirror_x(frame_buffer);
                }
                if (my % 2 == 1) {
                    frame_buffer = mirror_y(frame_buffer);
                }
                mx = 0;
                my = 0;
            }
            verifyFrame(frame_buffer, width, height, grading_mode);
        }
    }
    return;
}
