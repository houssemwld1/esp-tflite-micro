#include <stdio.h>
#include "csi_matrices.h"
#include "image_generator.h"
#include "esp_heap_caps.h"
#include <iostream> // for std::cout, std::cerr
#include <vector>   // for std::vector

// Include the stb_image_write header
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
static float flatImage[1][3][625][1]; // Static, retains its value across function calls
#define RESIZED_IMAGE_SIZE 25
#define IMAGE_COUNT 3 // Adjust according to your needs
size_t peak_memory_usage = 0; 

void colormap(float value, unsigned char *gray)
{
    *gray = static_cast<unsigned char>(value * 255);
}

void monitor_heap_memory() {
    size_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t used = total - free; // Calculate used memory
    size_t used_percentage = (used * 100) / total; // Calculate percentage used
    peak_memory_usage = (peak_memory_usage < used) ? used : peak_memory_usage; // Update peak memory usage

    // Print detailed memory information in one line
    printf("Free: %zu bytes, Minimum free: %zu bytes, Total: %zu bytes, Used: %zu bytes, Percentage used: %zu%%, Peak usage: %zu bytes%s\n", 
           free, minimum, total, used, used_percentage, peak_memory_usage, 
           (free < (total * 0.1)) ? " - Warning: Low free memory!" : "");
}

void generateResizedHeatmapFromMatrix(float matrix[55][50], unsigned char *image)
{
    // Find the min and max values in the matrix
    float min_val = matrix[0][0];
    float max_val = matrix[0][0];

    for (int i = 0; i < 55; ++i)
    {
        for (int j = 0; j < 50; ++j)
        {
            if (matrix[i][j] < min_val)
                min_val = matrix[i][j];
            if (matrix[i][j] > max_val)
                max_val = matrix[i][j];
        }
    }

    monitor_heap_memory();

    // Generate the resized image
    for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i)
    {
        for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j)
        {
            float srcX = static_cast<float>(j) * 50 / RESIZED_IMAGE_SIZE;
            float srcY = static_cast<float>(i) * 55 / RESIZED_IMAGE_SIZE;

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);

            // Ensure indices are within bounds
            x0 = (x0 >= 50) ? 49 : x0;
            y0 = (y0 >= 55) ? 54 : y0;

            float normalized_value = (max_val - min_val > 0) ? (matrix[y0][x0] - min_val) / (max_val - min_val) : 0;
            colormap(normalized_value, &image[i * RESIZED_IMAGE_SIZE + j]); // Use pointer
        }
    }

    monitor_heap_memory();
}

void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer)
{
    png_buffer.clear(); // Clear the buffer before encoding

    // Create a custom write function
    auto write_function = [](void *context, void *data, int size)
    {
        std::vector<unsigned char> *buffer = static_cast<std::vector<unsigned char> *>(context);
        buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
    };

    // Encode the PNG
    int error = stbi_write_png_to_func(write_function, &png_buffer, width, height, 1, image, width);

    if (error)
    {
        printf("PNG encoded successfully, size: %zu bytes\n", png_buffer.size());
    }
    else
    {
        printf("Encoder error while creating PNG\n");
    }
}

// New function to flatten images
void flattenImages(unsigned char images[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE], float flatImages[1][3][625][1], int width, int height)
{
    for (int imgIndex = 0; imgIndex < IMAGE_COUNT; ++imgIndex)
    {
        for (int i = 0; i < height; ++i)
        {
            for (int j = 0; j < width; ++j)
            {
                // Normalizing and storing in the appropriate dimension
                flatImages[0][imgIndex][i * width + j][0] = static_cast<float>(images[imgIndex][i * width + j]) / 255.0f;
            }
        }
    }
}

