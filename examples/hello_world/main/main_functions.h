/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_
// #include <vector> // Ensure this line is included
// #include <iostream> // Optional: only if needed elsewhere

// Expose a C friendly interface for main functions.
#ifdef __cplusplus
extern "C" {
#endif
#define RESIZED_IMAGE_SIZE 25
#define IMAGE_COUNT 3 // Adjust according to your needs
// Initializes all data needed for the example. The name is important, and needs
// to be setup() for Arduino compatibility.
void setup();
void monitor_heap_memory();
void generateImagesFromMatrices();
void flattenImages(unsigned char images[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE], float flatImages[1][3][625][1], int width, int height);
// void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer);
void generateResizedHeatmapFromMatrix(float matrix[55][50], unsigned char *image);

// Runs one iteration of data gathering and inference. This should be called
// repeatedly from the application code. The name needs to be loop() for Arduino
// compatibility.
void loop(void *param);

#ifdef __cplusplus
}
#endif

#endif  // TENSORFLOW_LITE_MICRO_EXAMPLES_HELLO_WORLD_MAIN_FUNCTIONS_H_
