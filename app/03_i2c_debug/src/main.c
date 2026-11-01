/* ============================================================================
 * File        : main.c
 * Description : Heltec ESP32 I2C bus diagnostic
 * Board       : Heltec WiFi LoRa 32 V2
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#define I2C_NODE         DT_NODELABEL(i2c0)
#define OLED_I2C_ADDRESS (0x3C)
#define I2C_NODE         DT_NODELABEL(i2c0)

static const struct gpio_dt_spec sda = GPIO_DT_SPEC_GET(I2C_NODE, sda_gpios);

static const struct gpio_dt_spec scl = GPIO_DT_SPEC_GET(I2C_NODE, scl_gpios);
int main(void)
{
    const struct device* const i2c_dev = DEVICE_DT_GET(I2C_NODE);
    int status;

    printk("========================================\n");
    printk(" Heltec I2C Diagnostic\n");
    printk("========================================\n");

    if(!device_is_ready(i2c_dev))
    {
        printk("ERROR: I2C controller not ready\n");
        return -ENODEV;
    }

    printk("I2C controller ready: %s\n", i2c_dev->name);

    if(!gpio_is_ready_dt(&sda) || !gpio_is_ready_dt(&scl))
    {
        printk("ERROR: GPIO controller not ready\n");
        return -ENODEV;
    }

    /*
     * Read GPIO input levels without reconfiguring the pins.
     *
     * The I2C driver already configures their pin multiplexing.
     */
    int sda_level = gpio_pin_get_raw(sda.port, sda.pin);
    int scl_level = gpio_pin_get_raw(scl.port, scl.pin);

    printk("BEFORE I2C: SDA=%d, SCL=%d\n", sda_level, scl_level);

    status = i2c_write(i2c_dev, NULL, 0, OLED_I2C_ADDRESS);

    printk("I2C result: %d\n", status);

    sda_level = gpio_pin_get_raw(sda.port, sda.pin);
    scl_level = gpio_pin_get_raw(scl.port, scl.pin);

    printk("AFTER I2C: SDA=%d, SCL=%d\n", sda_level, scl_level);
    // /*
    //  * A zero-length write tests address acknowledgement without
    //  * intentionally changing OLED registers.
    //  *
    //  * Note: Not every I2C controller supports zero-length messages.
    //  */
    // status = i2c_write(i2c_dev, NULL, 0, OLED_I2C_ADDRESS);

    // printk("OLED address 0x3C probe returned: %d\n", status);

    return 0;
}