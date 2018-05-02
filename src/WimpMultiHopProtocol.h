#include "Arduino.h"
#include "ESP8266WiFi.h"

//maybe they could be an association ID-IP
IPAddress parent;
IPAddress children[4];
IPAddress neighbours[10];
//maybe I need also the raspberry IP
char buffer[10][256];  //keep list of incoming messages (circular)
bool wait_for_ack = false;
int8_t first_message = 0;
int8_t last_message = 0;
uint16_t path_length = (64 * 1024) - 1; //not sure of the -1... 

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
    int read(char* data) {
        return -1;
    }

    /**
     * Send data to sink (and only to sink!)
     * loop is needed for reliability
    */
    bool send(char* data) {
        return false;
    }

    /**
     * Look at data,
     * take the first address/id
     * remove that ip
     * forward (new) data to that ip (udp-send)
     * it should be possible to do this without loop
    */
    void forward(char* data) {

    }

    /**
     * Calls all the needed functions for the first start (at least):
     * - scan_network()
     * - hello()
     * - change_parent()
     * maybe it needs the local ip
    */
    void manage_network() {

    }

    /**
     * Look for AP in the net
     * stores parent and neighbours (not children)
     * checks if at least parent is alive
     * it can change parent if needed
    */
    void scan_network() {

    }

    /**
     * Sends to all the neighbours (parent+children+neighbours) info
     * on his IP, parent, number of children and path_lenght;
     * if parent doesn't answer, loop
    */
    void hello() {

    }

    /**
     * Sent as an answer to hello, the node comunicates his
     * IP, parent, number of children and path_length
     * probably it can just be the same as hello...
    */
    void hello_risp() {

    }

    /**
     * Removes "connection" with old_parent
     * asks to new_parent to became the new parent,
     * has to wait for ack;
     * if errors, it has to be called again with a new new_parent
    */
    bool change_parent(IPAddress new_parent, IPAddress old_parent) {
        return false;
    }

    /**
     * Asks the parent to remove me from his children
     * also this need a loop I guess
    */
    bool leave_me() {
        return false;
    }

};


/**
 * 
 * the message to be forwarded from node to children is built from the rasp
 * he knows all the topology of the network and builds:
 * a.b.c.d\n
 * ...
 * a'.b'.c'.d'\n
 * original json data
 * the intermediate nodes do getLine, and have the ip as a string,
 * then they count the length of the removed string and create a new message
 * of the correct lenght without the first ip\n
 * 
 * if the message is broadcast, the rasp could just send
 * broadcast\n
 * original json data
 * and all the nodes send their message to their children (and only to them) 
 * 
*/