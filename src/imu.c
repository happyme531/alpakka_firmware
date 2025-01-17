// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Input Labs Oy.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include "imu.h"
#include "config.h"
#include "helper.h"
#include "pin.h"
#include "bus.h"
#include "touch.h"
#include "hid.h"
#include "led.h"
#include "helper.h"

double offset_gyro_0_x;
double offset_gyro_0_y;
double offset_gyro_0_z;
double offset_gyro_1_x;
double offset_gyro_1_y;
double offset_gyro_1_z;
double offset_accel_0_x;
double offset_accel_0_y;
double offset_accel_0_z;
double offset_accel_1_x;
double offset_accel_1_y;
double offset_accel_1_z;

void imu_init_single(uint8_t cs, uint8_t gyro_conf) {
    uint8_t id = bus_spi_read_one(cs, IMU_WHO_AM_I);
    bus_spi_write(cs, IMU_CTRL1_XL, IMU_CTRL1_XL_2G);
    bus_spi_write(cs, IMU_CTRL8_XL, IMU_CTRL8_XL_LP);
    bus_spi_write(cs, IMU_CTRL2_G, gyro_conf);
    uint8_t xl = bus_spi_read_one(cs, IMU_CTRL1_XL);
    uint8_t g = bus_spi_read_one(cs, IMU_CTRL2_G);
    printf("  IMU cs=%i id=0x%02x xl=0x%08i g=0x%08i\n", cs, id, bin(xl), bin(g));
}

void imu_init() {
    printf("INIT: IMU\n");
    imu_init_single(PIN_SPI_CS0, IMU_CTRL2_G_500);
    imu_init_single(PIN_SPI_CS1, IMU_CTRL2_G_125);
    config_nvm_t config;
    config_read(&config);
    offset_gyro_0_x = config.offset_gyro_0_x;
    offset_gyro_0_y = config.offset_gyro_0_y;
    offset_gyro_0_z = config.offset_gyro_0_z;
    offset_gyro_1_x = config.offset_gyro_1_x;
    offset_gyro_1_y = config.offset_gyro_1_y;
    offset_gyro_1_z = config.offset_gyro_1_z;
    offset_accel_0_x = config.offset_accel_0_x;
    offset_accel_0_y = config.offset_accel_0_y;
    offset_accel_0_z = config.offset_accel_0_z;
    offset_accel_1_x = config.offset_accel_1_x;
    offset_accel_1_y = config.offset_accel_1_y;
    offset_accel_1_z = config.offset_accel_1_z;
}

Vector imu_read_gyro_bits(uint8_t cs) {
    uint8_t buf[6];
    bus_spi_read(cs, IMU_OUTX_L_G, buf, 6);
    int16_t y =  (((int8_t)buf[1] << 8) + (int8_t)buf[0]);
    int16_t z =  (((int8_t)buf[3] << 8) + (int8_t)buf[2]);
    int16_t x = -(((int8_t)buf[5] << 8) + (int8_t)buf[4]);
    double offset_x = (cs==PIN_SPI_CS0) ? offset_gyro_0_x : offset_gyro_1_x;
    double offset_y = (cs==PIN_SPI_CS0) ? offset_gyro_0_y : offset_gyro_1_y;
    double offset_z = (cs==PIN_SPI_CS0) ? offset_gyro_0_z : offset_gyro_1_z;
    return (Vector){
        (double)x - offset_x,
        (double)y - offset_y,
        (double)z - offset_z,
    };
}

Vector imu_read_accel_bits(uint8_t cs) {
    uint8_t buf[6];
    bus_spi_read(cs, IMU_OUTX_L_XL, buf, 6);
    int16_t x =  (((int8_t)buf[1] << 8) + (int8_t)buf[0]);
    int16_t y =  (((int8_t)buf[3] << 8) + (int8_t)buf[2]);
    int16_t z =  (((int8_t)buf[5] << 8) + (int8_t)buf[4]);
    double offset_x = (cs==PIN_SPI_CS0) ? offset_accel_0_x : offset_accel_1_x;
    double offset_y = (cs==PIN_SPI_CS0) ? offset_accel_0_y : offset_accel_1_y;
    double offset_z = (cs==PIN_SPI_CS0) ? offset_accel_0_z : offset_accel_1_z;
    return (Vector){
        (double)x - offset_x,
        (double)y - offset_y,
        (double)z - offset_z,
    };
}

