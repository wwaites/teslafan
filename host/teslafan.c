/*
 * Tesla fan controller utility. The control device should be a character device
 * that accepts PWM values from 0-255. This utility measures the temperature on
 * the Tesla GPU and adjusts the PWM output accordingly.
 *
 * nvcc -o teslafan teslafan.c -lnvidia-ml
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <math.h>
#include <nvml.h>

#define PROGNAME "teslafan"
#define MIN(a, b) (a < b ? a : b)

void usage() {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s -h\n", PROGNAME);
    fprintf(stderr, "\t%s -c ctldev -g devid [-d] [-t target temp] [-w wait time]\n", PROGNAME);
}

nvmlReturn_t prInfo(int devidx, nvmlDevice_t handle) {
    nvmlReturn_t err = NVML_SUCCESS;
    char version[8], devname[16]; 

    if ((err = nvmlSystemGetDriverVersion(version, sizeof(version))) != NVML_SUCCESS) {
        syslog(LOG_ERR, "nvmlSystemGetDriverVersion(): %s", nvmlErrorString(err));
        return err;
    }

    if ((err = nvmlDeviceGetName(handle, devname, sizeof(devname))) != NVML_SUCCESS) {
        syslog(LOG_ERR, "nvmlDeviceGetName(): %s", nvmlErrorString(err));
        return err;
    }

    syslog(LOG_INFO, "Device %i is %s (driver version %s)", devidx, devname, version);

    return err;
}

int main(int argc, char *argv[]) {
    int opt, devidx = -1, ctl, ctlflags;
    int waittime = 10;
    unsigned char pwm = 64;
    float target = 35.0, error, k = 25.0;
    char *ctldev = NULL;
    nvmlDevice_t device;
    nvmlReturn_t err;
    unsigned int temp;
    int retval = 0;
    int debug = 0;
    int logflags = LOG_PERROR;

    while ((opt = getopt(argc, argv, "c:dg:hk:t:w:")) != -1) {
        switch(opt) {
        case 'c':
            ctldev = optarg;
            break;
        case 'd':
            debug = 1;
            break;
        case 'g':
            devidx = atoi(optarg);
            break;
        case 'h':
            usage();
            exit(EXIT_SUCCESS);
            break;
        case 'k':
            k = atof(optarg);
            break;
        case 't':
            target = atof(optarg);
            break;
        case 'w':
            waittime = atoi(optarg);
            break;
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if (devidx < 0) {
        usage();
        exit(EXIT_FAILURE);
    }

    if (ctldev == NULL) {
        usage();
        exit(EXIT_FAILURE);
    }

    if (debug) {
        logflags |= LOG_PERROR;
    }
    openlog(PROGNAME, logflags, LOG_DAEMON);

    // open the control port
    if ((ctl = open(ctldev, O_RDWR)) < 0) {
        syslog(LOG_ERR, "open(\"%s\"): %s", ctldev, strerror(errno));
        exit(EXIT_FAILURE);
    }

    ctlflags = TIOCM_DTR;
    if (ioctl(ctl, TIOCMBIC, &ctlflags) < 0) {
        syslog(LOG_ERR, "ioctl(TIOCMBIC, %04x): %s", ctlflags, strerror(errno));
        close(ctl);
        exit(EXIT_FAILURE);
    }


    // initialize NVML library
    if ((err = nvmlInit()) != NVML_SUCCESS) {
        syslog(LOG_ERR, "nvmlInit(): %s", nvmlErrorString(err));
        close(ctl);
        exit(EXIT_FAILURE);
    }

    if ((err = nvmlDeviceGetHandleByIndex(devidx, &device)) != NVML_SUCCESS) {
	syslog(LOG_ERR, "nvmlDeviceGetHandleByIndex(%i): %s", devidx, nvmlErrorString(err));
	nvmlShutdown();
        close(ctl);
        exit(EXIT_FAILURE);
    }

    if ((err = prInfo(devidx, device)) != NVML_SUCCESS) {
        nvmlShutdown();
        close(ctl);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Running with target = %.02fC, k = %.02f", target, k);

    for (;;) {
        if ((err = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp)) != NVML_SUCCESS) {
            syslog(LOG_ERR, "nvmlDeviceGetTemperature(%i): %s", devidx, nvmlErrorString(err));
            nvmlShutdown();
            close(ctl);
            exit(EXIT_FAILURE);
        }

        error = temp - target;
        float newpwm = 96 + k * error;
        if (newpwm > 255)
            newpwm = 255;
        if (newpwm < 0)
            newpwm = 0;
        if (abs(newpwm - pwm) > 1) {
            syslog(LOG_INFO, "Device %i temperature is %i. PWM setting is %i, changing to %f", devidx, temp, pwm, newpwm);
            pwm = newpwm;
            write(ctl, &pwm, 1);
            fsync(ctl);
        }

        sleep(waittime);
    }

    nvmlShutdown();
    close(ctl);
    exit(EXIT_SUCCESS);
}
