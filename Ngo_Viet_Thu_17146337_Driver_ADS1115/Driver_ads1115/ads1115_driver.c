#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define DRIVER_NAME "ads1115_driver" // Driver name for logging and I2C
#define CLASS_NAME  "ads1115"         // Class name in sysfs
#define DEVICE_NAME "ads1115"         // Device file name in /dev

// ADS1115 register addresses
#define ADS1115_REG_POINTER_CONVERSION  0x00 // ADC result register
#define ADS1115_REG_POINTER_CONFIG      0x01 // Configuration register

// Config register bits
#define ADS1115_CONFIG_OS_SINGLE        0x8000 // Start single conversion
#define ADS1115_CONFIG_MUX_OFFSET       12     // MUX bit offset
#define ADS1115_CONFIG_PGA_OFFSET       9      // PGA bit offset
#define ADS1115_CONFIG_MODE_SINGLE      0x0100 // Single-shot mode
#define ADS1115_CONFIG_DR_OFFSET        5      // Data rate bit offset

// MUX config for analog channels
#define ADS1115_MUX_AIN0_GND (0x04 << ADS1115_CONFIG_MUX_OFFSET) // Channel AIN0 vs GND
#define ADS1115_MUX_AIN1_GND (0x05 << ADS1115_CONFIG_MUX_OFFSET) // Channel AIN1 vs GND
#define ADS1115_MUX_AIN2_GND (0x06 << ADS1115_CONFIG_MUX_OFFSET) // Channel AIN2 vs GND
#define ADS1115_MUX_AIN3_GND (0x07 << ADS1115_CONFIG_MUX_OFFSET) // Channel AIN3 vs GND

// PGA and data rate config
#define ADS1115_PGA_4_096V (0x01 << ADS1115_CONFIG_PGA_OFFSET) // Gain +/-4.096V
#define ADS1115_DR_128SPS  (0x04 << ADS1115_CONFIG_DR_OFFSET)  // 128 samples/second

// Base config for Config register
#define ADS1115_CONFIG_BASE (ADS1115_CONFIG_OS_SINGLE | \
                             ADS1115_PGA_4_096V | \
                             ADS1115_CONFIG_MODE_SINGLE | \
                             ADS1115_DR_128SPS | \
                             0x0003) // Disable comparator

// IOCTL commands
#define ADS1115_IOCTL_MAGIC 'a' // IOCTL magic character
#define ADS1115_IOCTL_READ_AIN0 _IOR(ADS1115_IOCTL_MAGIC, 0, s16) // Read AIN0
#define ADS1115_IOCTL_READ_AIN1 _IOR(ADS1115_IOCTL_MAGIC, 1, s16) // Read AIN1
#define ADS1115_IOCTL_READ_AIN2 _IOR(ADS1115_IOCTL_MAGIC, 2, s16) // Read AIN2
#define ADS1115_IOCTL_READ_AIN3 _IOR(ADS1115_IOCTL_MAGIC, 3, s16) // Read AIN3

// Global variables
static struct i2c_client *ads1115_client; // I2C client for ADS1115
static struct class* ads1115_class = NULL; // Device class in sysfs
static struct device* ads1115_device = NULL; // Device node in /dev
static int major_number; // Major number for character device

// Read ADC value from a channel
static s16 ads1115_read_single_channel(struct i2c_client *client, u16 mux_config)
{
    u16 config_val;
    s16 raw_val;
    int ret;

    if (!client) {
        printk(KERN_ERR DRIVER_NAME ": Invalid I2C client\n");
        return -EIO;
    }

    config_val = ADS1115_CONFIG_BASE | mux_config;

    ret = i2c_smbus_write_word_data(client, ADS1115_REG_POINTER_CONFIG, swab16(config_val));
    if (ret < 0) {
        dev_err(&client->dev, "Config write error: %d\n", ret);
        return ret;
    }

    msleep(15); // Wait for ADC conversion (128SPS)

    ret = i2c_smbus_write_byte(client, ADS1115_REG_POINTER_CONVERSION);
    if (ret < 0) {
        dev_err(&client->dev, "Conversion register error: %d\n", ret);
        return ret;
    }

    raw_val = i2c_smbus_read_word_data(client, ADS1115_REG_POINTER_CONVERSION);
    if (raw_val < 0) {
        dev_err(&client->dev, "Read error: %d\n", raw_val);
        return raw_val;
    }

    return swab16(raw_val);
}