void generateImagesFromMatrices()
{
    unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
    
    for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
    {
        unsigned char resizedImage[RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
        generateResizedHeatmapFromMatrix(csi_matrices[iteration], resizedImage);
        printf("Generated image for iteration %d\n", iteration);

        // Encode to PNG (optional)
        std::vector<unsigned char> png_buffer; 
        encodeToPNG(resizedImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);
        
        // Store the resized images for flattening
        memcpy(resizedImages[iteration], resizedImage, sizeof(resizedImage));
    }
    printf("All images generated\n");
    // Prepare flatImage for model input
    flattenImages(resizedImages, flatImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);
    printf("flatImage size %zu\n", sizeof(flatImage));
    
    monitor_heap_memory();
}


// void generateImagesFromMatrices()
// {
//     // Vector to store all buffers of png images
//     std::vector<std::vector<unsigned char>> png_buffers;
//     std::vector<unsigned char> png_buffer; // Buffer to hold the PNG data
//     unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];

//     for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
//     {
//         unsigned char resizedImage[RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
//         generateResizedHeatmapFromMatrix(csi_matrices[iteration], resizedImage);
//         printf("Generated image for iteration %d\n", iteration);

//         encodeToPNG(resizedImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);
//         png_buffers.push_back(png_buffer);
        
//         // Store the resized images for flattening
//         memcpy(resizedImages[iteration], resizedImage, sizeof(resizedImage));
//     }

//     // Prepare flatImage for model input
//     float flatImage[1][3][625][1]; // Adjusting to hold 3 images with shape (1, 3, 625, 1)
//     flattenImages(resizedImages, flatImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);

//     png_buffer.clear(); // Clear the buffer after use
//     png_buffers.clear(); // Clear the buffers after use
//     printf("flatImage size %zu\n", sizeof(flatImage));
//     for (int i = 0; i < 3; i++)
//     {
//         for (int j = 0; j < 625/2; j++)
//         {
//             printf("%f ", flatImage[0][i][j][0]);
//         }
//         printf("\n");
//     }
//     printf("png_buffer size %zu\n", png_buffers.size());
//     printf("All images generated\n");
//     monitor_heap_memory();
// }



// #include <stdio.h>
// #include "csi_matrices.h"
// #include "image_generator.h"
// #include "esp_heap_caps.h"
// #include <iostream> // for std::cout, std::cerr
// #include <vector>   // for std::vector

// // Include the stb_image_write header
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "stb_image_write.h"

// #define RESIZED_IMAGE_SIZE 25
// #define IMAGE_COUNT 3 // Adjust according to your needs
// size_t peak_memory_usage = 0; 

// void colormap(float value, unsigned char *gray)
// {
//     *gray = static_cast<unsigned char>(value * 255);
// }

// void monitor_heap_memory() {
//     size_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
//     size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
//     size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
//     size_t used = total - free; // Calculate used memory
//     size_t used_percentage = (used * 100) / total; // Calculate percentage used
//     peak_memory_usage = (peak_memory_usage < used) ? used : peak_memory_usage; // Update peak memory usage

//     // Print detailed memory information in one line
//     printf("Free: %zu bytes, Minimum free: %zu bytes, Total: %zu bytes, Used: %zu bytes, Percentage used: %zu%%, Peak usage: %zu bytes%s\n", 
//            free, minimum, total, used, used_percentage, peak_memory_usage, 
//            (free < (total * 0.1)) ? " - Warning: Low free memory!" : "");
// }


// void generateResizedHeatmapFromMatrix(float matrix[55][50], unsigned char *image)
// {
//     // Find the min and max values in the matrix
//     float min_val = matrix[0][0];
//     float max_val = matrix[0][0];

//     for (int i = 0; i < 55; ++i)
//     {
//         for (int j = 0; j < 50; ++j)
//         {
//             if (matrix[i][j] < min_val)
//                 min_val = matrix[i][j];
//             if (matrix[i][j] > max_val)
//                 max_val = matrix[i][j];
//         }
//     }

//     monitor_heap_memory();

//     // Generate the resized image
//     for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i)
//     {
//         for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j)
//         {
//             float srcX = static_cast<float>(j) * 50 / RESIZED_IMAGE_SIZE;
//             float srcY = static_cast<float>(i) * 55 / RESIZED_IMAGE_SIZE;

//             int x0 = static_cast<int>(srcX);
//             int y0 = static_cast<int>(srcY);

//             // Ensure indices are within bounds
//             x0 = (x0 >= 50) ? 49 : x0;
//             y0 = (y0 >= 55) ? 54 : y0;

//             float normalized_value = (max_val - min_val > 0) ? (matrix[y0][x0] - min_val) / (max_val - min_val) : 0;
//             colormap(normalized_value, &image[i * RESIZED_IMAGE_SIZE + j]); // Use pointer
//         }
//     }

//     monitor_heap_memory();
// }



// void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer)
// {
//     png_buffer.clear(); // Clear the buffer before encoding

//     // Create a custom write function
//     auto write_function = [](void *context, void *data, int size)
//     {
//         std::vector<unsigned char> *buffer = static_cast<std::vector<unsigned char> *>(context);
//         buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
//     };

//     // Encode the PNG
//     int error = stbi_write_png_to_func(write_function, &png_buffer, width, height, 1, image, width);

//     if (error)
//     {
//         printf("PNG encoded successfully, size: %zu bytes\n", png_buffer.size());
//     }
//     else
//     {
//         printf("Encoder error while creating PNG\n");
//     }
// }

// void generateImagesFromMatrices()
// {
//     // vector to store all buffers of png images
//     std::vector<std::vector<unsigned char>> png_buffers;
//     std::vector<unsigned char> png_buffer; // Buffer to hold the PNG data

//     for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
//     {
//         unsigned char resizedImage[RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
//         generateResizedHeatmapFromMatrix(csi_matrices[iteration], resizedImage);
//         printf("Generated image for iteration %d\n", iteration);

//         encodeToPNG(resizedImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);
//         png_buffers.push_back(png_buffer);
//         // The png_buffer can be used for further processing or transmission
//     }
//     png_buffer.clear(); // Clear the buffer after use
//     printf("png_buffer size %zu\n", png_buffers.size());
//     printf("All images generated\n");
//     monitor_heap_memory();
//     // png_buffers.clear(); // Clear the buffers after use
// }
