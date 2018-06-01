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
            exit(1);
        }

        if (n > 0) {
            //message for application
            // Send the message
            if (sendto(s, msg, 512, 0, (struct sockaddr *) &si_other, slen) < 0) {
                std::cerr << "Error while forwarding message to WLM." << std::endl;
                exit(1);
            }
            std::cout << "Just sent: " << msg << std::endl;
        }
    }
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

    // Bind socket to port.
    if( bind(s, (struct sockaddr*)&si_me, sizeof(si_me) ) == -1) {
        std::cerr << "Unable to bind input socket." << std::endl;
        exit(1);
    }

    // Keep listening for data.
    std::cout << "Now forwarding from WLM to WSN." << std::endl;

    while(true) {
        // Try to receive some data, this is a blocking call.
        recv_len = recvfrom(s, buf, PACKET_SIZE, 0, (struct sockaddr *) &si_other, &slen);
        if (recv_len < 0) {
            std::cerr << "Error while receiving UDP packets from WLM." << std::endl;
            exit(1);
        }

        if (recv_len > 0) {
            buf[recv_len] = '\0';

            json doc;
            try {
                doc = json::parse(buf);
                std::string id = doc["to"].get<std::string>();

                // Print details of the client/peer and the data received.
                printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
                std::cout << "received packet: " << buf  << "\tm id: " << id << std::endl;

                if (WIMP::send(buf, id.c_str())){
                    std::cout << "Message correctly sent and received!" << std::endl;
                } else {
                    std::cerr << "We didn't receive the ack!" << std::endl;
                }
            } catch  (json::parse_error &e) {
                std::cerr << "Error in parsing!" << std::endl;
                exit(1);
            }
        }

    }
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Specify local ip address! Usage: ./gateway.o ip" << std::endl;
        return 0;
    }

    std::cout << "Hello! I'm a WimpSink, but I'm going to surprise you." << std::endl;

    // Connection step
    std::cout << "Hop hop connection..." << std::endl;
    if (!WIMP::initialize(argv[1])) {
        std::cerr << "Error initializing the WIMP net!" << std::endl;
        return 1;
    }
    std::cout << "I'm connected to WIMP net! Hello guys :)" << std::endl;

    // WLM to WSN (led updates)
    std::cout << "Connecting WLM to WSN..." << std::endl;
    std::thread wlm2wsn_worker(wlm2wsn_forwarder);

    // WSN to WLM (parking place updates)
    std::cout << "Connecting WSN to WLM..." << std::endl;
    std::thread wsn2wlm_worker(wsn2wlm_forwarder);

    // Join the forwarders for termination.
    wlm2wsn_worker.join();
    wsn2wlm_worker.join();

    return 0;
}

