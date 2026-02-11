#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/*
LoopbackMode: A loopback mode that can play back the sound received by the microphone.
PlaybackMusicMode: A music playback mode that can directly play PCM-format audio.
The rotary encoder adjusts the audio playback volume.
*/
#define LoopbackMode         0
#define PlaybackMusicmode    1

#define AudioMode  LoopbackMode

//i2s
#define EXAMPLE_I2S_STD_BCLK_PIN    (gpio_num_t)39//(gpio_num_t)48//(gpio_num_t)39    // I2S bit clock io number
#define EXAMPLE_I2S_STD_WS_PIN      (gpio_num_t)40//(gpio_num_t)38//(gpio_num_t)40  // I2S word select io number
#define EXAMPLE_I2S_STD_DOUT_PIN    (gpio_num_t)41//(gpio_num_t)47//(gpio_num_t)41   // I2S data out io number


#define EXAMPLE_I2S_PDM_DATA_PIN    (gpio_num_t)46
#define EXAMPLE_I2S_PDM_CLK_PIN     (gpio_num_t)45


//encoder 

#define EXAMPLE_ENCODER_ECA_PIN    8
#define EXAMPLE_ENCODER_ECB_PIN    7


//bit

#define SET_BIT(reg,bit) (reg |= ((uint32_t)0x01<<bit))
#define CLEAR_BIT(reg,bit) (reg &= (~((uint32_t)0x01<<bit)))
#define READ_BIT(reg,bit) (((uint32_t)reg>>bit) & 0x01)
#define BIT_EVEN_ALL (0x00ffffff)

#endif