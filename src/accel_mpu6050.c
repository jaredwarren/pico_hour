/*
 * accel_mpu6050.c — MPU-6050 accelerometer over I2C
 *
 * I2C basics (why two writes for a read):
 *  - Address phase: we tell the chip which register we care about.
 *  - For a read, the Pico SDK does a “write register pointer” with repeated start, then read bytes.
 *
 * The MPU-6050 powers on in sleep mode; clearing PWR_MGMT_1 wakes the accelerometer.
 * WHO_AM_I lets us confirm the chip responded (0x68 is classic MPU-6050; 0x98 appears on some clones).
 */

#include "accel.h"

#include "config.h"        /* I2C_INST, pins, MPU6050_ADDR */
#include "hardware/i2c.h"  /* i2c_init, i2c_write_blocking, i2c_read_blocking */
#include "pico/stdlib.h"   /* sleep_ms, gpio_set_function, gpio_pull_up */

/* Register map excerpts from InvenSense datasheet (hex addresses). */
#define REG_PWR_MGMT_1 0x6BU
#define REG_ACCEL_XOUT_H 0x3BU /* burst read from here gets X,Y,Z high bytes first */
#define REG_WHO_AM_I 0x75U

/*
 * Write one register. Returns 0 on success, -1 on I2C failure.
 * buf[0] = register, buf[1] = value — typical I2C device protocol.
 */
static int write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    /* false = stop condition after write (release bus). */
    return i2c_write_blocking(I2C_INST, MPU6050_ADDR, buf, 2, false) == 2 ? 0 : -1;
}

/*
 * Read len bytes starting at reg. Returns 0 on success.
 * true after the 1-byte write = “repeated start” then read (I2C combined transaction).
 */
static int read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    if (i2c_write_blocking(I2C_INST, MPU6050_ADDR, &reg, 1, true) != 1) {
        return -1;
    }
    return i2c_read_blocking(I2C_INST, MPU6050_ADDR, dst, len, false) == (int)len ? 0 : -1;
}

bool accel_init(void) {
    /* 400 kHz is standard fast-mode I2C; pull-ups on SDA/SCL required (often on breakout). */
    i2c_init(I2C_INST, 400 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    sleep_ms(10); /* let power and lines settle after power-up */

    uint8_t who = 0;
    if (read_regs(REG_WHO_AM_I, &who, 1) != 0) {
        return false; /* no ACK — wrong wiring, wrong address, or no power */
    }
    if (who != 0x68U && who != 0x98U) {
        return false; /* unexpected chip */
    }

    /* 0x00 in PWR_MGMT_1 clears sleep bit (register default on some boards is sleep). */
    if (write_reg(REG_PWR_MGMT_1, 0x00U) != 0) {
        return false;
    }
    return true;
}

/*
 * Read accelerometer and convert to “g” (approximate).
 *
 * Raw values are 16-bit signed, big-endian in the register file (high byte first).
 * Default full scale ±2g → 16384 LSB per g (datasheet). If you change FS_SEL in config registers,
 * this scale factor must change too.
 */
bool accel_read_g(float *ax, float *ay, float *az) {
    uint8_t raw[6];
    if (read_regs(REG_ACCEL_XOUT_H, raw, 6) != 0) {
        return false;
    }

    /*
     * Assemble int16 from two bytes: promote to int16 before shift so sign extension is correct.
     * (If you used uint16_t only, negative values would break.)
     */
    int16_t x = (int16_t)(((int16_t)raw[0] << 8) | raw[1]);
    int16_t y = (int16_t)(((int16_t)raw[2] << 8) | raw[3]);
    int16_t z = (int16_t)(((int16_t)raw[4] << 8) | raw[5]);
    const float scale = 1.0f / 16384.0f;
    *ax = (float)x * scale;
    *ay = (float)y * scale;
    *az = (float)z * scale;
    return true;
}
