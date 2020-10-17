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
#define MIN(a, b) ((a > b) ? b : a)

#define DIR_TOP 0
#define DIR_BOT 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

uint8_t *mirror_x(uint8_t *buffer_frame);
uint8_t *mirror_y(uint8_t *buffer_frame);

uint8_t temporary_buffer1[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));
uint8_t temporary_buffer2[10000 * 10000 * 3 + sizeof(__m256i) * 2] __attribute__((aligned(32)));

unsigned image_width;
unsigned image_width3;
unsigned image_size;
unsigned image_size3;

struct image_state {
    // Translation properties
    int32_t pos[2];
    int32_t clip[4];

    // transformed [0] top, [2] right
    // buffer 0 = top, 1 = bottom, 2 = left, 3 = right
    uint32_t orientation[2];
};

struct image_state image_state;

void dump_state() {
    printf("pos: %d %d, clip: %d %d %d %d, orientation: %d %d\n",
            image_state.pos[0], image_state.pos[1],
            image_state.clip[0], image_state.clip[1], image_state.clip[2], image_state.clip[3],
            image_state.orientation[0], image_state.orientation[1]);
}

static inline __attribute__((always_inline))
void clear_state() {
    memset(&image_state, 0, sizeof(image_state));
    image_state.orientation[1] = DIR_RIGHT;
}

static inline __attribute__((always_inline))
uint32_t opposite_dir(uint32_t dir) {
    if (dir == DIR_TOP) {
        return DIR_BOT;
    } else if (dir == DIR_BOT) {
        return DIR_TOP;
    } else if (dir == DIR_LEFT) {
        return DIR_RIGHT;
    } else {
        return DIR_LEFT;
    }
}

static inline __attribute__((always_inline))
bool is_normal_axis() {
    uint32_t up_dir = image_state.orientation[0];
    uint32_t right_dir = image_state.orientation[1];

    if (up_dir == DIR_TOP) {
        return right_dir == DIR_RIGHT;
    } else if (up_dir == DIR_RIGHT) {
        return right_dir == DIR_BOT;
    } else if (up_dir == DIR_BOT) {
        return right_dir == DIR_LEFT;
    } else {
        return right_dir == DIR_TOP;
    }
}

static inline __attribute__((always_inline))
uint8_t* acquire_temporary_buffer(uint8_t *frame_buffer) {
    if (frame_buffer == temporary_buffer1) {
        return temporary_buffer2;
    } else if (frame_buffer == temporary_buffer2) {
        return temporary_buffer1;
    }
    return temporary_buffer1;
}

