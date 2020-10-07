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

static inline uint8_t* align_temporary_buffer(uint8_t *frame_buffer) {
    static uint8_t temporary_buffer[10000 * 10000 * 3 + sizeof(__m256i) * 2];

    uint8_t frame_alignment = (size_t) frame_buffer % 32;
    uint8_t temp_alignment = (size_t) temporary_buffer % 32;

    return temporary_buffer + 32 - temp_alignment + frame_alignment;
}

// src, dst mod 32 must be equal
static void frame_copy(register uint8_t *src, register uint8_t *dst, register size_t size) {
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
    __m256i *src_vect = (__m256i*) (src + offset);
    __m256i *dst_vect = (__m256i*) (dst + offset);
    offset += sizeof(__m256i);
    while (offset <= size) { // equality occurs when there are exactly 32 bytes left to copy
        _mm256_stream_si256(dst_vect, *src_vect);

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

static void frame_clear(register uint8_t *dst, register size_t size) {
    register size_t offset;

    register __m256i *dst_vect = (__m256i*) dst;
    __m256i val_vect = _mm256_set1_epi64x(0xFFFFFFFFFFFFFFFF);
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

    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    unsigned height_limit = height - offset;
    unsigned width3 = width * 3;
    unsigned height3 = height * 3;

    // store shifted pixels to temporary buffer
    for (int row = 0; row < height_limit; row++) {
        int row_offset_render = row * width3;
        int row_offset_frame = (row + offset) * width3;
        for (int column = 0; column < width3; column += 3) {
            int position_rendered_frame = row_offset_render + column;
            int position_buffer_frame = row_offset_frame + column;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // fill left over pixels with white pixels
    frame_clear(render_buffer + (height - offset) * width3, offset * width3);

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height3);

    // return a pointer to the updated image buffer
    return buffer_frame;
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
    
    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned height3 = height * 3;
    unsigned offset3 = offset * 3;

    // store shifted pixels to temporary buffer
    for (int row = 0; row < height; row++) {
        int row_offset = row * width3;
        for (int column = offset3; column < width3; column += 3) {
            int position_rendered_frame = row_offset + column;
            int position_buffer_frame = position_rendered_frame - offset3;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // fill left over pixels with white pixels
    for (int row = 0; row < height; row++) {
        frame_clear(render_buffer + row * width3, offset3);
    }

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height3);

    // return a pointer to the updated image buffer
    return buffer_frame;
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

    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    unsigned width3 = width * 3;
    unsigned height3 = height * 3;
    unsigned offset3 = offset * 3;

    // store shifted pixels to temporary buffer
    for (int row = offset3; row < height3; row += 3) {
        int row_offset_render = row * width;
        int row_offset_frame = (row - offset3) * width;
        for (int column = 0; column < width3; column += 3) {
            int position_rendered_frame = row_offset_render + column;
            int position_buffer_frame = row_offset_frame + column;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // fill left over pixels with white pixels
    frame_clear(render_buffer, offset * width * 3);

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height * 3);

    // return a pointer to the updated image buffer
    return buffer_frame;
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

    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    int width_limit = width - offset;

    // store shifted pixels to temporary buffer
    for (int row = 0; row < height; row++) {
        int frame_y = row * width * 3;
        for (int column = 0; column < width_limit; column++) {
            int position_rendered_frame = frame_y + column * 3;
            int position_buffer_frame = frame_y + (column + offset) * 3;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // fill left over pixels with white pixels
    for (int row = 0; row < height; row++) {
        frame_clear(render_buffer + row * width * 3 + (width - offset) * 3, offset * 3);
    }

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height * 3);

    // return a pointer to the updated image buffer
    return buffer_frame;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param rotate_iteration - rotate object inside frame buffer clockwise by 90 degrees, <iteration> times
 * @return - pointer pointing a buffer storing a modified 24-bit bitmap image
 * Note: You can assume the frame will always be square and you will be rotating the entire image
 **********************************************************************************************************************/
unsigned char *processRotateCW(register unsigned char *buffer_frame, unsigned width, unsigned height,
                               int rotate_iteration) {
    rotate_iteration = rotate_iteration % 4;
    // handle negative offsets
    if (rotate_iteration < 0){
        return processRotateCCW(buffer_frame, width, height, rotate_iteration * -1);
    }

    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    int width3 = width * 3;
    int height3 = height * 3;
    int render_column_init = width3 - 3;
    int row_limit = width3 * width;

    int render_buffer_write_offset = width3 * height + 3;

    // store shifted pixels to temporary buffer
    for (int iteration = 0; iteration < rotate_iteration; iteration++) {
        uint8_t *render_buffer_write = &render_buffer[render_column_init];

        for (int row = 0; row < row_limit; row += width3) {
            for (int column = 0; column < height3; column += 3) {
                int position_frame_buffer = row + column;
                render_buffer_write[0] = buffer_frame[position_frame_buffer];
                render_buffer_write[1] = buffer_frame[position_frame_buffer + 1];
                render_buffer_write[2] = buffer_frame[position_frame_buffer + 2];

                render_buffer_write += width3;
            }

            render_buffer_write -= render_buffer_write_offset;
        }

        // copy the temporary buffer back to original frame buffer
        frame_copy(render_buffer, buffer_frame, width * height * 3);
    }

    // return a pointer to the updated image buffer
    return buffer_frame;
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
    if (rotate_iteration < 0){
        // handle negative offsets
        // rotating 90 degrees counter clockwise in opposite direction is equal to 90 degrees in cw direction
        buffer_frame = processRotateCW(buffer_frame, width, height, -rotate_iteration);
    } else {
        // rotating 90 degrees counter clockwise is equivalent of rotating 270 degrees clockwise
        buffer_frame = processRotateCW(buffer_frame, width, height, 3 * rotate_iteration);
    }

    // return a pointer to the updated image buffer
    return buffer_frame;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param _unused - this field is unused
 * @return
 **********************************************************************************************************************/
unsigned char *processMirrorX(register unsigned char *buffer_frame, unsigned int width, unsigned int height, int _unused) {
    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    // store shifted pixels to temporary buffer
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            int position_rendered_frame = row * height * 3 + column * 3;
            int position_buffer_frame = (height - row - 1) * height * 3 + column * 3;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height * 3);

    // return a pointer to the updated image buffer
    return buffer_frame;
}

/***********************************************************************************************************************
 * @param buffer_frame - pointer pointing to a buffer storing the imported 24-bit bitmap image
 * @param width - width of the imported 24-bit bitmap image
 * @param height - height of the imported 24-bit bitmap image
 * @param _unused - this field is unused
 * @return
 **********************************************************************************************************************/
unsigned char *processMirrorY(register unsigned char *buffer_frame, unsigned width, unsigned height, int _unused) {
    register uint8_t *render_buffer = align_temporary_buffer(buffer_frame);

    // store shifted pixels to temporary buffer
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            int position_rendered_frame = row * height * 3 + column * 3;
            int position_buffer_frame = row * height * 3 + (width - column - 1) * 3;
            render_buffer[position_rendered_frame] = buffer_frame[position_buffer_frame];
            render_buffer[position_rendered_frame + 1] = buffer_frame[position_buffer_frame + 1];
            render_buffer[position_rendered_frame + 2] = buffer_frame[position_buffer_frame + 2];
        }
    }

    // copy the temporary buffer back to original frame buffer
    frame_copy(render_buffer, buffer_frame, width * height * 3);

    // return a pointer to the updated image buffer
    return buffer_frame;
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
