/**
 * @brief IMU one-time calibration tool.
 *
 * Run the robot stationary on a flat surface.
 * Prints the IMU offsets to paste into settings.xml <Offset> element.
 */
#include <cstdint>
#include <cstdio>
#include <spdlog/spdlog.h>

#include "ImuControl.h"
#include "Helper.h"

int main()
{
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::trace);

    // Stop doly service to avoid hardware conflict
    Helper::stopDolyService();

    // Initialize IMU with zero offsets first
    spdlog::info("Initializing IMU (no offsets)...");
    if (ImuControl::init(0) < 0) {
        spdlog::error("ImuControl::init failed - check i2c wiring (chip: LSM6DSR, addr: 0x6A, bus: i2c-1)");
        return -1;
    }

    // Calculate calibrated offsets
    // Keep the robot STILL and FLAT during this process
    spdlog::info("Calculating offsets - keep robot STILL and FLAT for ~5 seconds...");
    int16_t gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0;
    int8_t res = ImuControl::calculate_offsets(&gx, &gy, &gz, &ax, &ay, &az);
    if (res < 0) {
        spdlog::error("calculate_offsets failed with code: {}", res);
        ImuControl::dispose();
        return -2;
    }

    ImuControl::dispose();

    // Print the result
    printf("\n========= IMU Calibration Result =========\n");
    printf("Gx=%d  Gy=%d  Gz=%d\n", gx, gy, gz);
    printf("Ax=%d  Ay=%d  Az=%d\n", ax, ay, az);
    printf("\nPaste this into /.doly/config/settings.xml:\n");
    printf("    <Offset Gx=\"%d\" Gy=\"%d\" Gz=\"%d\" Ax=\"%d\" Ay=\"%d\" Az=\"%d\" TOFL=\"0\" TOFR=\"0\"/>\n",
           gx, gy, gz, ax, ay, az);
    printf("==========================================\n\n");

    return 0;
}
