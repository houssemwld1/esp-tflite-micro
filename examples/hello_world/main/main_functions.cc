

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "main_functions.h"
#include "model.h"
#include "constants.h"
#include "output_handler.h"
#include "esp_heap_caps.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "csi_matrices.h"
#include "image_generator.h"

// Include the stb_image_write header
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float flatImage[1][3][625][1]; // Static to retain its value across function calls

size_t peak_memory_usage = 0;
void monitor_heap_memory()
{
  size_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t used = total - free;
  size_t used_percentage = (used * 100) / total;
  peak_memory_usage = std::max(peak_memory_usage, used);

  printf("Free: %zu bytes, Minimum free: %zu bytes, Total: %zu bytes, Used: %zu bytes, Percentage used: %zu%%, Peak usage: %zu bytes%s\n",
         free, minimum, total, used, used_percentage, peak_memory_usage,
         (free < (total * 0.1)) ? " - Warning: Low free memory!" : "");
}
void colormap(float value, unsigned char *gray)
{
  *gray = static_cast<unsigned char>(value * 255);
}

void generateResizedHeatmapFromMatrix(float matrix[55][50], unsigned char *image)
{
  float min_val = matrix[0][0];
  float max_val = matrix[0][0];

  for (int i = 0; i < 55; ++i)
  {
    for (int j = 0; j < 50; ++j)
    {
      min_val = std::min(min_val, matrix[i][j]);
      max_val = std::max(max_val, matrix[i][j]);
    }
  }

  for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i)
  {
    for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j)
    {
      float srcX = static_cast<float>(j) * 50 / RESIZED_IMAGE_SIZE;
      float srcY = static_cast<float>(i) * 55 / RESIZED_IMAGE_SIZE;

      int x0 = std::min(static_cast<int>(srcX), 49);
      int y0 = std::min(static_cast<int>(srcY), 54);

      float normalized_value = (max_val - min_val > 0) ? (matrix[y0][x0] - min_val) / (max_val - min_val) : 0;
      colormap(normalized_value, &image[i * RESIZED_IMAGE_SIZE + j]);
    }
  }
}

void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer)
{
  png_buffer.clear();

  auto write_function = [](void *context, void *data, int size)
  {
    auto *buffer = static_cast<std::vector<unsigned char> *>(context);
    buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
  };

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

void flattenImages(unsigned char images[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE],
                   float flatImages[1][3][625][1], int width, int height)
{
  for (int imgIndex = 0; imgIndex < IMAGE_COUNT; ++imgIndex)
  {
    for (int i = 0; i < height; ++i)
    {
      for (int j = 0; j < width; ++j)
      {
        flatImages[0][imgIndex][i * width + j][0] =
            static_cast<float>(images[imgIndex][i * width + j]) / 255.0f;
      }
    }
  }
}

void generateImagesFromMatrices()
{
  unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];

  for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
  {
    generateResizedHeatmapFromMatrix(csi_matrices[iteration], resizedImages[iteration]);
    printf("Generated image for iteration %d\n", iteration);

    // Optional: Encode to PNG
    std::vector<unsigned char> png_buffer;
    encodeToPNG(resizedImages[iteration], RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);
  }

  flattenImages(resizedImages, flatImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);
  printf("flatImage size %zu\n", sizeof(flatImage));
}

namespace
{
  const tflite::Model *model = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *output = nullptr;
  int inference_count = 0;
  constexpr int kTensorArenaSize = 70 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
}

