#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "user_encoder_bsp.h"
#include "user_config.h"
#include "audio_bsp.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "main";

static void user_encoder_loop_task(void *arg);

void app_main(void)
{
  user_encoder_init();
  audio_bsp_init();
  ESP_LOGI(TAG,"audio start");
  xTaskCreate(user_encoder_loop_task, "user_encoder_loop_task", 3000, NULL, 2, NULL);
}

static void user_encoder_loop_task(void *arg)
{
  int8_t vol = 10;
  for(;;)
  {
    EventBits_t even = xEventGroupWaitBits(knob_even_,BIT_EVEN_ALL,pdTRUE,pdFALSE,pdMS_TO_TICKS(5000)); //等待WIFI 连接成功
    if(READ_BIT(even,0))
    {
      vol--;
      if(vol<=0)
      vol = 0;
      volume_adjustment(vol);
      ESP_LOGI(TAG,"vol:%d",vol);
    }
    if(READ_BIT(even,1))
    {
      vol++;
      if(vol>=50)
      vol = 50;
      volume_adjustment(vol);
      ESP_LOGI(TAG,"vol:%d",vol);
    }
  }
}