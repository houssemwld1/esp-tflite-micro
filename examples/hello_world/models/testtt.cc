
#define MATRIX_ROWS 55
#define MATRIX_COLS 50
#define RESIZED_IMAGE_SIZE 25
#define IMAGE_COUNT 3 // Number of images to store

typedef struct
{
    unsigned char *data;
    size_t size;
} ImageData;

ImageData imageBuffer[IMAGE_COUNT];
int currentIndex = 0;

void colormap(float value, unsigned char &gray)
{
    // Simple colormap for grayscale image
    gray = static_cast<unsigned char>(value * 255);
}

void generateResizedHeatmapFromMatrix(float matrix[MATRIX_ROWS][MATRIX_COLS], unsigned char *image)
{
    // Find the min and max values in the matrix
    float min_val = matrix[0][0];
    float max_val = matrix[0][0];
    for (int i = 0; i < MATRIX_ROWS; ++i)
    {
        for (int j = 0; j < MATRIX_COLS; ++j)
        {
            if (matrix[i][j] < min_val)
                min_val = matrix[i][j];
            if (matrix[i][j] > max_val)
                max_val = matrix[i][j];
        }
    }

    // Directly generate the resized image
    for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i)
    {
        for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j)
        {
            float srcX = static_cast<float>(j) * MATRIX_COLS / RESIZED_IMAGE_SIZE;
            float srcY = static_cast<float>(i) * MATRIX_ROWS / RESIZED_IMAGE_SIZE;

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);

            float normalized_value = (max_val - min_val > 0) ? (matrix[y0][x0] - min_val) / (max_val - min_val) : 0;
            colormap(normalized_value, image[i * RESIZED_IMAGE_SIZE + j]);
        }
    }
}

void encodeToPNG(unsigned char *image, unsigned width, unsigned height, ImageData *imgData)
{
    // Create a buffer to hold the PNG data
    std::vector<unsigned char> png_buffer;

    // Encode the PNG using stb_image_write
    int result = stbi_write_png_to_func(
        [](void *context, void *data, int size) {
            std::vector<unsigned char> *buffer = static_cast<std::vector<unsigned char> *>(context);
            buffer->insert(buffer->end(), (unsigned char *)data, (unsigned char *)data + size);
        },
        &png_buffer,
        width, height, 1, image, width);

    if (result == 0)
    {
        std::cerr << "Encoder error while creating PNG" << std::endl;
        return;
    }

    imgData->size = png_buffer.size();
    imgData->data = (unsigned char *)malloc(imgData->size);
    memcpy(imgData->data, png_buffer.data(), imgData->size);
}

void addImageToBuffer(ImageData &imgData)
{
    if (imageBuffer[currentIndex].data != nullptr)
    {
        free(imageBuffer[currentIndex].data);
    }

    imageBuffer[currentIndex] = imgData;
    currentIndex = (currentIndex + 1) % IMAGE_COUNT;
}

void flattenImages(unsigned char *images[IMAGE_COUNT], float flatImages[1][3][625][1], int width, int height)
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
int main() {
    // Load data from .npy file
    cnpy::NpyArray arr = cnpy::npy_load("./no_activity_filtered_splited_first3.npy");
    std::cout << "arr shape: " << arr.shape[0] << " " << arr.shape[1] << " " << arr.shape[2] << std::endl;
    double *data = arr.data<double>();
    float matrices[IMAGE_COUNT][MATRIX_ROWS][MATRIX_COLS];
    for (int i = 0; i < IMAGE_COUNT; ++i) {
        for (int j = 0; j < MATRIX_ROWS; ++j) {
            for (int k = 0; k < MATRIX_COLS; ++k) {
                matrices[i][j][k] = static_cast<float>(data[i * MATRIX_ROWS * MATRIX_COLS + j * MATRIX_COLS + k]);
            }
        }
    }

    unsigned char resizedImages[IMAGE_COUNT][RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];

    for (int iteration = 0; iteration < IMAGE_COUNT; ++iteration) {
        unsigned char resizedImage[RESIZED_IMAGE_SIZE * RESIZED_IMAGE_SIZE];
        generateResizedHeatmapFromMatrix(matrices[iteration], resizedImage);

        ImageData imgDataResized;
        encodeToPNG(resizedImage, RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE, &imgDataResized);
        addImageToBuffer(imgDataResized);
        printf("Image size: %ld\n", imgDataResized.size);

        // Print the image data
        for (int i = 0; i < RESIZED_IMAGE_SIZE; ++i) {
            for (int j = 0; j < RESIZED_IMAGE_SIZE; ++j) {
                printf("%d ", resizedImage[i * RESIZED_IMAGE_SIZE + j]);
            }
            printf("\n");
        }
        // Write the PNG to a file
        std::ofstream outFileResized("heatmap_resized_" + std::to_string(iteration) + ".png", std::ios::binary);
        outFileResized.write(reinterpret_cast<const char *>(imgDataResized.data), imgDataResized.size);
        outFileResized.close();

        // Store the resized images for flattening
        memcpy(resizedImages[iteration], resizedImage, sizeof(resizedImage));
    }

    // Prepare flatImage for model input
    float flatImage[1][3][625][1]; // Adjusting to hold 3 images with shape (1, 3, 625, 1)
    flattenImages(resizedImages, flatImage[0][0], RESIZED_IMAGE_SIZE, RESIZED_IMAGE_SIZE);

    // TensorFlow Lite setup
    static tflite::MicroErrorReporter micro_error_reporter;
    tflite::ErrorReporter* error_reporter = &micro_error_reporter;

    const uint8_t *model_data = model_data; // Pointer to model data from the header
    const size_t model_data_size = model_data_len; // Length of the model data (defined in the header)

    static tflite::MicroMutableOpResolver<5> resolver; // Adjust the number of operations if necessary
    resolver.AddConv2D();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddQuantize();
    resolver.AddDequantize();

    // Create an interpreter
    uint8_t tensor_arena[80* 1024]; // Size of the memory arena for tensors
    tflite::MicroInterpreter interpreter(model_data, model_data_size, resolver, tensor_arena, sizeof(tensor_arena), error_reporter);

    // Allocate memory for the model's tensors
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        error_reporter->Report("AllocateTensors failed.");
        return -1;
    }

    // Check model version
    const tflite::Model* tflite_model = tflite::GetModel(model_data);
    if (tflite_model->version() != TFLITE_SCHEMA_VERSION) {
        error_reporter->Report("Model schema version mismatch.");
        return -1;
    }

    // Set the input tensor
    TfLiteTensor* input = interpreter.input(0);
    memcpy(input->data.f, flatImage, sizeof(flatImage)); // Copy the entire structure

    // Invoke the model
    if (interpreter.Invoke() != kTfLiteOk) {
        error_reporter->Report("Invoke failed.");
        return -1;
    }

    // Handle output
    TfLiteTensor* output = interpreter.output(0);
    std::cout << "Model output: " << output->data.f[0] << std::endl;

    // Free image buffer data
    for (int i = 0; i < IMAGE_COUNT; ++i) {
        if (imageBuffer[i].data) {
            std::cout << "Image " << i << " size: " << imageBuffer[i].size << " bytes" << std::endl;
            free(imageBuffer[i].data);
        }
    }

    return 0;
}