void setup()
{
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION)
  {
    MicroPrintf("Model provided is schema version %d not equal to supported version %d.",
                model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  static tflite::MicroMutableOpResolver<30> resolver;
  if (resolver.AddConv2D() != kTfLiteOk ||
      resolver.AddMaxPool2D() != kTfLiteOk ||
      resolver.AddFullyConnected() != kTfLiteOk ||
      resolver.AddSoftmax() != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk ||
      resolver.AddStridedSlice() != kTfLiteOk ||
      resolver.AddPack() != kTfLiteOk ||
      resolver.AddQuantize() != kTfLiteOk ||
      resolver.AddDequantize() != kTfLiteOk ||
      resolver.AddWhile() != kTfLiteOk ||
      resolver.AddLess() != kTfLiteOk ||
      resolver.AddGreater() != kTfLiteOk ||
      resolver.AddLogicalAnd() != kTfLiteOk ||
      resolver.AddAdd() != kTfLiteOk ||
      resolver.AddGather() != kTfLiteOk ||
      resolver.AddSplit() != kTfLiteOk ||
      resolver.AddLogistic() != kTfLiteOk ||
      resolver.AddMul() != kTfLiteOk ||
      resolver.AddTanh() != kTfLiteOk ||
      resolver.AddExpandDims() != kTfLiteOk ||
      resolver.AddFill() != kTfLiteOk ||
      resolver.AddSub() != kTfLiteOk ||
      resolver.AddConcatenation() != kTfLiteOk ||
      resolver.AddAveragePool2D() != kTfLiteOk ||
      resolver.AddPad() != kTfLiteOk ||
      resolver.AddMean() != kTfLiteOk)
  {
    return;
  }

  static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk)
  {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);

  size_t used_bytes = interpreter->arena_used_bytes();
  MicroPrintf("Memory used by tensors: %zu bytes", used_bytes);

  printf("shape of input tensor : %d\n", input->dims->size);
  printf("input tensor size : %d\n", input->dims->size);
  printf("input tensor shape : %d\n", input->dims->data[0]);
  printf("input tensor shape : %d\n", input->dims->data[1]);
  printf("input tensor shape : %d\n", input->dims->data[2]);
  printf("input tensor shape : %d\n", input->dims->data[3]);

  printf("shape of output tensor : %d\n", output->dims->size);
  printf("output tensor shape : %d\n", output->dims->data[0]);
  printf("output tensor shape : %d\n", output->dims->data[1]);

  MicroPrintf("Generating images from matrices\n");
  inference_count = 0;
}

void loop()
{
  generateImagesFromMatrices();
  printf("flatImage size from main setup : %zu\n", sizeof(flatImage));

  for (int i = 0; i < input->dims->data[1]; i++)
  {
    for (int j = 0; j < input->dims->data[2]; j++)
    {
      for (int k = 0; k < input->dims->data[3]; k++)
      {
        input->data.f[(i * input->dims->data[2] + j) * input->dims->data[3] + k] = flatImage[0][i][j][k];
      }
    }
  }

  if (interpreter->Invoke() != kTfLiteOk)
  {
    MicroPrintf("Invoke failed");
    return;
  }

  MicroPrintf("Inference output:");
  for (int i = 0; i < output->dims->data[1]; i++)
  {
    printf("%f ", output->data.f[i]);
  }
  printf("\n");
  int max_index = 0;
  float max_value = output->data.f[0];
  for (int i = 1; i < output->dims->data[1]; i++)
  {
    if (output->data.f[i] > max_value)
    {
      max_value = output->data.f[i];
      max_index = i;
    }
  }

  HandleOutput(max_index);

  monitor_heap_memory();
  inference_count += 1;
}

// #include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
// #include "tensorflow/lite/micro/micro_interpreter.h"
// #include "tensorflow/lite/micro/system_setup.h"
// #include "tensorflow/lite/schema/schema_generated.h"
// #include "main_functions.h"
// #include "model.h"
// #include "constants.h"
// #include "output_handler.h"
// #include "esp_heap_caps.h"
// // #include "image_generator.h"
// #include "esp_heap_caps.h"
// #include "esp_vfs.h"
// #include "esp_spiffs.h"
// /// test code

// #include <stdio.h>
// #include "csi_matrices.h"
// #include "image_generator.h"
// #include "esp_heap_caps.h"

// // Include the stb_image_write header
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "stb_image_write.h"
// static float flatImage[1][3][625][1]; // Static, retains its value across function calls

// size_t peak_memory_usage = 0;

// void colormap(float value, unsigned char *gray)
// {
//   *gray = static_cast<unsigned char>(value * 255);
// }

