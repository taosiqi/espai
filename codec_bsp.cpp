#include "codec_bsp.h"

#include <esp_log.h>
#include <string.h>

CodecPort::CodecPort(I2cMasterBus &i2cbus, const char *boardName)
    : i2cbus_(i2cbus)
{
  set_codec_board_type(boardName);
  codec_init_cfg_t codec_cfg = {};
  codec_cfg.in_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
  codec_cfg.in_use_tdm = false;
  codec_cfg.reuse_dev = false;
  ESP_ERROR_CHECK(init_codec(&codec_cfg));

  playback = get_playback_handle();
  record = get_record_handle();

  i2c_master_bus_handle_t bus = i2cbus_.Get_I2cBusHandle();
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.scl_speed_hz = 400000;

  dev_cfg.device_address = Es8311Address;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &I2c_DevEs8311));

  dev_cfg.device_address = Es7210Address;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &I2c_DevEs7210));
}

CodecPort::~CodecPort()
{
}

void CodecPort::setInfo(const char *name, bool open, int sampleRate, int channels, int bitsPerSample)
{
  esp_codec_dev_sample_info_t info = {};
  info.sample_rate = sampleRate;
  info.channel = channels;
  info.bits_per_sample = bitsPerSample;
  if (!open) {
    return;
  }
  if (!strcmp(name, "es8311")) {
    esp_codec_dev_open(playback, &info);
  } else if (!strcmp(name, "es7210")) {
    esp_codec_dev_open(record, &info);
  } else {
    esp_codec_dev_open(playback, &info);
    esp_codec_dev_open(record, &info);
  }
}

void CodecPort::setSpeakerVol(int vol)
{
  esp_codec_dev_set_out_vol(playback, vol);
}

void CodecPort::setMicGain(float dbValue)
{
  esp_codec_dev_set_in_gain(record, dbValue);
}

int CodecPort::playWrite(void *ptr, int ptrLen)
{
  return esp_codec_dev_write(playback, ptr, ptrLen);
}

int CodecPort::recordRead(void *ptr, int ptrLen)
{
  return esp_codec_dev_read(record, ptr, ptrLen);
}
