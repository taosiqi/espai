#pragma once

#include "i2c_bsp.h"
#include "src/ExternLib/codec_board/codec_board.h"
#include "src/ExternLib/codec_board/codec_init.h"

class CodecPort {
private:
  esp_codec_dev_handle_t playback = NULL;
  esp_codec_dev_handle_t record = NULL;
  I2cMasterBus &i2cbus_;
  i2c_master_dev_handle_t I2c_DevEs8311;
  i2c_master_dev_handle_t I2c_DevEs7210;
  const uint8_t Es8311Address = 0x18;
  const uint8_t Es7210Address = 0x40;

public:
  CodecPort(I2cMasterBus &i2cbus, const char *boardName);
  ~CodecPort();

  void setInfo(const char *name, bool open, int sampleRate, int channels, int bitsPerSample);
  void setSpeakerVol(int vol);
  void setMicGain(float dbValue);
  int playWrite(void *ptr, int ptrLen);
  int recordRead(void *ptr, int ptrLen);
};