// void generateResizedHeatmapFromMatrix(float matrix[55][50], unsigned char *image)
// {
//   // Find the min and max values in the matrix
//   float min_val = matrix[0][0];
//   float max_val = matrix[0][0];

//   for (int i = 0; i < 55; ++i)
//   {
//     for (int j = 0; j < 50; ++j)
//     {
//       if (matrix[i][j] < min_val)
//         min_val = matrix[i][j];
//       if (matrix[i][j] > max_val)
//         max_val = matrix[i][j];
//     }
//   }

//   // monitor_heap_memory();

//   // Generate the resized image
//   for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i)
//   {
//     for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j)
//     {
//       float srcX = static_cast<float>(j) * 50 / RESIZED_IMAGE_SIZE;
//       float srcY = static_cast<float>(i) * 55 / RESIZED_IMAGE_SIZE;

//       int x0 = static_cast<int>(srcX);
//       int y0 = static_cast<int>(srcY);

//       // Ensure indices are within bounds
//       x0 = (x0 >= 50) ? 49 : x0;
//       y0 = (y0 >= 55) ? 54 : y0;

//       float normalized_value = (max_val - min_val > 0) ? (matrix[y0][x0] - min_val) / (max_val - min_val) : 0;
//       colormap(normalized_value, &image[i * RESIZED_IMAGE_SIZE + j]); // Use pointer
//     }
//   }

//   // monitor_heap_memory();
// }

// void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer)
// {
//   png_buffer.clear(); // Clear the buffer before encoding

//   // Create a custom write function
//   auto write_function = [](void *context, void *data, int size)
//   {
//     std::vector<unsigned char> *buffer = static_cast<std::vector<unsigned char> *>(context);
//     buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
//   };

//   // Encode the PNG
//   int error = stbi_write_png_to_func(write_function, &png_buffer, width, height, 1, image, width);

//   if (error)
//   {
//     printf("PNG encoded successfully, size: %zu bytes\n", png_buffer.size());
//   }
//   else
//   {
//     printf("Encoder error while creating PNG\n");
//   }
// }

// // New function to flatten images
// void flattenImages(unsigned char images[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE], float flatImages[1][3][625][1], int width, int height)
// {
//   for (int imgIndex = 0; imgIndex < IMAGE_COUNT; ++imgIndex)
//   {
//     for (int i = 0; i < height; ++i)
//     {
//       for (int j = 0; j < width; ++j)
//       {
//         // Normalizing and storing in the appropriate dimension
//         flatImages[0][imgIndex][i * width + j][0] = static_cast<float>(images[imgIndex][i * width + j]) / 255.0f;
//       }
//     }
//   }
// }

// void generateImagesFromMatrices()
// {
//   unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];

//   for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
//   {
//     unsigned char resizedImage[RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
//     generateResizedHeatmapFromMatrix(csi_matrices[iteration], resizedImage);
//     printf("Generated image for iteration %d\n", iteration);

//     // Encode to PNG (optional)
//     std::vector<unsigned char> png_buffer;
//     encodeToPNG(resizedImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);

//     // Store the resized images for flattening
//     memcpy(resizedImages[iteration], resizedImage, sizeof(resizedImage));
//   }
//   printf("All images generated\n");
//   // Prepare flatImage for model input
//   flattenImages(resizedImages, flatImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);
//   printf("flatImage size %zu\n", sizeof(flatImage));

//   // monitor_heap_memory();
// }

// /// end test code
// // static float flatImage[1][3][625][1]; // Static, retains its value across function calls
// // The number of inferences to perform for each x value.
// // Globals, used for compatibility with Arduino-style sketches.

// /// this the main function model
// namespace
// {

//   const tflite::Model *model = nullptr;
//   tflite::MicroInterpreter *interpreter = nullptr;
//   TfLiteTensor *input = nullptr;
//   TfLiteTensor *output = nullptr;
//   int inference_count = 0;
//   constexpr int kTensorArenaSize = 70 * 1024; // Adjust as needed
//   uint8_t tensor_arena[kTensorArenaSize];
// } // namespace

