/**
 * Minimal test firmware to verify Pico hardware works
 * Just blinks the LED and outputs to USB serial
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main() {
    // Initialize standard I/O (USB serial)
    stdio_init_all();

    // Wait for USB to enumerate
    sleep_ms(2000);

    printf("\n=== Pico WH LED Blink Test ===\n");
    printf("Starting WiFi chip initialization...\n");

    // Initialize WiFi chip (needed to control the LED on Pico W/WH)
    if (cyw43_arch_init()) {
        printf("ERROR: WiFi chip init failed!\n");
        printf("LED will not work without WiFi chip.\n");
        // Infinite loop with messages
        while (1) {
            printf("STUCK: Cannot initialize cyw43 chip\n");
            sleep_ms(1000);
        }
    }

    printf("WiFi chip initialized successfully!\n");
    printf("Starting LED blink...\n");

    // Blink the LED forever
    int count = 0;
    while (1) {
        printf("Blink %d - LED ON\n", count);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);

        printf("Blink %d - LED OFF\n", count);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);

        count++;
    }

    return 0;
}
