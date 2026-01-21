/**
 * Raspberry Pi Pico WH - LED Blink Test
 *
 * Minimal test firmware to verify Pico WH hardware is working correctly.
 * This program:
 * - Initializes USB serial output
 * - Initializes the WiFi chip (needed to control the onboard LED)
 * - Blinks the onboard LED every 500ms
 * - Outputs status messages to USB serial
 *
 * License: Apache 2.0
 * Copyright 2026 K3s Pico Node Project
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main() {
    // Initialize standard I/O (USB serial at 115200 baud)
    stdio_init_all();

    // Wait for USB to enumerate (2 seconds)
    sleep_ms(2000);

    printf("\n");
    printf("========================================\n");
    printf("  Pico WH LED Blink Test\n");
    printf("========================================\n");
    printf("Firmware: v1.0\n");
    printf("Board: Raspberry Pi Pico WH\n");
    printf("Purpose: Hardware verification\n");
    printf("========================================\n");
    printf("\n");

    printf("Initializing WiFi chip...\n");

    // Initialize WiFi chip (required to control the onboard LED on Pico W/WH)
    if (cyw43_arch_init()) {
        printf("ERROR: WiFi chip initialization failed!\n");
        printf("The LED cannot be controlled without the WiFi chip.\n");
        printf("\n");
        printf("Possible causes:\n");
        printf("  1. Hardware failure\n");
        printf("  2. Incorrect board type (must be Pico W or WH)\n");
        printf("  3. Firmware built for wrong board\n");
        printf("\n");

        // Infinite loop with error messages
        while (1) {
            printf("STUCK: Cannot initialize cyw43 chip\n");
            sleep_ms(5000);
        }
    }

    printf("WiFi chip initialized successfully!\n");
    printf("Starting LED blink sequence...\n");
    printf("\n");

    // Blink the LED forever
    int count = 0;
    bool led_on = false;

    while (1) {
        // Toggle LED state
        led_on = !led_on;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);

        // Print status
        if (led_on) {
            printf("[%d] LED ON\n", count);
        } else {
            printf("[%d] LED OFF\n", count);
            count++;
        }

        // Wait 500ms
        sleep_ms(500);

        // Print heartbeat every 10 blinks
        if (count > 0 && count % 10 == 0) {
            printf("--- Heartbeat: %d blinks completed ---\n", count);
        }
    }

    // Cleanup (never reached)
    cyw43_arch_deinit();
    return 0;
}