static inline __attribute__((always_inline))
void frame_copy_unaligned(register uint8_t *src, register uint8_t *dst, register size_t size) {
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

static inline __attribute__((always_inline))
void frame_clear(register uint8_t *dst, register size_t size) {
    memset(dst, 0xFF, size);
    return;
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

    register uint8_t *render_buffer_start = render_buffer + image_width3 - 3;
    register uint8_t *buffer_frame_start = buffer_frame;
    for (int row = 0; row < image_width; row++) {
        for (int column = 0; column < image_width; column++) {
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

static inline uint8_t* translate(uint8_t *buffer_frame, unsigned x, unsigned y, int32_t clip[4]) {
    uint8_t *render_buffer = acquire_temporary_buffer(buffer_frame);
    unsigned clipped_height = image_width - clip[0] - clip[1];
    unsigned clipped_width = image_width - clip[2] - clip[3];
    int y_start = clip[0] - y;
    unsigned y_end = y_start + clipped_height;
    int x_start = clip[2] + x;
    unsigned x_end = x_start + clipped_width;
    y_start = MAX(y_start, 0);
    x_start = MAX(x_start, 0);
    y_end = MIN(y_end, image_width);
    x_end = MIN(x_end, image_width);
    clipped_height = y_end - y_start;
    clipped_width = x_end - x_start;

    unsigned x_start3 = x_start * 3;
    unsigned clipped_width3 = clipped_width * 3;
    unsigned x_post3 = x_start3 + clipped_width3;
    unsigned x_remaining3 = image_width3 - clipped_width3 - x_start3;

    frame_clear(render_buffer, y_start * image_width3);

    uint8_t *render_buffer_start = render_buffer + y_start * image_width3;
    uint8_t *buffer_frame_start = buffer_frame + clip[0] * image_width3 + clip[2] * 3;
    for (int row = 0; row < clipped_height; row++) {
        frame_clear(render_buffer_start, x_start3);
        frame_copy_unaligned(buffer_frame_start, render_buffer_start + x_start3, clipped_width3);
        frame_clear(render_buffer_start + x_post3, x_remaining3);

        render_buffer_start += image_width3;
        buffer_frame_start += image_width3;
    }

    frame_clear(render_buffer_start, (clip[1] + y) * image_width3);

    return render_buffer;
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


void queue_mirror(bool y) {
    if (y) {
        image_state.orientation[0] = opposite_dir(image_state.orientation[0]);
    } else {
        image_state.orientation[1] = opposite_dir(image_state.orientation[1]);
    }
}

void queue_rotation(uint32_t cw_iter) {
    cw_iter = cw_iter % 4;

    uint32_t temp = 0;

    if (cw_iter == 0) {
        return;
    } else if (cw_iter == 1) {
        // top becomes current left
        // right becomes current top
        temp = image_state.orientation[0];
        image_state.orientation[0] = opposite_dir(image_state.orientation[1]);
        image_state.orientation[1] = temp;
    } else if (cw_iter == 2) {
        // top becomes current bottom
        // right becomes current left
        image_state.orientation[0] = opposite_dir(image_state.orientation[0]);
        image_state.orientation[1] = opposite_dir(image_state.orientation[1]);
    } else if (cw_iter == 3) {
        // top becomes current right
        // right becomes current bottom
        temp = image_state.orientation[0];
        image_state.orientation[0] = image_state.orientation[1];
        image_state.orientation[1] = opposite_dir(temp);
    }

}

int32_t direction_scale[4] = {
    1, -1, -1, 1
};

int32_t vert_lookup[4][2] = {
    { 
        1, 1,
    },
    { 
        -1, 1,
    },
    { 
        1, 0,
    },
    { 
        -1, 0,
    },
};

void queue_translate_vert(int32_t y) {
    uint32_t up_dir = image_state.orientation[0];
    uint32_t right_dir = image_state.orientation[1];
    int32_t *clip = image_state.clip;

    int32_t real_dir = y > 0 ? up_dir : opposite_dir(up_dir);

    int32_t axis = vert_lookup[up_dir][1];
    int32_t delta = y * direction_scale[up_dir];
    int32_t pos = image_state.pos[axis] + delta;

    clip[real_dir] = MAX(clip[real_dir], pos * direction_scale[real_dir]);

    image_state.pos[axis] = pos;
}

int32_t horiz_lookup[4][2] = {
    { 
        1, 0,
    },
    { 
        -1, 0,
    },
    { 
        1, 1,
    },
    { 
        -1, 1,
    },
};

void queue_translate_horiz(int32_t x) {
    uint32_t up_dir = image_state.orientation[0];
    uint32_t right_dir = image_state.orientation[1];
    int32_t *clip = image_state.clip;

    int32_t real_dir = x > 0 ? right_dir : opposite_dir(right_dir);

    int32_t axis = horiz_lookup[up_dir][1];
    int32_t delta = x * direction_scale[right_dir];
    int32_t pos = image_state.pos[axis] + delta;

    clip[real_dir] = MAX(clip[real_dir], pos * direction_scale[real_dir]);

    image_state.pos[axis] = pos;
}

uint8_t* process_state(uint8_t *frame_buffer) {
    // translate
    frame_buffer = translate(frame_buffer, image_state.pos[0], image_state.pos[1], image_state.clip);
    // decompose rotation and mirror
    uint32_t top = image_state.orientation[0];
    uint32_t right = image_state.orientation[1];
    uint32_t cw_iter = 0;
    bool mirrorx = false;
    bool mirrory = false;
    if (top == DIR_TOP) {
        // no rotation
        if (right == DIR_LEFT) {
            mirrory = true;
        }
    } else if (top == DIR_LEFT) {
        cw_iter = 1;
        
        if (right == DIR_BOT) {
            mirrorx = true;
        }
    } else if (top == DIR_RIGHT) {
        cw_iter = 3;

        if (right == DIR_TOP) {
            mirrorx = true;
        }
    } else if (top == DIR_BOT) {
        // no rotation
        mirrorx = true;
        if (right == DIR_LEFT) {
            mirrory = true;
        }
    }
    if (mirrorx) {
        frame_buffer = mirror_x(frame_buffer);
    }
    if (mirrory) {
        frame_buffer = mirror_y(frame_buffer);
    }
    if (cw_iter == 1) {
        frame_buffer = rotate_right(frame_buffer);
    } else if (cw_iter == 2) {
        frame_buffer = rotate_180(frame_buffer);
    } else if (cw_iter == 3) {
        frame_buffer = rotate_left(frame_buffer);
    }


    clear_state();
    return frame_buffer;
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

    clear_state();

    int processed_frames = 0;
    for (int sensorValueIdx = 0; sensorValueIdx < sensor_values_count; sensorValueIdx++) {
        struct kv sensor_value = sensor_values[sensorValueIdx];
        char key0 = sensor_value.key[0];
        char key1 = sensor_value.key[1];
        int val = sensor_value.value;
        if (key0 == 'W') {
            queue_translate_vert(val);
        } else if (key0 == 'A') {
            queue_translate_horiz(-val);
        } else if (key0 == 'S') {
            queue_translate_vert(-val);
        } else if (key0 == 'D') {
            queue_translate_horiz(val);
        } else if (key0 == 'C') {
            if (key1 == 'W') { // CW
                if (val < 0)
                    queue_rotation(-val * 3);
                else
                    queue_rotation(val);
            } else if (key1 == 'C') { // CCW
                if (val < 0)
                    queue_rotation(-val);
                else
                    queue_rotation(val * 3);
            }
        } else if (key0 == 'M') {
            queue_mirror(key1 == 'X');
        }

        processed_frames += 1;

        if (processed_frames % 25 == 0) {
            frame_buffer = process_state(frame_buffer);
            verifyFrame(frame_buffer, width, height, grading_mode);
        }
    }
    return;
}
