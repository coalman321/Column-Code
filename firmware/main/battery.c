#include "battery.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "battery";

#define MAX17048_I2C_ADDR 0x36
#define MAX17048_REG_VCELL 0x02  /* Voltage cell register */
#define MAX17048_REG_SOC 0x04    /* State of charge register */

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_max17048_handle = NULL;

esp_err_t battery_init(void)
{
    /* Configure I2C bus */
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure MAX17048 device */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX17048_I2C_ADDR,
        .scl_speed_hz = 100000,  /* 100 kHz */
    };

    err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_config, &s_max17048_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MAX17048 battery monitor initialized");
    return ESP_OK;
}

esp_err_t battery_read(BatteryStatus *status)
{
    if (!s_max17048_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2];

    /* Read voltage cell (register 0x02, 2 bytes) */
    esp_err_t err = i2c_master_transmit_receive(s_max17048_handle,
                                                 (uint8_t[]){MAX17048_REG_VCELL}, 1,
                                                 data, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to read VCELL: %s", esp_err_to_name(err));
        return err;
    }

    /* VCELL = (MSB << 4 | LSB >> 4) * 1.25 mV */
    uint16_t vcell_raw = ((uint16_t)data[0] << 8) | data[1];
    vcell_raw = vcell_raw >> 4;  /* 12-bit value */
    status->voltage_mv = (vcell_raw * 125) / 100;  /* 1.25 mV per unit */

    /* Read SOC (register 0x04, 2 bytes) */
    err = i2c_master_transmit_receive(s_max17048_handle,
                                       (uint8_t[]){MAX17048_REG_SOC}, 1,
                                       data, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to read SOC: %s", esp_err_to_name(err));
        return err;
    }

    /* SOC = (MSB + LSB/256) * 1% */
    status->soc = data[0];  /* Integer part is the percentage */

    ESP_LOGD(TAG, "battery: %u%% @ %u mV", status->soc, status->voltage_mv);
    return ESP_OK;
}
