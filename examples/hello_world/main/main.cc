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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "main_functions.h"
#include "wifi_test_code.h"
extern TaskHandle_t prediction_task_handle;
extern "C" void app_main(void)
{
  App_main_wifi();
  setup();

  // while (true)
  // {
  // loop();
  xTaskCreatePinnedToCore(loop, "prediction_task", 2 * 1024, NULL, 2, &prediction_task_handle, 1);
  //   // trigger one inference every 500ms
  //   vTaskDelay(pdMS_TO_TICKS(500));
  // }
}
