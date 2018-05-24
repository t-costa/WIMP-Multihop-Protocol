//
// Created by tommaso on 17/05/18.
//

#include <sys/socket.h>
#include <pthread.h>
#include <cstdlib>
#include <cstdio>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <iostream>

#include <nlohmann/json.hpp>
#include <thread>

#include "RaspServer.h"

#define HOST "127.0.0.1"
#define PACKET_SIZE 512
#define SINK_SERVER_PORT 42000
#define WLM_SERVER_PORT 42001

using json = nlohmann::json;

void sender() {
    sleep(5);
    std::vector<std::string> messages;

    std::string hello, ack, leave, change, forpar, forchi;
    hello = R"({"handle":"hello","ip":"192.168.43.114","path":"77"})";
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
        sleep(30);
        int i = rand() % 6;
        WIMP::show_routes();
        sleep(1);
        std::cout << "Sending message: " << messages[i] << std::endl;

        if (i%2 == 0) {
            WIMP::send(messages[i].c_str(), "192.168.43.57");
        } else {
            WIMP::send(messages[i].c_str(), "255.255.255.255");
        }
        t = (i != 7);
    }
}

void wsn2wlm_forwarder() {
    struct sockaddr_in si_other;
    int s = 0;
    socklen_t slen = sizeof(si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        std::cerr << "Connection to WLM failed." << std::endl;
        exit(1);
    }

    memset((char *) &si_other, 0, sizeof(si_other));

    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(WLM_SERVER_PORT);
    if (inet_aton(HOST , &si_other.sin_addr) == 0) {
        std::cerr << "inet_aton() failed" << std::endl;
        exit(1);
    }

    std::cout << "Now forwarding from WSN to WLM" << std::endl;

    char msg[PACKET_SIZE];
    int n = 0;
    while(true) {
        // Read from WSN
        n = WIMP::read(msg);

        if (n < 0) {
            std::cerr << "Error in read, n<0" << std::endl;
            //exit(1);
        }

        if (n > 0) {
            //message for application
            // Send the message
            if (sendto(s, msg, 512, 0, (struct sockaddr *) &si_other, slen) < 0) {
                std::cerr << "Error while forwarding message to WLM." << std::endl;
                //cacca
            }
            std::cout << "Just sent: " << msg << std::endl;
        }
    }

    close(s);
    std::cerr << "WSN to WLM has ended" << std::endl;
}

void wlm2wsn_forwarder() {
    struct sockaddr_in si_me, si_other;

    int s = 0;
    ssize_t recv_len = 0;
    socklen_t slen = sizeof(si_other);
    char buf[PACKET_SIZE];

    // Create a UDP socket.
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        std::cerr << "Unable to create input socket." << std::endl;
        exit(1);
    }

    // zero out the structure
    memset((char *) &si_me, 0, sizeof(si_me));

    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(SINK_SERVER_PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    // Setting SOREUSEADDR
    //if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) { fprintf(stderr, "setsockopt(SO_REUSEADDR) failed"); exit(1); }

    // Bind socket to port.
    if( bind(s, (struct sockaddr*)&si_me, sizeof(si_me) ) == -1) {
        std::cerr << "Unable to bind input socket." << std::endl;
        exit(1);
    }

    // Keep listening for data.
    std::cout << "Now forwarding from WLM to WSN." << std::endl;

    while(true) {
        // Try to receive some data, this is a blocking call.
        if ((recv_len = recvfrom(s, buf, PACKET_SIZE, 0, (struct sockaddr *) &si_other, &slen)) == -1) {
            std::cerr << "Error while receiving UDP packets from WLM." << std::endl;
        }

        if (recv_len > 0) {
            buf[recv_len] = '\0';

            json doc;
            try {
                doc = json::parse(buf);
                std::string ip = doc["to"].get<std::string>();

                // Print details of the client/peer and the data received.
                printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
                std::cout << "received packet: " << buf  << "\tdest ip: " << ip << std::endl;

                WIMP::send(buf, ip.c_str());
            } catch  (json::parse_error &e) {
                std::cerr << "Error in parsing!" << std::endl;
            }
        }

    }

    close(s);
    std::cerr << "WLM to WSN has ended." << std::endl;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Secify local ip address! Usage: ./gateway.o ip" << std::endl;
        return 0;
    }

    std::cout << "Hello! I'm a WimpSink, but I'm going to surprise you." << std::endl;

    // Connection step
    std::cout << "Hop hop connection..." << std::endl;
    if (!WIMP::initialize(argv[1])) {
        std::cerr << "Error ininitializing the WIMP net!" << std::endl;
        return 1;
    }
    std::cout << "I'm connected to WIMPnet! Hello guys :)" << std::endl;

    // WLM to WSN (led updates)
    std::cout << "Connecting WLM to WSN..." << std::endl;
    std::thread wlm2wsn_worker(wlm2wsn_forwarder);

    // WSN to WLM (parking place updates)
    std::cout << "Connecting WSN to WLM..." << std::endl;
    std::thread wsn2wlm_worker(wsn2wlm_forwarder);

    //std::cout << "Starting sender test..." << std::endl;
    //std::thread sender_worker(sender);

    // Join the forwarders for termination.
    wlm2wsn_worker.join();
    wsn2wlm_worker.join();
    //sender_worker.join();

    std::cout << "All forwarders have been terminated. Goodbye!" << std::endl;

    return 0;
}

