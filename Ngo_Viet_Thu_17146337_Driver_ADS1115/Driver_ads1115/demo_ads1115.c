#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

// IOCTL commands
#define ADS1115_IOCTL_MAGIC 'a'
#define ADS1115_IOCTL_READ_AIN0 _IOR(ADS1115_IOCTL_MAGIC, 0, short)
#define ADS1115_IOCTL_READ_AIN1 _IOR(ADS1115_IOCTL_MAGIC, 1, short)
#define ADS1115_IOCTL_READ_AIN2 _IOR(ADS1115_IOCTL_MAGIC, 2, short)
#define ADS1115_IOCTL_READ_AIN3 _IOR(ADS1115_IOCTL_MAGIC, 3, short)

#define DEVICE_PATH "/dev/ads1115"

// Convert ADC to voltage (PGA ±4.096V)
float adc_to_vol(short adc_value) {
    return (adc_value * 4.096) / 32768.0;
}

int main() {
    int fd;
    short data;

    // Open the device
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open the device");
        return errno;
    }

    // Read AIN0 data
    if (ioctl(fd, ADS1115_IOCTL_READ_AIN0, &data) < 0) {
        perror("Failed to read AIN0 data");
        close(fd);
        return errno;
    }
    printf("AIN0: ADC=%d, Voltage=%.3f V\n", data, adc_to_vol(data));

    // Read AIN1 data
    if (ioctl(fd, ADS1115_IOCTL_READ_AIN1, &data) < 0) {
        perror("Failed to read AIN1 data");
        close(fd);
        return errno;
    }
    printf("AIN1: ADC=%d, Voltage=%.3f V\n", data, adc_to_vol(data));

    // Read AIN2 data
    if (ioctl(fd, ADS1115_IOCTL_READ_AIN2, &data) < 0) {
        perror("Failed to read AIN2 data");
        close(fd);
        return errno;
    }
    printf("AIN2: ADC=%d, Voltage=%.3f V\n", data, adc_to_vol(data));

    // Read AIN3 data
    if (ioctl(fd, ADS1115_IOCTL_READ_AIN3, &data) < 0) {
        perror("Failed to read AIN3 data");
        close(fd);
        return errno;
    }
    printf("AIN3: ADC=%d, Voltage=%.3f V\n", data, adc_to_vol(data));

    // Close the device
    close(fd);
    return 0;
}