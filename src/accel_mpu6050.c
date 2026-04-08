#include "accel.h"

#include "config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define REG_PWR_MGMT_1 0x6BU
#define REG_ACCEL_XOUT_H 0x3BU
#define REG_WHO_AM_I 0x75U

static int write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_write_blocking(I2C_INST, MPU6050_ADDR, buf, 2, false) == 2 ? 0 : -1;
}

static int read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    if (i2c_write_blocking(I2C_INST, MPU6050_ADDR, &reg, 1, true) != 1) {
        return -1;
    }
    return i2c_read_blocking(I2C_INST, MPU6050_ADDR, dst, len, false) == (int)len ? 0 : -1;
}

bool accel_init(void) {
    i2c_init(I2C_INST, 400 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    sleep_ms(10);

    uint8_t who = 0;
    if (read_regs(REG_WHO_AM_I, &who, 1) != 0) {
        return false;
    }
    if (who != 0x68U && who != 0x98U) {
        return false;
    }

    if (write_reg(REG_PWR_MGMT_1, 0x00U) != 0) {
        return false;
    }
    return true;
}

bool accel_read_g(float *ax, float *ay, float *az) {
    uint8_t raw[6];
    if (read_regs(REG_ACCEL_XOUT_H, raw, 6) != 0) {
        return false;
    }
    int16_t x = (int16_t)(((int16_t)raw[0] << 8) | raw[1]);
    int16_t y = (int16_t)(((int16_t)raw[2] << 8) | raw[3]);
    int16_t z = (int16_t)(((int16_t)raw[4] << 8) | raw[5]);
    const float scale = 1.0f / 16384.0f;
    *ax = (float)x * scale;
    *ay = (float)y * scale;
    *az = (float)z * scale;
    return true;
}
