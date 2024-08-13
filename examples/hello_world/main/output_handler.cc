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

#include "output_handler.h"
#include "tensorflow/lite/micro/micro_log.h"

// Assuming you have a predefined array of class names or labels
const char* class_names[] = {"NO_ACTIVITY","PET","RUN","WALK"};

void HandleOutput(int predicted_class_index) {
    // Log the predicted class index
    if (predicted_class_index >= 0 && predicted_class_index < sizeof(class_names) / sizeof(class_names[0])) {
        // Log the predicted class
        MicroPrintf("Predicted class index: %d, Class name: %s",
                    predicted_class_index, class_names[predicted_class_index]);
    } else {
        // Log an error message if the predicted index is out of bounds
        MicroPrintf("Predicted class index out of bounds: %d", predicted_class_index);
    }
}
