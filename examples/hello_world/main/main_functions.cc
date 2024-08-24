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
// #include "csi_matrices.h"
// #include "image_generator.h"
// #include "wifi.h"
#include "mqtt.h"
#include "esp_radar.h"
#include "wifi_test_code.h"
// #include "wifi_sensing.h"

// all the code until here will be moved to image_generator.h
// Include the stb_image_write header
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "stb_image_write.h"

static float flatImage[1][3][625][1]; // Static to retain its value across function calls
extern esp_mqtt_client_handle_t client_mqtt;
// extern char bufDataString_data[700];
char dataPrediction[100];
// extern float Amp[57][28];
// circlular buffer
extern CircularBuffer csiBuffer;
size_t peak_memory_usage = 0;
// sensingStruct sensing;
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
/*
// // void encodeToPNG(unsigned char *image, unsigned width, unsigned height, std::vector<unsigned char> &png_buffer)
// // {
// //   png_buffer.clear();

// //   auto write_function = [](void *context, void *data, int size)
// //   {
// //     auto *buffer = static_cast<std::vector<unsigned char> *>(context);
// //     buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
// //   };

// //   int error = stbi_write_png_to_func(write_function, &png_buffer, width, height, 1, image, width);
// //   if (error)
// //   {
// //     printf("PNG encoded successfully, size: %zu bytes\n", png_buffer.size());
// //   }
// //   else
// //   {
// //     printf("Encoder error while creating PNG\n");
// //   }
// // }
*/

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

void generateImagesFromMatrices(int old, int news)
{
  unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];

  // for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
  // {
  //   generateResizedHeatmapFromMatrix(csiBuffer.buffer[bufferIndex], resizedImages[iteration]);
  //   printf("Generated image for iteration %d\n", iteration);

  //   // Optional: Encode to PNG
  //   // std::vector<unsigned char> png_buffer;
  //   // encodeToPNG(resizedImages[iteration], RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, png_buffer);
  // }
  int current = old;
  for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration)
  {
    // Generate image from the current matrix in the buffer
    generateResizedHeatmapFromMatrix(csiBuffer.buffer[current], resizedImages[iteration]);
    printf("Generated image for iteration %d from buffer index %d\n", iteration, current);

    // Move to the next index, wrapping around if necessary
    current = (current + 1) % BUFFER_SIZE;

    // Stop if we've processed all available matrices in the buffer
    if (current == news)
      break;
  }
  flattenImages(resizedImages, flatImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);
  printf("flatImage size %zu\n", sizeof(flatImage));
}

//// all the code above will be moved to image_generator.h

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

  // App_main_wifi();
  // WIFI_CONNECT();
  // Sensing_routine();
  mqtt_app_start();

  // Initialize the TensorFlow Lite interpreter
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

  // generateImagesFromMatrices();
  // printf("flatImage size from main setup : %zu\n", sizeof(flatImage));

  // for (int i = 0; i < input->dims->data[1]; i++)
  // {
  //   for (int j = 0; j < input->dims->data[2]; j++)
  //   {
  //     for (int k = 0; k < input->dims->data[3]; k++)
  //     {
  //       input->data.f[(i * input->dims->data[2] + j) * input->dims->data[3] + k] = flatImage[0][i][j][k];
  //     }
  //   }
  // }

  MicroPrintf("Generating images from matrices\n");
  inference_count = 0;
}

void loop(void *param)
{

  while (1)
  {
    // Wait for a notification that new data is available
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Acquire mutex to read from the buffer
    if (xSemaphoreTake(csiBuffer.mutex, portMAX_DELAY))
    {
      if (csiBuffer.count > 2)
      {

        csiBuffer.tail = (csiBuffer.tail + 1) % BUFFER_SIZE;
        csiBuffer.count--;
        // print the csibuffer all
        printf("==========================================================START===============================================================\n");
        // for (int k = 0; k < 3; k++)
        // {
        //   printf("K = %d ", k);

        //   for (int i = 0; i < 55; i++)
        //   {
        //     for (int j = 0; j < 1; j++)
        //     {
        //       // printf("%d", j);
        //       printf("%f ", csiBuffer.buffer[k][i][j]);
        //     }
        //     printf("\n");
        //   }
        //   printf("\n");
        // }
        printf("notification received\n");
        printf("csiBuffer.count : %d\n", csiBuffer.count);
        printf("csiBuffer.head after  : %d\n", (csiBuffer.head + 2) % BUFFER_SIZE);
        printf("csiBuffer.tail  after : %d\n", (csiBuffer.tail + 2) % BUFFER_SIZE);
        int news = ((csiBuffer.head + 2) % BUFFER_SIZE);
        int old = ((csiBuffer.tail + 2) % BUFFER_SIZE);
        // // Perform prediction with the CSI data
        // // ...

        generateImagesFromMatrices(old, news);
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
        // perform inference

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
        sprintf(dataPrediction, "%f %f %f %f", static_cast<double>((output->data.f[0])), static_cast<double>((output->data.f[1])), static_cast<double>((output->data.f[2])), static_cast<double>((output->data.f[3])));
        int msg_id = esp_mqtt_client_publish(client_mqtt, "/houssy/data", dataPrediction, strlen(dataPrediction), 0, 0);

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

        xSemaphoreGive(csiBuffer.mutex);
        monitor_heap_memory();

        printf("==============================================================END===========================================================\n");
      }
      else
      {
        xSemaphoreGive(csiBuffer.mutex);
      }
    }
    // App_main_wifi();
    // printf("bufDataString from main setup : %s\n", bufDataString_data);
    // // show bufDataString_data[700]

    // for (int i = 0; i < 700; i++)
    // {
    //   printf("csi_data %c", bufDataString_data[i]);
    // }

    // if (interpreter->Invoke() != kTfLiteOk)
    // {
    //   MicroPrintf("Invoke failed");
    //   return;
    // }

    // MicroPrintf("Inference output:");
    // for (int i = 0; i < output->dims->data[1]; i++)
    // {
    //   printf("%f ", output->data.f[i]);
    // }
    // sprintf(dataPrediction, "%f %f %f %f", static_cast<double>((output->data.f[0])), static_cast<double>((output->data.f[1])), static_cast<double>((output->data.f[2])), static_cast<double>((output->data.f[3])));
    // int msg_id = esp_mqtt_client_publish(client_mqtt, "/houssy/data", dataPrediction, strlen(dataPrediction), 0, 0);

    // printf("\n");
    // int max_index = 0;
    // float max_value = output->data.f[0];
    // for (int i = 1; i < output->dims->data[1]; i++)
    // {
    //   if (output->data.f[i] > max_value)
    //   {
    //     max_value = output->data.f[i];
    //     max_index = i;
    //   }
    // }

    // HandleOutput(max_index);

    // // monitor_heap_memory();
    // inference_count += 1;
  }
}