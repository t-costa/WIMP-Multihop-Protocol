#include "ESP8266WiFi.h"

namespace WIMP {

    /**
     * Look for udp packet arrived
     * check if they are for current esp -> save data
     * if they have to be forwarder -> call forward
     * if manage message (hello) -> call the right method
     * if ack/no answer needed -> nothing
     * if there are already pending messages (buffer not empty), maybe it should
     * first return those messages -> writes the message for the appl in data
     * returns the number of bytes read
    */
    int read(char* data);

    /**
     * 
    */
    void ack(IPAddress const& dest, bool flag);

    /**
     * 
    */
    char* retrieve_packet();

    /**
     * Send data to sink (and only to sink!)
     * loop is needed for reliability
    */
    void send(char* data);

    /**
     * Calls all the needed functions for the first start (at least):
     * - scan_network()
     * - hello()
     * - change_parent()
     * maybe it needs the local ip
    */
    void manage_network();

//    /**
//     * Look for AP in the net
//     * stores parent and neighbours (not children)
//     * checks if at least parent is alive
//     * it can change parent if needed
//    */
//    bool scan_network();

    /**
     * Initializes the network
    */
    void initialize();
};
