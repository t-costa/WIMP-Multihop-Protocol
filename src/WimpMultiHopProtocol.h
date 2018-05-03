#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "WiFiUdp.h"
#include "string.h"
#include "ArduinoJson.h"

#define PORT 4210

//maybe they could be an association ID-IP
IPAddress my_ip;
IPAddress parent;
IPAddress children[4];
IPAddress neighbours[10];
//maybe I need also the raspberry IP
char buffer[10][256];  //keep list of incoming messages (circular)
bool wait_for_ack = false;
bool message_waiting = false;
int8_t first_message = 0;
int8_t last_message = 0;
uint16_t path_length = (64 * 1024) - 1; //not sure of the -1... 
WiFiUDP udp;
StaticJsonBuffer<256> json_buffer;


namespace WIMP {
    
    /**
     * Actual send of the packet with udp
    */
    void udp_send(IPAddress ip_dest, int port_dest, char* data) {
        udp.beginPacket(ip_dest, port_dest);
        udp.write(data);
        udp.endPacket();
    }

    /**
     * Reads a string until a \n or the end of file,
     * returns the line read
    */
    char* readline(char* message, int length, int* start) {
        char res[64];
        int i = 0;
        
        while (*start < length && res[*start] != '\n') {
            res[i] = message[*start];
            i++;
            (*start)++;
        }

        return res;
    }

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

        //prepare to listen for messages to that port
        udp.begin(PORT);

        //maybe need delay?
        int packet_size = udp.parsePacket();
        int i = 0;
        while (!packet_size && i < 5) {
            Serial.printf("Waiting for arriving packet ... \n");
            delay(250);
            i++;
        }
        //TODO: check if packet_size == len_packet
        if (!packet_size) {
            //no packet arrived
            return 0;
        }

        //there is some packet
        Serial.printf("Received packet: %d byte from %s ip, port %d\n", 
                        packet_size, 
                        udp.remoteIP().toString().c_str(), 
                        udp.remotePort());

        //TODO: 256 is random for now...
        int len_packet = udp.read(data, 256);
        if (len_packet > 0 && len_packet < 256) {
            data[len_packet] = '\0';
        }

        Serial.printf("Text of received packet: \n%s", data);

        JsonObject& root = json_buffer.parseObject(data);
        if (!root.success) {
            Serial.println("Error in parsing received message!");
            return -1;
        }

        char* type = root["type"];

        if (strcmp(type, "HELLO")) {
            //hello packet
            //add the possibly new neighbour to the list, answer with
            //your info
            IPAddress other;
            int other_path;
        
            other.fromString(root["IP"].asString());
            other_path = root["PATH"];
            //TODO: use the info

            hello(other);
            return 0;   //no message for application
        }

        if (strcmp(type, "CHANGE")) {

        }

        if (strcmp(type, "LEAVE")) {
            
        }

        if (strcmp(type, "FORWARD")) {
            //nel campo IP avrà la lista da seguire
        }

        //application packet
        //it's a json and is not a management/forward one
        
        for (int i=0; i<len_packet; ++i) {
            buffer[last_message][i] = data[i];
        }
        last_message++;
        last_message = (last_message % 10); //circular buffer

        message_waiting = true;

        return len_packet;
    }

    char* retrieve_packet() {
        if (message_waiting) {
            char* msg = buffer[first_message];
            first_message++;
            first_message = (first_message % 10);
            if (first_message == last_message) {
                message_waiting = false;
            }
            return msg;
        } else {
            return NULL;
        }
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
    void hello(IPAddress dest) {  
        char msg[256];
        JsonObject& ans = json_buffer.createObject();
       
        ans["TYPE"] = "HELLO";
        ans["IP"] = my_ip.toString();
        ans["PATH"] = path_length;
        ans.printTo(msg);

        udp_send(dest, PORT, msg);
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

/**
 * 
 * management messages
 * HELLO: 
 *      HELLO\n
 *      ID/IP\n
 *      PathLength\0
 * 
 * HELLORISP = HELLO
 * 
 * CHANGEPARENT:
 *      CHANGE\n
 *      ID/IPsource\n
 *      ID/IPoldpar\0
 * 
 * LEAVEME:
 *      LEAVE\n
 *      ID/IPsource\0   non credo serva dest, viene mandato direttamente a lui
 * 
*/