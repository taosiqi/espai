#include <Arduino.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

#include "adc_bsp.h"

static adc_cali_handle_t cali_handle = nullptr;
static adc_oneshot_unit_handle_t adc1_handle = nullptr;
static bool adc_ready = false;

void Adc_PortInit(void)
{
  adc_cali_curve_fitting_config_t cali_config = {};
  cali_config.unit_id = ADC_UNIT_1;
  cali_config.atten = ADC_ATTEN_DB_12;
  cali_config.bitwidth = ADC_BITWIDTH_12;

  adc_oneshot_unit_init_cfg_t init_config = {};
  init_config.unit_id = ADC_UNIT_1;

  adc_oneshot_chan_cfg_t channel_config = {};
  channel_config.bitwidth = ADC_BITWIDTH_12;
  channel_config.atten = ADC_ATTEN_DB_12;

  adc_ready = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK &&
              adc_oneshot_new_unit(&init_config, &adc1_handle) == ESP_OK &&
              adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &channel_config) == ESP_OK;
}

float Adc_GetBatteryVoltage(int *data)
{
  int raw = 0;
  int millivolts = 0;
  float voltage = 0.0f;

  if (adc_ready && adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &raw) == ESP_OK) {
    if (adc_cali_raw_to_voltage(cali_handle, raw, &millivolts) == ESP_OK) {
      voltage = 0.001f * millivolts * 3.0f;
    }
  }

  if (data) {
    *data = raw;
  }
  return voltage;
}

uint8_t Adc_GetBatteryLevel(void)
{
  float voltage = Adc_GetBatteryVoltage(nullptr);
  if (voltage < 3.0f) {
    return 0;
  }
  if (voltage > 4.12f) {
    return 100;
  }
  return (uint8_t)(((voltage - 3.0f) / 1.12f) * 100.0f);
}
