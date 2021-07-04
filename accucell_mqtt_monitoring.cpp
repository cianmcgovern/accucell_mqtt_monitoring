#include <iostream>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#include <b6/Device.hh>

#include <mosquitto.h>

/*
 * Compilation:
 *  g++ -I /path/to/libb6/include/files/ -I /usr/include/libusb-1.0/ -lusb-1.0 -lmosquitto -Wall accucell_mqtt_monitoring.cpp /path/to/libb6.a -o accucell_mqtt_monitoring
 *
 * Dependencies:
 *  libmosquitto-dev
 *  libusb-1.0-0-dev
 *  libb2 (C++ library for accessing Turnigy Accucell 6 or similar chargers over USB: https://github.com/maciek134/libb6)
 */

static int run = 1;

void handle_signal(int s) {
	run = 0;
}

int main(int argc, char *argv[]) {

    if(argc != 3) {
        fprintf(stderr, "Usage: accucell_mqtt_monitoring <MQTT Broker hostname> <MQTT Broker port>\n");
        exit(1);
    }

    char *mqtt_host = argv[1];
    int mqtt_port;
    sscanf(argv[2], "%i", &mqtt_port);

    // Time variables
    time_t tm;
    struct tm *timem;
    char time_str[200];

    // Handle process signals
    signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

    // Initialise MQTT and connect to server
    struct mosquitto *mosq;
    mosquitto_lib_init();

    mosq = mosquitto_new("accucell", true, 0);

    if(!mosq) {
        fprintf(stderr, "MQTT initialisation failed!\n");
        exit(1);
    }

    if(mosquitto_connect(mosq, mqtt_host, mqtt_port, 60)) {
        fprintf(stderr, "Unable to connect to MQTT server!\n");
        exit(1);
    }

    // Connect to charger
    // Based on example from https://github.com/maciek134/libb6
    // Device object
    b6::Device *dev;
    try {
        dev = new b6::Device();
    }
    catch (std::exception& e) {
        fprintf(stderr, "Unable to connect to charger!\n");
        exit(1);
    }

    const char *charging = "Charging";
    const char *not_charging = "Not charging";
    const char *stopped = "Stopped";

    b6::ChargeInfo info;

    while(run) {
        // Get current time for console output
        time(&tm);
        timem = localtime(&tm);
        strftime(time_str,sizeof(time_str),"%D %T", timem);

        // Variable for status message
        const char *status;

        try {
            info = dev->getChargeInfo();
        }
        catch (std::exception& e) {
            status = stopped;
            mosquitto_publish(mosq, NULL, "/accucell/status", strlen(status), status, 0, 0);
            fprintf(stderr, "%s Error getting charge info!\n",time_str);
            fprintf(stderr, "%s Waiting 10 seconds...\n", time_str);
            sleep(10);
            continue;
        }
        if(info.state == static_cast<uint8_t>(b6::STATE::CHARGING)) {
            status = charging;
        }
        else {
            status = not_charging;
        }

        // Capacity
        int capacity = info.capacity;
        size_t capacity_nbytes = snprintf(NULL, 0,"%i", capacity) + 1;
        char *capacity_str = (char*) malloc(capacity_nbytes);
        snprintf(capacity_str, capacity_nbytes, "%i", capacity);

        // Voltage
        float voltage = info.voltage / 1000.000;
        size_t voltage_nbytes = snprintf(NULL, 0, "%.3f", voltage) + 1;
        char *voltage_str = (char*) malloc(voltage_nbytes);
        snprintf(voltage_str, voltage_nbytes, "%.3f", voltage);

        // Current
        float current = info.current / 1000.0;
        size_t current_nbytes = snprintf(NULL, 0, "%.1f", current) + 1;
        char *current_str = (char*) malloc(current_nbytes);
        snprintf(current_str, current_nbytes, "%.1f", current);

        // Duration
        int duration = info.time;
        size_t duration_nbytes = snprintf(NULL, 0, "%i", duration) + 1;
        char *duration_str = (char *) malloc(duration_nbytes);
        snprintf(duration_str, duration_nbytes, "%i", duration);

        // Charger temperature
        int temp = info.tempInt;
        size_t temp_nbytes = snprintf(NULL, 0, "%i", temp) + 1;
        char *temp_str = (char *) malloc(temp_nbytes);
        snprintf(temp_str, temp_nbytes, "%i", temp);


        std::cout << time_str << " Status: " 	<< status       << std::endl;
        std::cout << time_str << " mAh: " 		<< capacity_str << std::endl;
        std::cout << time_str << " Voltage: " 	<< voltage_str  << std::endl;
        std::cout << time_str << " Current: " 	<< current_str  << std::endl;
        std::cout << time_str << " Duration: " 	<< duration_str << std::endl;
        std::cout << time_str << " Int temp: " 	<< temp_str     << std::endl;

        mosquitto_publish(mosq, NULL, "/accucell/capacity", capacity_nbytes, capacity_str, 0, 0);
        mosquitto_publish(mosq, NULL, "/accucell/current", current_nbytes, current_str, 0, 0);
        mosquitto_publish(mosq, NULL, "/accucell/voltage", voltage_nbytes, voltage_str, 0, 0);
        mosquitto_publish(mosq, NULL, "/accucell/status", strlen(status), status, 0, 0);
        mosquitto_publish(mosq, NULL, "/accucell/duration", strlen(duration_str), duration_str, 0, 0);
        mosquitto_publish(mosq, NULL, "/accucell/int_temp", strlen(temp_str), temp_str, 0, 0);

        sleep(10);
    }

    delete dev; // this releases the device, reattaches the kernel driver and frees libusb

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}