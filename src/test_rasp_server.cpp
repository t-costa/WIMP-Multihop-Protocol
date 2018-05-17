//
// Created by tommaso on 10/05/18.
//

#include "RaspServer.h"
#include <iostream>
#include <unistd.h>
#include <vector>
#include <thread>

void sender() {
    sleep(5);
    std::vector<std::string> messages;

    std::string hello, ack, leave, change, forpar, forchi;
    hello = R"({"handle":"hello","ip":"192.168.12.12","path":"77"})";
    ack = R"({"handle" : "ack","type" : "true"})";
    forpar = R"({"handle" : "forward_parent","data":{"type":"place","from":"DEVICE_ID","place":"7","status":"busy"}})";
    leave = R"({"handle" : "leave",	"ip_source" : "192.168.91.10"})";
    change = R"({"handle" : "change","ip_source" : "192.54.153.15","ip_old_parent" : "192.168.12.12"})";
    forchi = R"({"type": "ledstatus","from": "DEVICE_ID","place": "7","light": "on"})";

    messages.push_back(hello);
    messages.push_back(ack);
    messages.push_back(forchi);
    messages.push_back(forpar);
    messages.push_back(change);
    messages.push_back(leave);

    std::cout << "Messages formed" << std::endl;

    srand(time(NULL));
    bool t = true;
    while (t) {
        sleep(5);
        int i = rand() % 6;
        WIMP::show_routes();
        sleep(1);
        std::cout << "Sending message: " << messages[i] << std::endl;

        WIMP::send(messages[i].c_str(), "255.255.255.255");
        t = (i != 7);
    }

}

int main() {

    std::cout << "Initializing..." << std::endl;

    if (WIMP::initialize())
        std::cout << "Initialization completed!" << std::endl;
    else {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }

    char data[512];

    std::thread t1(sender);

    while (true) {
        int n = WIMP::read(data);
        if (n > 0) {
            std::cout << "Received message" << std::endl;
            data[n] = '\0';
            std::cout << "Data: " << data << std::endl;
        } else {
            if (n < 0) {
                break;
            }
        }
    }

    return 0;
}