// // The name of this function is important for Arduino compatibility.
// void setup()
// {
//   // Map the model into a usable data structure. This doesn't involve any
//   // copying or parsing, it's a very lightweight operation.
//   model = tflite::GetModel(g_model);
//   if (model->version() != TFLITE_SCHEMA_VERSION)
//   {
//     MicroPrintf("Model provided is schema version %d not equal to supported "
//                 "version %d.",
//                 model->version(), TFLITE_SCHEMA_VERSION);
//     return;
//   }

//   // Pull in only the operation implementations we need.

//   static tflite::MicroMutableOpResolver<30> resolver; // Adjust the size as needed
//   if (resolver.AddConv2D() != kTfLiteOk)
//     return;
//   if (resolver.AddMaxPool2D() != kTfLiteOk)
//     return;
//   if (resolver.AddFullyConnected() != kTfLiteOk)
//     return;
//   if (resolver.AddSoftmax() != kTfLiteOk)
//     return;
//   if (resolver.AddReshape() != kTfLiteOk)
//     return;
//   if (resolver.AddStridedSlice() != kTfLiteOk)
//     return;
//   if (resolver.AddPack() != kTfLiteOk)
//     return; // Add PACK operator
//   if (resolver.AddQuantize() != kTfLiteOk)
//     return; // Add QUANTIZE operator
//   if (resolver.AddDequantize() != kTfLiteOk)
//     return; // Add DEQUANTIZE operator
//   if (resolver.AddWhile() != kTfLiteOk)
//     return;
//   if (resolver.AddLess() != kTfLiteOk)
//     return;
//   if (resolver.AddGreater() != kTfLiteOk)
//     return; // Add GREATER operator
//   if (resolver.AddLogicalAnd() != kTfLiteOk)
//     return; // Add LOGICAL_AND operator
//   if (resolver.AddAdd() != kTfLiteOk)
//     return; // Add ADD operator
//   if (resolver.AddGather() != kTfLiteOk)
//     return; // Add GATHER operator
//   if (resolver.AddSplit() != kTfLiteOk)
//     return; // Add SPLIT operator
//   if (resolver.AddLogistic() != kTfLiteOk)
//     return; // Add SUB operator
//   if (resolver.AddMul() != kTfLiteOk)
//     return; // Add MUL operator
//   if (resolver.AddTanh() != kTfLiteOk)
//     return; // Add TANH operator
//   if (resolver.AddExpandDims() != kTfLiteOk)
//     return; // Add EXPAND_DIMS operator
//   if (resolver.AddFill() != kTfLiteOk)
//     return; // Add EQUAL operator
//   if (resolver.AddSub() != kTfLiteOk)
//     return; // Add SUB operator
//   if (resolver.AddConcatenation() != kTfLiteOk)
//     return; // Add CONCATENATION operator
//   if (resolver.AddAveragePool2D() != kTfLiteOk)
//     return; // Add AVERAGE_POOL_2D operator
//   // add Pad operator
//   if (resolver.AddPad() != kTfLiteOk)
//     return;
//   // add mean operator
//   if (resolver.AddMean() != kTfLiteOk)
//     return;

//   static tflite::MicroInterpreter static_interpreter(
//       model, resolver, tensor_arena, kTensorArenaSize);
//   interpreter = &static_interpreter;

//   // Allocate memory from the tensor_arena for the model's tensors.
//   TfLiteStatus allocate_status = interpreter->AllocateTensors();
//   if (allocate_status != kTfLiteOk)
//   {
//     MicroPrintf("AllocateTensors() failed");
//     return;
//   }

//   // Obtain pointers to the model's input and output tensors.
//   input = interpreter->input(0);
//   output = interpreter->output(0);

//   size_t used_bytes = interpreter->arena_used_bytes();
//   MicroPrintf("Memory used by tensors: %d bytes", used_bytes);

