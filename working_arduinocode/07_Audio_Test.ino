#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "bidi_switch_knob.h"
#include "esp_log.h"
#include "esp_err.h"
#include "user_config.h"
#include "audio_bsp.h"

static const char *TAG = "encoder";

EventGroupHandle_t knob_even_ = NULL;

static knob_handle_t s_knob = 0;

static void _knob_left_cb(void *arg, void *data)
{
  uint8_t eventBits_ = 0;
  SET_BIT(eventBits_,0);
  xEventGroupSetBits(knob_even_,eventBits_);
}
static void _knob_right_cb(void *arg, void *data)
{
  uint8_t eventBits_ = 0;
  SET_BIT(eventBits_,1);
  xEventGroupSetBits(knob_even_,eventBits_);
}
void setup()
{
  Serial.begin(115200);
  knob_even_ = xEventGroupCreate();
  // create knob
  knob_config_t cfg = 
  {
    .gpio_encoder_a = EXAMPLE_ENCODER_ECA_PIN,
    .gpio_encoder_b = EXAMPLE_ENCODER_ECB_PIN,
  };
  s_knob = iot_knob_create(&cfg);
  if(NULL == s_knob)
  {
    Serial.println("knob create failed");
  }
  ESP_ERROR_CHECK(iot_knob_register_cb(s_knob, KNOB_LEFT, _knob_left_cb, NULL));
  ESP_ERROR_CHECK(iot_knob_register_cb(s_knob, KNOB_RIGHT, _knob_right_cb, NULL));
  audio_bsp_init();
  Serial.println("audio start");
  xTaskCreate(user_encoder_loop_task, "user_encoder_loop_task", 3000, NULL, 2, NULL);
}

static void user_encoder_loop_task(void *arg)
{
  int8_t vol = 10;
  for(;;)
  {
    EventBits_t even = xEventGroupWaitBits(knob_even_,BIT_EVEN_ALL,pdTRUE,pdFALSE,pdMS_TO_TICKS(5000)); //等待事件发生
    if(READ_BIT(even,0))
    {
      vol--;
      if(vol<=0)
      vol = 0;
      volume_adjustment(vol);
      Serial.print("vol:");
      Serial.println(vol);
    }
    if(READ_BIT(even,1))
    {
      vol++;
      if(vol>=50)
      vol = 50;
      volume_adjustment(vol);
      Serial.print("vol:");
      Serial.println(vol);
    }
  }
}

void loop()
{

}