Vector imu_read_gyro_burst(uint8_t cs, uint8_t samples) {
    double x = 0;
    double y = 0;
    double z = 0;
    for(uint8_t i=0; i<samples; i++) {
        Vector sample = imu_read_gyro_bits(cs);
        x += sample.x;
        y += sample.y;
        z += sample.z;
    }
    x /= samples;
    y /= samples;
    z /= samples;
    return (Vector){x, y, z};
}

Vector imu_read_gyro() {
    Vector imu0 = imu_read_gyro_burst(PIN_SPI_CS0, CFG_IMU_TICK_SAMPLES/8*1);
    Vector imu1 = imu_read_gyro_burst(PIN_SPI_CS1, CFG_IMU_TICK_SAMPLES/8*7);
    double weight = max(abs(imu1.x), abs(imu1.y)) / 32768.0;
    double weight_0 = ramp_mid(weight, 0.2);
    double weight_1 = 1 - weight_0;
    double x = (imu0.x * weight_0) + (imu1.x * weight_1 / 4);
    double y = (imu0.y * weight_0) + (imu1.y * weight_1 / 4);
    double z = (imu0.z * weight_0) + (imu1.z * weight_1 / 4);
    return (Vector){x, y, z};
}

Vector imu_read_accel() {
    Vector accel0 = imu_read_accel_bits(PIN_SPI_CS0);
    Vector accel1 = imu_read_accel_bits(PIN_SPI_CS1);
    return (Vector){
        (accel0.x + accel1.x) / 2,
        (accel0.y + accel1.y) / 2,
        (accel0.z + accel1.z) / 2
    };
}

Vector imu_calibrate_single(uint8_t cs, bool mode, double* x, double* y, double* z) {
    char *mode_str = mode ? "accel" : "gyro";
    printf("IMU: cs=%i calibrating %s...", cs, mode_str);
    *x = 0;
    *y = 0;
    *z = 0;
    double tx = 0;
    double ty = 0;
    double tz = 0;
    uint32_t i = 0;
    while(i < CFG_IMU_CALIBRATION_SAMPLES) {
        if (!(i % IMU_CALIBRATION_BLINK_FREQ)) led_cycle_step();
        Vector sample = mode ? imu_read_accel_bits(cs) : imu_read_gyro_bits(cs);
        tx += sample.x;
        ty += sample.y;
        tz += sample.z;
        i++;
    }
    *x = tx / CFG_IMU_CALIBRATION_SAMPLES;
    *y = ty / CFG_IMU_CALIBRATION_SAMPLES;
    *z = tz / CFG_IMU_CALIBRATION_SAMPLES;
    // Assuming the resting state of the controller is having a vector of 1G
    // pointing down. (Newton's fault for inventing the gravity /jk).
    if (mode==1) *z -= BIT_14;
    printf("\rIMU: cs=%i %s calibration x=%f y=%f z=%f\n", cs, mode_str, *x, *y, *z);
}

void imu_calibrate() {
    imu_calibrate_single(PIN_SPI_CS0, 0, &offset_gyro_0_x, &offset_gyro_0_y, &offset_gyro_0_z);
    imu_calibrate_single(PIN_SPI_CS1, 0, &offset_gyro_1_x, &offset_gyro_1_y, &offset_gyro_1_z);
    imu_calibrate_single(PIN_SPI_CS0, 1, &offset_accel_0_x, &offset_accel_0_y, &offset_accel_0_z);
    imu_calibrate_single(PIN_SPI_CS1, 1, &offset_accel_1_x, &offset_accel_1_y, &offset_accel_1_z);
    config_set_gyro_offset(
        offset_gyro_0_x,
        offset_gyro_0_y,
        offset_gyro_0_z,
        offset_gyro_1_x,
        offset_gyro_1_y,
        offset_gyro_1_z
    );
    config_set_accel_offset(
        offset_accel_0_x,
        offset_accel_0_y,
        offset_accel_0_z,
        offset_accel_1_x,
        offset_accel_1_y,
        offset_accel_1_z
    );
    printf("Calibration done\n");
}
