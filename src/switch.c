/*
 * switch.c
 * Raspberry Pi GPIO for use with Mausberry switches.
 * Lots of code adapted from http://elinux.org/RPi_Low-level_peripherals
 */

#include <errno.h>
#include <fcntl.h>
#include <libconfig.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define IN  0
#define OUT 1

#define LOW  0
#define HIGH 1

#define PIN  24
#define POUT 23

#define BUFSZ 64

void SignalHandler(int sig)
{
    syslog(LOG_NOTICE, "Caught signal %d, terminating mausberry-switch daemon.", sig);
    exit(EXIT_SUCCESS);
}

int GPIOExport(int pin)
{
    char buffer[BUFSZ] = {0};
    size_t to_write;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open export for writing: %m\n");
        return -1;
    }

    to_write = (size_t)snprintf(buffer, BUFSZ, "%d", pin);
    write(fd, buffer, to_write);
    close(fd);
    return 0;
}

int GPIOUnexport(int pin)
{
    char buffer[BUFSZ] = {0};
    size_t to_write;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open unexport for writing: %m\n");
        return -1;
    }

    to_write = (size_t)snprintf(buffer, BUFSZ, "%d", pin);
    write(fd, buffer, to_write);
    close(fd);
    return 0;
}

int GPIODirection(int pin, int dir)
{
    const char s_directions_str[]  = "in\0out";

    char path[BUFSZ] = {0};
    int fd;

    snprintf(path, BUFSZ, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open gpio direction for writing: %m\n");
        return -1;
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
        syslog(LOG_WARNING, "Failed to set gpio direction: %m\n");
        return -1;
    }

    close(fd);
    return 0;
}

int GPIOInterrupt(int pin)
{
    char path[BUFSZ] = {0};
    const char when_to_return[] = "both";
    int fd;

    snprintf(path, BUFSZ, "/sys/class/gpio/gpio%d/edge", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open gpio edge for writing: %m\n");
        return -1;
    }

    if (-1 == write(fd, when_to_return, 4)) {
        syslog(LOG_WARNING, "Failed to configure gpio as interrupt source: %m\n");
        return -1;
    }

    close(fd);
    return 0;
}

int GPIOWait(int pin)
{
    int value = -1;
    char path[BUFSZ] = {0};
    char value_str[BUFSZ] = {0};
    int fd;

    //open gpio file descriptor
    snprintf(path, BUFSZ, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open gpio value for reading: %m\n");
        return -1;
    }

    //wait for kernel to notify us of changes
    struct pollfd pfds[1];
    pfds[0].fd = fd;
    pfds[0].events = POLLPRI | POLLERR;

    for (;;) {

        int rc = poll(pfds, 1, -1);
        if(rc < 0) {
            int errsv = errno;
            if(errsv != EAGAIN && errsv != EINTR && errsv != EINVAL) {
                syslog(LOG_WARNING, "An error occurred while polling the switch: %m\n");
                return -1;
            }
        }

        if(pfds[0].revents & POLLPRI) {
            lseek(fd, 0, SEEK_SET);
            //read the value
            if (-1 == read(fd, value_str, BUFSZ)) {
                syslog(LOG_WARNING, "Failed to read switch value: %m\n");
                return -1;
            }
            value = atoi(value_str);
            if (value == HIGH) {
                close(fd);
                return value;
            }
        }
    }
}

int GPIOWrite(int pin, int value)
{
    const char s_values_str[] = "01";

    char path[BUFSZ] = {0};
    int fd;

    snprintf(path, BUFSZ, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        syslog(LOG_WARNING, "Failed to open gpio value for writing: %m\n");
        return -1;
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
        syslog(LOG_WARNING, "Failed to write value: %m\n");
        return -1;
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    //begin most of 15-step daemonize

    //reset all signal handlers to default
    int i;
    for(i = 0; i < _NSIG; i++) {
        signal(i, SIG_DFL);
    }

    //set up new signal handlers
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SignalHandler);
    signal(SIGTERM, SignalHandler);

    pid_t pid, sid;

    //fork parent process
    pid = fork();
    if(pid < 0) {
        exit(EXIT_FAILURE);
    }

    if(pid > 0) {
        exit(EXIT_SUCCESS);
    }

    //close all file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    //change file mode mask
    umask(0);

    //create unique session id
    sid = setsid();
    if(sid < 0) {
        exit(EXIT_FAILURE);
    }

    //change working directory
    if((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }

    //open syslog
    openlog("mausberry-switch", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "Mausberry switch daemon started. Config file: %s", CONFFILE);

    //config initialization
    config_t cfg;
    int delay = 0;
    config_init(&cfg);

    //open config file
    if(config_read_file(&cfg, CONFFILE)) {
        //read shutdown delay configuration
        if(config_lookup_int(&cfg, "delay", &delay)) {
            syslog(LOG_NOTICE, "Mausberry switch shutdown delay: %d seconds", delay);
        } else {
            syslog(LOG_ERR, "Mausberry switch 'delay' value not found. Defaulting to 0 seconds.");
        }
    } else {
        syslog(LOG_ERR, "Mausberry switch configuration file error:");
        syslog(LOG_ERR, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
        syslog(LOG_ERR, "Mausberry switch 'delay' value not found. Defaulting to 0 seconds.");
    }

    //close config file
    config_destroy(&cfg);

    // Reset GPIO pins
    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
        syslog(LOG_WARNING, "GPIO pins not reset.");

    // Enable GPIO pins
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN))
        syslog(LOG_WARNING, "GPIO pins not exported.");

    // Set GPIO directions
    if (-1 == GPIODirection(POUT, IN) || -1 == GPIODirection(PIN, OUT))
        syslog(LOG_WARNING, "GPIO directions not set.");

    // Initialize switch state
    if (-1 == GPIOWrite(PIN, HIGH))
        syslog(LOG_WARNING, "GPIO not initialized.");

    // Register 'out' pin as interrupt source
    if (-1 == GPIOInterrupt(POUT))
        syslog(LOG_WARNING, "GPIO not configured as interrupt.");

    // Wait for switch state to change
    int result = GPIOWait(POUT);
    syslog(LOG_NOTICE, "Received a %d from gpiowait!\n", result);

    // Disable GPIO pins
    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
        syslog(LOG_WARNING, "Could not unexport gpio pins before shutting down.");

    // Delay
    syslog(LOG_NOTICE, "Waiting %d seconds before shutting down.", delay);
    sleep(delay);

    // Shutdown
    syslog(LOG_NOTICE, "Shutting down.");
    system("poweroff");

    return 0;
}
