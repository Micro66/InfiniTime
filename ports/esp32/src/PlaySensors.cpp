#include "PlaySensors.h"
#include "PlayLogic.h"
#include <Arduino.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>
#include <driver/i2s.h>
#include "es7210.h"

namespace {
  SensorQMI8658 imu;
  bool motionOn = false, imuReady = false;
  Esp32::MotionSample motion;
  Esp32::AudioSample audio;
  bool microphone = false, codecOn = false;
  uint32_t lastMotion = 0, lastAudio = 0;
}

namespace Esp32 {
  bool PlaySensors::MotionStart() {
    if (!imuReady) {
      imuReady = imu.begin(Wire, 0x6b, -1, -1);
      if (!imuReady)
        return false;
      imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    }
    motionOn = imu.enableAccelerometer();
    motion.valid = false;
    return motionOn;
  }

  void PlaySensors::MotionStop() {
    if (motionOn)
      imu.disableAccelerometer();
    motionOn = false;
    motion.valid = false;
  }

  bool PlaySensors::AudioStart() {
    if (microphone)
      return true;
    audio = {};
    audio_hal_codec_config_t codec {};
    codec.adc_input = AUDIO_HAL_ADC_INPUT_ALL;
    codec.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
    codec.i2s_iface = {AUDIO_HAL_MODE_SLAVE, AUDIO_HAL_I2S_NORMAL, AUDIO_HAL_16K_SAMPLES, AUDIO_HAL_BIT_LENGTH_16BITS};
    codecOn = true;
    if (es7210_adc_init(&Wire, &codec) != ESP_OK || es7210_adc_config_i2s(codec.codec_mode, &codec.i2s_iface) != ESP_OK ||
        es7210_mic_select(ES7210_INPUT_MIC1) != ESP_OK || es7210_adc_set_gain(ES7210_INPUT_MIC1, GAIN_37_5DB) != ESP_OK) {
      AudioStop();
      return false;
    }
    es7210_adc_ctrl_state(codec.codec_mode, AUDIO_HAL_CTRL_START);
    // The 1.75C schematic connects its microphones to ADC1/2 and GPIO10 to
    // SDOUT1. Mono-left selects ADC1; ADC3/4 are the auxiliary/AEC path.
    // Use the manufacturer's ES7210 I2S path. In this SDK, legacy I2S keeps
    // its GDMA callback context in internal RAM, as required by IRAM-safe GDMA.
    i2s_config_t config {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = 16000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_desc_num = 8;
    config.dma_frame_num = 256;
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
    i2s_pin_config_t pins {};
    pins.mck_io_num = 16;
    pins.bck_io_num = 9;
    pins.ws_io_num = 45;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = 10;
    microphone = i2s_driver_install(I2S_NUM_1, &config, 0, nullptr) == ESP_OK;
    if (!microphone || i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) {
      AudioStop();
      return false;
    }
    return true;
  }

  void PlaySensors::AudioStop() {
    if (microphone) {
      i2s_stop(I2S_NUM_1);
      i2s_driver_uninstall(I2S_NUM_1);
      microphone = false;
    }
    if (codecOn) {
      es7210_adc_ctrl_state(AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_STOP);
      codecOn = false;
    }
    audio.valid = false;
  }

  void PlaySensors::Tick() {
    const uint32_t now = millis();
    if (motionOn && now - lastMotion >= 15) {
      lastMotion = now;
      if (imu.getDataReady() && imu.getAccelerometer(motion.x, motion.y, motion.z)) {
        motion.valid = true;
        ++motion.serial;
      }
    }
    if (microphone) {
      int16_t samples[256], next[256];
      size_t bytes = 0;
      // Drain queued DMA data with bounded work; the most recent complete block wins.
      for (unsigned i = 0; i < 8; ++i) {
        size_t got = 0;
        if (i2s_read(I2S_NUM_1, next, sizeof(next), &got, 0) != ESP_OK || got != sizeof(next))
          break;
        memcpy(samples, next, sizeof(samples));
        bytes = got;
      }
      if (bytes && now - lastAudio >= 30) {
        lastAudio = now;
        audio.bands = Spectrum(samples, 256);
        double energy = 0, mean = 0;
        for (auto value : samples)
          mean += value;
        mean /= 256;
        for (auto value : samples)
          energy += (value - mean) * (value - mean);
        audio.rms = std::sqrt(energy / 256) / 32768;
        ++audio.blocks;
        audio.valid = true;
      }
    }
  }

  MotionSample PlaySensors::Motion() {
    return motion;
  }

  AudioSample PlaySensors::Audio() {
    return audio;
  }
}