//   // check if flatimage values are not zero all
//   printf("shape of input tensor : %d\n", input->dims->size);
//   // show more detail about input tensor
//   printf("input tensor size : %d\n", input->dims->size);
//   printf("input tensor shape : %d\n", input->dims->data[0]);
//   printf("input tensor shape : %d\n", input->dims->data[1]);
//   printf("input tensor shape : %d\n", input->dims->data[2]);
//   printf("input tensor shape : %d\n", input->dims->data[3]);
//   // show more detail about output tensor
//   printf("shape of output tensor : %d\n", output->dims->size);
//   printf("output tensor shape : %d\n", output->dims->data[0]);
//   printf("output tensor shape : %d\n", output->dims->data[1]);

//   // image generation
//   MicroPrintf("Generating images from matrices\n");
//   // Keep track of how many inferences we have performed.
//   inference_count = 0;
// }

// // monitor heap memory

// // Global variable to track peak memory usage

// void monitor_heap_memory()
// {
//   size_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
//   size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
//   size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
//   size_t used = total - free;                                                // Calculate used memory
//   size_t used_percentage = (used * 100) / total;                             // Calculate percentage used
//   peak_memory_usage = (peak_memory_usage < used) ? used : peak_memory_usage; // Update peak memory usage

//   // Print detailed memory information in one line
//   printf("Free: %zu bytes, Minimum free: %zu bytes, Total: %zu bytes, Used: %zu bytes, Percentage used: %zu%%, Peak usage: %zu bytes%s\n",
//          free, minimum, total, used, used_percentage, peak_memory_usage,
//          (free < (total * 0.1)) ? " - Warning: Low free memory!" : "");
// }

// // The name of this function is important for Arduino compatibility.
// void loop()
// {

//   generateImagesFromMatrices();
//   printf("flatImage size from main setup :%zu\n", sizeof(flatImage));
//   // print some value from flatImage

//   //   Generate the input flatImage from matrices.

//   // Set the input tensor for the model
//   TfLiteTensor *input = interpreter->input(0);

//   // Populate the input tensor
//   for (int i = 0; i < 1; ++i)
//   {
//     for (int j = 0; j < 3; ++j)
//     {
//       for (int k = 0; k < 625; ++k)
//       {
//         float value = flatImage[i][j][k][0];
//         input->data.f[i * 3 * 625 + j * 625 + k] = value;

//         // printf("flatImage[%d][%d][%d] = %f\n", i, j, k, value); // Debugging line
//       }
//     }
//   }
//   // Run inference, and report any error
//   TfLiteStatus invoke_status = interpreter->Invoke();
//   if (invoke_status != kTfLiteOk)
//   {
//     MicroPrintf("Invoke failed\n");
//     return;
//   }
//   else
//   {
//     MicroPrintf("Invoke success\n");
//   }
//   monitor_heap_memory();
//   // Obtain the output tensor from model's output
//   TfLiteTensor *output = interpreter->output(0);
//   // Assuming you have a classification problem with one output per class
//   int num_classes = output->dims->data[1]; // Get the number of classes from output tensor shape
//   MicroPrintf("Number of classes: %d\n", num_classes);
//   // Find the predicted class index
//   int predicted_class_index = 1;

//   float max_prob = output->data.f[0];

//   printf("max_prob : %f\n", max_prob);

//   for (int i = 1; i >= num_classes; ++i)
//   {
//     printf("output->data.f[%d] : %f\n", i, output->data.f[i]);
//     if (output->data.f[i] < max_prob)
//     {
//       max_prob = output->data.f[i];
//       predicted_class_index = i;
//     }
//   }
//   monitor_heap_memory();

//   // Output the predicted class index
//   // MicroPrintf("Predicted class index: %d\n", predicted_class_index);

//   // Handle the output as needed
//   HandleOutput(predicted_class_index);

//   // Increment the inference_counter, and reset it if we have reached the total number per cycle
//   inference_count += 1;
//   if (inference_count >= kInferencesPerCycle)
//     inference_count = 0;
//   // delete the flatImage
// }

// // The name of this function is important for Arduino compatibility.
// // just for test