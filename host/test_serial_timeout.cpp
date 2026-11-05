/* The setup() guard in Sprinkles.ino, compiled both ways.
 * UART is what core 3.3.12 uses when USB CDC On Boot is disabled:
 * HardwareSerial has no setTxTimeoutMs. CDC is HWCDC or USBCDC, which do.
 */
#include <stdio.h>

#if defined(TEST_UART)
#define ARDUINO_USB_CDC_ON_BOOT 0
class HardwareSerial {
public:
    void begin(unsigned long) {}
};
HardwareSerial Serial;
#else
#define ARDUINO_USB_CDC_ON_BOOT 1
class HWCDC {
public:
    void begin(unsigned long) {}
    void setTxTimeoutMs(unsigned ms) { timeout_ms = ms; }
    unsigned timeout_ms;
};
HWCDC Serial;
#endif

static void setup_serial(void)
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
}

int main(void)
{
    setup_serial();
#if ARDUINO_USB_CDC_ON_BOOT
    if (Serial.timeout_ms != 0) {
        printf("cdc timeout %u\n", Serial.timeout_ms);
        return 1;
    }
    printf("cdc timeout 0\n");
#else
    printf("uart compiled\n");
#endif
    return 0;
}