// Handle IOCTL commands
static long ads1115_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    s16 data;

    if (!ads1115_client) {
        printk(KERN_ERR DRIVER_NAME ": Client not initialized\n");
        return -ENODEV;
    }

    switch (cmd) {
        case ADS1115_IOCTL_READ_AIN0:
            data = ads1115_read_single_channel(ads1115_client, ADS1115_MUX_AIN0_GND);
            break;
        case ADS1115_IOCTL_READ_AIN1:
            data = ads1115_read_single_channel(ads1115_client, ADS1115_MUX_AIN1_GND);
            break;
        case ADS1115_IOCTL_READ_AIN2:
            data = ads1115_read_single_channel(ads1115_client, ADS1115_MUX_AIN2_GND);
            break;
        case ADS1115_IOCTL_READ_AIN3:
            data = ads1115_read_single_channel(ads1115_client, ADS1115_MUX_AIN3_GND);
            break;
        default:
            printk(KERN_WARNING DRIVER_NAME ": Invalid IOCTL command: %u\n", cmd);
            return -EINVAL;
    }

    // Check for I2C errors (heuristic for negative values)
    if (data < -1000 && (data == -EIO || data == -ETIMEDOUT || data == -ENXIO || data == -EBUSY)) {
        printk(KERN_ERR DRIVER_NAME ": ADC channel read error: %d\n", data);
        return data;
    }

    if (copy_to_user((s16 __user *)arg, &data, sizeof(data))) {
        printk(KERN_ERR DRIVER_NAME ": Copy to user error\n");
        return -EFAULT;
    }

    return 0;
}

static int ads1115_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO DRIVER_NAME ": Device opened\n");
    return 0;
}

static int ads1115_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO DRIVER_NAME ": Device closed\n");
    return 0;
}

// File operations for character device
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = ads1115_open,
    .unlocked_ioctl = ads1115_ioctl,
    .release = ads1115_release,
};

// I2C probe function
static int ads1115_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    ads1115_client = client;

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR DRIVER_NAME ": Major number registration failed: %d\n", major_number);
        return major_number;
    }

    ads1115_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(ads1115_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR DRIVER_NAME ": Class creation failed\n");
        return PTR_ERR(ads1115_class);
    }

    ads1115_device = device_create(ads1115_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(ads1115_device)) {
        class_destroy(ads1115_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR DRIVER_NAME ": Device creation failed\n");
        return PTR_ERR(ads1115_device);
    }

    return 0;
}

// I2C remove function
static int ads1115_i2c_remove(struct i2c_client *client)
{
    device_destroy(ads1115_class, MKDEV(major_number, 0));
    class_unregister(ads1115_class);
    class_destroy(ads1115_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    return 0;
}

// Device Tree match table
static const struct of_device_id ads1115_of_match[] = {
    { .compatible = "ti,ads1115" },
    { }
};
MODULE_DEVICE_TABLE(of, ads1115_of_match);

// I2C driver structure
static struct i2c_driver ads1115_driver = {
    .driver = {
        .name   = DRIVER_NAME,
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(ads1115_of_match),
    },
    .probe      = ads1115_i2c_probe,
    .remove     = ads1115_i2c_remove,
};

// Module initialization
static int __init ads1115_init(void) {
    return i2c_add_driver(&ads1115_driver);
}

// Module cleanup
static void __exit ads1115_exit(void) {
    i2c_del_driver(&ads1115_driver);
}

module_init(ads1115_init);
module_exit(ads1115_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ngo_Viet_Thu");
MODULE_DESCRIPTION("ADS1115 I2C Driver with IOCTL Interface");