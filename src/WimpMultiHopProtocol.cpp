#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "WiFiUdp.h"
#include "ArduinoJson.h"
#include "WimpMultiHopProtocol.h"

#include <cstring>
#include <string>
#include <ctime>
#include <sstream>
#include <random>



#define PORT 42100
#define MAX_CHILDREN 1
#define MAX_NEIGHBOURS 10
#define LEN_BUFFER 10
#define LEN_PACKET 256
#define MAX_PATH_LENGTH 255
#define MAX_TIME_NOT_SEEN 5

#define debug true

//TODO: FORSE LA COSA PIÙ FACILE È DEFINIRE DUE READ DIVERSE, PER MANAGEMENT E PER APPLICAZIONE, DOVE CONTROLLI VECCHI PACCHETTI

struct node_info {
    IPAddress ip;
    String ssid;
    int32_t rssi;
    uint8_t len_path;
    uint8_t times_not_seen;
};

//maybe they could be an association ID-IP
WiFiUDP udp;
IPAddress my_ip;
node_info default_parent = node_info {IPAddress(0, 0, 0, 0), "myself", -999, MAX_PATH_LENGTH, MAX_TIME_NOT_SEEN};
node_info parent = default_parent;
node_info children[MAX_CHILDREN];
node_info neighbours[MAX_NEIGHBOURS];   //dovrebbe essere ordinato per path_length e power signal!

//WiFiEventHandler stationDisconnectedHandler, stationConnectedHandler;

char buffer[LEN_BUFFER][LEN_PACKET];  //keep list of incoming messages (circular)
bool positive_ack = false, message_waiting = false, parent_answer = false;
char wait_for_ack[20];
uint8_t first_message = 0, last_message = 0, path_length = MAX_PATH_LENGTH, num_children = 0, num_neighbours = 0;
char ssid_name[10];
uint32_t test_id = 0;
//const char* ssid = "IPA";
//const char* password = "sett7ete";

uint32_t unique_id = 7; //FIXME: dovrà essere l'arduino a darmi questo id (o direttamente io?) e verrà usato da raspy per deploy e collegamento id-ip

const char* ssid = "WIMP_";
const char* password = "7settete7&IPA";

//const char* ssid = "FRITZ";
//const char* password = "HirmerDiGuardoAmicoFRITZ";

//const char* ssid = "Mi";
//const char* password = "4e8b149679da";

StaticJsonBuffer<LEN_PACKET> json_buffer;


/// Actual send of the packet with udp
/// \param ip_dest ip address of the destination
/// \param port_dest port of the destination
/// \param data message to be sent
void udp_send(IPAddress const& ip_dest, uint16_t port_dest, char* data) {
    udp.beginPacket(ip_dest, port_dest);
    udp.write(data);
    udp.endPacket();
}


/// Search a specific ip in the given list
/// \param other ip address the funtion has to search
/// \param list structure in which the function has to search
/// \param length length of the structure
/// \return intex of the element in the list if found, -1 if not present
int search(IPAddress const& other, node_info list[], int length) {
    bool found = false;
    int i = 0;
    while (i < length && !found) {
        if (list[i].ip.operator==(other)) {
            found = true;
        } else {
            i++;
        }
    }
    if (found) {
        return i;
    } else {
        return -1;
    }
}


/// Swaps values in positions i and j in v (assume 0 <= i,j < length of v)
/// \param v list with the elements
/// \param i index of the first element to be swapped
/// \param j index of the second element to be swapped
void swap(node_info v[], int i, int j) {
    node_info temp = v[i];
    v[i] = v[j];
    v[j] = temp;
}


/// A shitty implementation of a sorting algorithm
/// \param v structure we have to sort
/// \param length length of the structure
void shit_sort(node_info v[], int length) {
    
    for (int i=0; i<length; ++i) {
        for (int j=i+1; j<length; ++j) {
            if (v[j].rssi < v[i].rssi) {
                swap(v, i, j);
            }
        }
    }

    for (int i=0; i<length; ++i) {
        for (int j=i+1; j<length; ++j) {
            if (v[j].len_path < v[i].len_path) {
                swap(v, i, j);
            }
        }
    }
}


/// Sends an ack to a node
/// \param dest ip address of the destination node
/// \param flag type of the ack, if positive or negative
void WIMP::ack(IPAddress const& dest, bool flag) {
    char msg[64];
    JsonObject& ans = json_buffer.createObject();
    
    ans["handle"] = "ack";
    ans["type"] = flag ? "true" : "false";
    ans["ip_source"] = my_ip.toString();
    ans.printTo(msg);
#if debug
if (flag) {
    Serial.printf("Sending positive ack\n");
} else {
    Serial.printf("Sending negative ack\n");
}
#endif
    udp_send(dest, PORT, msg);
}


/// Parse a received ack and sets global variables
/// \param root json received
void read_ack(JsonObject& root, char* data) {
    //TODO: credo di poter fare anche direttamente con bool

    const char* source = root["ip_source"];
#if debug
    Serial.printf("Waiting ack from %s, received from %s\n", wait_for_ack, source);
#endif
    if (strcmp(source, wait_for_ack) == 0) {
        const char* result = root["type"];

        positive_ack = strcmp(result, "true") == 0;

#if debug
        if (positive_ack) {
            Serial.printf("Read a positive ack\n");
        } else {
            Serial.printf("Read a negative ack\n");
        }
#endif
        //wait_for_ack = nullptr;
    } else {
#if debug
Serial.printf("Received an ack that is not for me, forward to destination (parent)\n");
#endif
        udp_send(parent.ip, PORT, data);
    }


}


/// Parse the received message of type hello
/// \param root json received
void read_hello(JsonObject& root) {

    uint8_t other_path;
    String s;

    root["ip"].printTo(s);  //stores "...."
    uint8_t b[4];

    int i=s.length()-2, j=3;
    int acc = 1;
    for (unsigned char &k : b) {
        k = 0;
    }

	while (i > 0) {
        b[j] += (s[i]-'0')*acc;
        acc *= 10;
        i--;
        if (i >= 0 && s[i] == '.') {
            i--;
            acc = 1;
            j--;
        }
	}
	IPAddress other(b[0], b[1], b[2], b[3]);

    other_path = root["path"];

#if debug
Serial.printf("Reading hello risp: ip: %s - path: %d\n", other.toString().c_str(), other_path);
Serial.printf("My parent: %s\n", parent.ip.toString().c_str());
#endif

    if (parent.ip.operator==(other)) {
        //update path length of the parent
#if debug
Serial.printf("Read an hello from parent:\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
        parent_answer = true;
        other_path++;
        parent.len_path = other_path;
        parent.times_not_seen = 0;
    } else {
        i = search(other, children, num_children);
        if (i != -1) {
            //update info of child
#if debug
Serial.printf("Read an hello from children:\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
            //here i don't need the ++ on other_path
            children[i].len_path = (uint8_t) ((other_path < path_length+1) ? other_path : path_length+1);
            children[i].times_not_seen = 0;
        } else {
            i = search(other, neighbours, num_neighbours);
            if (i != -1) {
                //known neighbour
#if debug
Serial.printf("Read an hello from neighbour (known):\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
                //no need for other_path++
                neighbours[i].len_path = other_path;
                neighbours[i].times_not_seen = 0;
            } else {
                //new neighbour
#if debug
Serial.printf("Read an hello from neighbour (unknown):\n");
Serial.printf("IP: %s\n PATH: %d\nNum_neighbours: %d\n", other.toString().c_str(), other_path, num_neighbours);
#endif
                if (num_neighbours < MAX_NEIGHBOURS) {
                    //add the new neighbour it
                    char sn[20];   //TODO:CHECK
                    root["ssid"].printTo(sn);
                    neighbours[num_neighbours] = node_info { other, sn, -75, other_path, 0 };
                    num_neighbours++;
                }
#if debug
else {
    Serial.println("Can't add the new neighbour! There are too many! :(");
}
#endif
            }
        }
    }
}


/// Forwards the received packet to the specific child or to the application if I am the destination
/// \param complete_packet data received
/// \param root json object received
/// \return 0 if I'm not the destination, the length of the packet otherwise
size_t read_forward_children(char* complete_packet, JsonObject& root) {
    //in the path field there will be the list of hop
    JsonArray& ip_path = root["path"];

#if debug
root.prettyPrintTo(Serial);
#endif

    if (ip_path.measureLength() == 0) {
        //I am the destination
        //read message and deliver
        const char* data = root["data"];
        size_t len_packet = strlen(data); //forse questo è più affidabile...
        //size_t len_packet = root["data"].measureLength();

        for (int i=0; i<len_packet; ++i) {
            buffer[last_message][i] = data[i];
        }

        last_message++;
        last_message = (uint8_t) (last_message % LEN_BUFFER); //circular buffer

        message_waiting = true;
#if debug
Serial.printf("Read a forward children directed to me (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("DATA: %s\n", data);
#endif

        return len_packet;
    } else {
        String next = ip_path[0];   //first hop

        if (next.equals("broadcast")) {
            //deliver data to application
            const char* data = root["data"];
            size_t len_packet = root["data"].measureLength();  //TODO: check

            for (int i=0; i<len_packet; ++i) {
                buffer[last_message][i] = data[i];
            }
            
            last_message++;
            last_message = (uint8_t) (last_message % LEN_BUFFER); //circular buffer

            message_waiting = true;
#if debug
Serial.printf("Read a forward children broadcast (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("DATA: %s\n", data);
#endif
            //resend only to children (avoid loop in the network)
            for (int i=0; i<num_children; ++i) {
                udp_send(children[i].ip, PORT, complete_packet);
            }

            return len_packet;
        }
        //I'm just an intermediate node
        //remove my ip from the path and forward to the right child
        IPAddress next_hop;
        next_hop.fromString((const char*) ip_path[0]);  //TODO: check

        //remove the hop that I'm processing
        ip_path.remove(0);

        //write the new json message
        char new_data[LEN_PACKET];
        root.printTo(new_data);

#if debug
Serial.printf("Read a forward children NOT directed to me (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Next hop: %s\n", next_hop.toString().c_str());
Serial.printf("DATA (forwarded): %s\n", new_data);
#endif
        
        udp_send(next_hop, PORT, new_data);

        return 0;
    }            
}


/// Forwards the received packet to my parent
/// \param data message to be forwarded
void read_forward_parent(char* data) {
    //packet from sink to parent
    //since this is the library for the esp,
    //I will never be the destination...
    //data should not be modified

#if debug
Serial.printf("Read a forward parent (my_ip: %s):\n", my_ip.toString().c_str());
#endif

    udp_send(parent.ip, PORT, data);
}


/// Parse the received message of type change
/// \param root json object received
void read_change(JsonObject& root) {
    //I received a change, add the node to children if possible
    IPAddress new_child, old_par;
    String n, o;
    root["ip_source"].printTo(n);
    root["ip_old_parent"].printTo(o);
    new_child.fromString(n);
    old_par.fromString(o);

#if debug
Serial.printf("Read a change parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("ip_source: %s - ip_old: %s\n", new_child.toString().c_str(), old_par.toString().c_str());
#endif

    if (num_children == MAX_CHILDREN) {
        //send error, can't have more children
#if debug
Serial.printf("Refused because reached max_children\n");
#endif
        WIMP::ack(new_child, false);
    } else {
        if (old_par.operator==(parent.ip)) {
            //TODO: fratello bastardo
#if debug
Serial.println("My parent is the same as old parent! Shit");
#endif
            //TODO: dovrebbe essere questo il caso scatenante
            WIMP::ack(new_child, false);
            return;
        }

        //add to children and send positive ack

        children[num_children].ip = new_child;
        children[num_children].len_path = (uint8_t) (path_length+1);
        num_children++;
#if debug
Serial.printf("Child accepted! num_children=%d\n", num_children);
Serial.println("Sending ack to the new child");
#endif
        WIMP::ack(new_child, true);

        //inform sink
        JsonObject& risp = json_buffer.createObject();
        risp["type"] = "network_changed";
        risp["operation"] = "new_child";
        risp["ip_child"] = new_child.toString().c_str();
        risp["ip_parent"] = my_ip.toString().c_str();
        char data[LEN_PACKET];
        risp.printTo(data);

        //WIMP::send(data);
        udp_send(parent.ip, PORT, data);    //la send mi wrappa il forward children
#if debug
Serial.printf("Sent change in the net to the raspy\n");
#endif
        json_buffer.clear();
    }
}


/// Parse the received message of type leave
/// \param root json object received
void read_leave(JsonObject& root) {
    //my child is a piece of shit, I have to kill it
    IPAddress ip;
    ip.fromString(root["ip_source"].as<char*>());
    
    int index = search(ip, children, num_children);

    if (index == -1) {
        //not found, error
#if debug
Serial.printf("Read a leave parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Error! not my child\n");
#endif
        //mando comunque ack a true, è come se l'obiettivo fosse raggiunto
        WIMP::ack(ip, true);
        return;
    }

    //ricompatta children
    while (index < num_children-1) {
        children[index] = children[index+1];
        index++;
    }
    num_children--;

    WIMP::ack(ip, true);
    
    //inform sink
    JsonObject& obj = json_buffer.createObject();
    obj["handle"] = "network_changed";
    obj["operation"] = "removed_child";
    obj["ip_child"] = ip.toString().c_str();
    obj["ip_parent"] = parent.ip.toString().c_str();
    char data[LEN_PACKET];
    obj.printTo(data);

#if debug
Serial.printf("Read a leave parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Accepted, inform raspy\n");
#endif

    //WIMP::send(data);
    udp_send(parent.ip, PORT, data);    //la send mi wrappa il forward children
}


/// Deliver an old received packet to the application
/// \return an old received packet or NULL if there is nothing
char* retrieve_packet() {
    if (message_waiting) {
        char* msg = buffer[first_message];
        first_message++;
        first_message = (uint8_t) (first_message % 10);
        if (first_message == last_message) {
            message_waiting = false;
        }
        return msg;
    } else {
        return nullptr;
    }
}


/// Sends to all the reachable nodes info on my self
/// \param dest address to which send the packet
/// \param type hello or hello_risp message
void hello(IPAddress const& dest, const char* type) {
    char msg[LEN_PACKET];
    JsonObject& ans = json_buffer.createObject();
    
    ans["handle"] = type;
    ans["ip"] = my_ip.toString();
    ans["path"] = path_length;
    ans["ssid"] = ssid_name;
    ans["test_id"] = test_id;
    ans["unique_id"] = unique_id;
    test_id++;
    ans.printTo(msg);

#if debug
Serial.printf("Sending hello (my_ip: %s) (dest: %s)\n", my_ip.toString().c_str(), dest.toString().c_str());
Serial.printf("Data: %s\n", msg);
#endif

    udp_send(dest, PORT, msg);
    json_buffer.clear();
}


/// Implements actual udp read
/// \param data buffer where to store received data
/// \return length of the received data, 0 if no data for application, -1 if error
int private_read(char* data) {
    int packet_size = udp.parsePacket();
    int i = 0;

#if debug
Serial.printf("Waiting for arriving packet ... \n");
#endif

    while (!packet_size && i < 20) {
#if debug
Serial.printf(".");
#endif
        delay(250);
        i++;
    }

    //TODO: check if packet_size == len_packet
    if (packet_size <= 0) {
        //no packet arrived
#if debug
Serial.println(" No new packet!");
#endif
        return 0;
    }

    //there is a packet
#if debug
Serial.printf("Received packet: %d byte from %s ip, port %d\n",
              packet_size,
              udp.remoteIP().toString().c_str(),
              udp.remotePort());
#endif

    int len_packet = udp.read(data, LEN_PACKET);

#if debug
Serial.printf("packet_size (from parse_packet): %d\nlen_packet (from actual read): %d\n", packet_size, len_packet);
#endif

    if (len_packet > 0 && len_packet < LEN_PACKET) {
        //non so se serve...
        data[len_packet] = '\0';
    }

    JsonObject& root = json_buffer.parseObject(data);

    if (!root.success()) {
#if debug
Serial.println("Error in parsing received data!");
#endif
        json_buffer.clear();
        return -1;
    }

#if debug
Serial.printf("Text of received packet: \n%s", data);
root.prettyPrintTo(Serial);
#endif

    const char* handle = root["handle"];

    if (strcmp(handle, "hello") == 0) {
#if debug
        Serial.println("Parsing a HELLO message");
#endif
        read_hello(root);
        //always answer
        hello(udp.remoteIP(), "hello_risp");
        json_buffer.clear();
        return 0;   //no message for application
    }

    if (strcmp(handle, "hello_risp") == 0) {
#if debug
Serial.println("Parsing a HELLO_RISP message");
#endif
        read_hello(root);
        json_buffer.clear();
        return 0;
    }

    if (strcmp(handle, "change") == 0) {
#if debug
Serial.println("Parsing a CHANGE message");
#endif
        read_change(root);
        json_buffer.clear();
        return 0;
    }

    if (strcmp(handle, "leave") == 0) {
#if debug
Serial.println("Parsing a LEAVE message");
#endif
        read_leave(root);
        json_buffer.clear();
        return 0;
    }

    if (strcmp(handle, "forward_children") == 0) {
#if debug
Serial.println("Parsing a FORWARD_CHILDREN message");
#endif
        int ret = (int) read_forward_children(data, root);
        json_buffer.clear();
        return ret;
    }

    if (strcmp(handle, "forward_parent") == 0) {
#if debug
Serial.println("Parsing a FORWARD_PARENT message");
#endif
        read_forward_parent(data);
        json_buffer.clear();
        return 0;
    }

    if (strcmp(handle, "network_changed") == 0) {
#if debug
Serial.println("Parsing a NETWORK_CHANGED message");
#endif
        read_forward_parent(data);  //they have to do the same thing
        json_buffer.clear();
        return 0;
    }

    if (strcmp(handle, "ack") == 0) {
#if debug
Serial.println("Parsing a ACK message");
#endif
        read_ack(root, data);
        json_buffer.clear();
        return 0;
    }

    //this should be an error
#if debug
Serial.println("Qualquadra non cosa...");
#endif
    return -1;
}

/// Look for new udp packet incoming and checks the type of the packets
/// \param data buffer in which write the received message for the application (if any)
/// \return the number of bytes read, 0 if the messages were of management, -1 if errors
int WIMP::read(char* data) {
    if (message_waiting) {
        data = retrieve_packet();
        return (data == nullptr ? 0 : (int) strlen(data));
    } else {
        int n = private_read(data);
        if (n > 0) {
            //dovrei avere il messaggio sia nel buffer che in data, rimuovo quello in buffer
            message_waiting = false;
            first_message++;    //check
        }
        return private_read(data);
    }
}


/// Sends data to sink (and only to sink!)
/// \param data message to be sent to the sink
void WIMP::send(const char* data) {
    JsonObject& root = json_buffer.createObject();
    root["handle"] = "forward_parent";
    root["data"] = data;

    root["test_id"] = test_id;
    test_id++;

    char msg[LEN_PACKET];
    root.printTo(msg, root.measureLength() + 1);    //TODO: check dim and \0
    msg[root.measureLength()] = '\0';   //serve?

#if debug
Serial.printf("Sending a message to raspy (my_ip: %s) (parent: %s):\n", my_ip.toString().c_str(), parent.ip.toString().c_str());
Serial.printf("DATA: %s\n", msg);
#endif

    udp_send(parent.ip, PORT, msg);
    json_buffer.clear();
}


/// Asks the parent to remove me from his children
/// \return true iff everything went ok
bool leave_me() {

    JsonObject& root = json_buffer.createObject();
    root["handle"] = "leave";
    root["ip_source"] = my_ip.toString().c_str();

    char data[LEN_PACKET];
    root.printTo(data);

#if debug
Serial.printf("Sending leave me (my_ip: %s)\n", my_ip.toString().c_str());
Serial.printf("Data: %s\n", data);
#endif

    udp_send(parent.ip, PORT, data);

    memcpy(wait_for_ack, parent.ip.toString().c_str(), strlen(parent.ip.toString().c_str()));
    positive_ack = false;

    delay(1000);

    WIMP::read(data);

#if debug
if (positive_ack) {
    Serial.printf("Received positive ack\n");
} else {
    Serial.printf("Received negative ack\n");
}
#endif

    return positive_ack;
}


/// Sends a change message to the parent and if everything goes ok changes the current parent
/// \param new_parent node that we want to become our new parent (destination of the message)
/// \param old_parent old parent of this node
/// \return true iff the change went ok
bool change_parent(IPAddress const& new_parent, IPAddress const& old_parent) {

    JsonObject& root = json_buffer.createObject();

    root["handle"] = "change";
    root["ip_source"] = my_ip.toString();
    root["ip_old_parent"] = old_parent.toString();
    root["test_id"] = test_id;
    test_id++;

    char data[LEN_PACKET];
    root.printTo(data);

#if debug
    Serial.printf("Sending change parent (my_ip: %s) (new_parent: %s) (old_parent: %s)\n", my_ip.toString().c_str(), new_parent.toString().c_str(), old_parent.toString().c_str());
    Serial.printf("Data: %s\n", data);
#endif

    udp_send(new_parent, PORT, data);
    //wait_for_ack = (char*) new_parent.toString().c_str();
    memcpy(wait_for_ack, new_parent.toString().c_str(), strlen(new_parent.toString().c_str()));

    //se faccio delay non verrà mai chiamata la read... devo chiamarla io
    positive_ack = false;
    //qui c'è un delay che dovrebbe salvarmi...
#if debug
    Serial.printf("Waiting for ack (my_ip: %s)\n", my_ip.toString().c_str());
#endif

    delay(1000);

    //WIMP::read(data);
    private_read(data);
    //se è false, dovrà essere chiamata di nuovo con un new parent

#if debug
    if (positive_ack) {
        Serial.printf("Received positive ack\n");
    } else {
        Serial.printf("Received negative ack or not received ack!\n");
    }
#endif

//    if (positive_ack) {
//        parent.ip = new_parent;
//    }

    //penso di doverlo fare ogni volta che costruisco un pacchetto, altrimenti si riempiono tutti i 512 byte
    json_buffer.clear();

    return positive_ack;
}


/// Calls all the needed functions for the management of the network
void WIMP::manage_network() {

    IPAddress broadcast(255, 255, 255, 255);

#if debug
Serial.printf("Sending hello in broadcast (my_ip: %s):\n", my_ip.toString().c_str());
#endif

    parent_answer = false;
    int _try = 0;

    //update liveness of children
    for (int i=0; i<num_children; ++i) {
        children[i].times_not_seen++;
    }

    while (!parent_answer && _try < 3) {
#if debug
Serial.printf("Preparing to send hello in broadcast, try = %d\n", _try);
#endif

        hello(broadcast, "hello");

        char data[LEN_PACKET];
#if debug
Serial.printf("Preparing to read answer (my_ip: %s):\n", my_ip.toString().c_str());
#endif

        delay(1000);

        for (int i=0; i<=num_children; ++i) {
            private_read(data);
            //cerco di essere sicuro di leggere la risposta di tutti -> = anche per il caso num_children=0
        }

        _try++;
    }

    if (!parent_answer) {
#if debug
Serial.printf("Parent didn't answer to hello (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Starting change parent\n");
#endif
        //TODO: direi che dovrei mandare il leave_me, prendere il primo nodo in neighbours != da parent e fare  il change
        //TODO: se non trovo un altro nodo dovrò rifare initialize/scan_network...
        leave_me();
        //non uso il valore di ritorno, tanto suppongo non mi risponda
        bool connected = false;
        int i = 0;
        shit_sort(neighbours, num_neighbours);

        WiFi.disconnect();
#if debug
        Serial.printf("Disconnected from previous ap (parent)\n");
#endif

        while (!connected && i < num_neighbours) {
            WiFi.begin(neighbours[i].ssid.c_str(), password);
            int j = 0;
#if debug
Serial.printf("Starting connection to %s\n", neighbours[i].ssid.c_str());
#endif
            while (WiFi.status() != WL_CONNECTED && j < 20) {
                delay(600);
                j++;
            }

            if (WiFi.status() == WL_CONNECTED) {
#if debug
Serial.println("Connected!");
#endif
                if (change_parent(neighbours[i].ip, parent.ip)) {
#if debug
Serial.printf("Change parent went ok, updating info!\n");
#endif
                    parent = neighbours[i];
                    connected = true;
                } else {
#if debug
Serial.println("I have not been accepted!");
#endif
                    i++;
                }
            } else {
#if debug
Serial.println("Not connected!");
#endif
                i++;
            }
        }

        if (!connected) {
            //devo reiniziare l'inizializzazione
#if debug
Serial.println("Worst case scenario, I have to restart initialization!");
#endif
            num_neighbours = 0;
            WIMP::initialize();
        }

    } else {
#if debug
Serial.printf("Parent ansered to hello (my_ip: %s):\n", my_ip.toString().c_str());
#endif
    }

    for (int i=0; i<num_children; ++i) {
        if (children[i].times_not_seen >= MAX_TIME_NOT_SEEN) {
            //remove children
#if debug
Serial.printf("My child %s is dead, I'm removing it...");
#endif
            JsonObject& risp = json_buffer.createObject();
            risp["type"] = "network_changed";
            risp["operation"] = "removed_child";
            risp["ip_child"] = children[i].ip.toString().c_str();
            risp["ip_parent"] = my_ip.toString().c_str();
            char data[LEN_PACKET];
            risp.printTo(data);

            swap(children, i, num_children-1);//lo metto alla fine
            num_children--;//non considero più l'ultimo nodo

#if debug
Serial.println("Sending notification to sink...");
#endif

            //WIMP::send(data);
            udp_send(parent.ip, PORT, data);    //la send mi wrappa il forward children
#if debug
Serial.printf("Sent change in the net to the raspy\n");
#endif
            json_buffer.clear();
        }
    }

#if debug
Serial.printf("my parent: IP %s \t SSID: %s\t len_path: %d\t rssi: %d\t tns: %d\n",
        parent.ip.toString().c_str(), parent.ssid.c_str(), parent.len_path, parent.rssi, parent.times_not_seen);
Serial.printf("Number of children: %d\n", num_children);
for (int i=0; i<num_children; ++i) {
    Serial.printf("children[%d]: IP %s \t SSID: %s\t len_path: %d\t rssi: %d\t tns: %d\n",
                  i, children[i].ip.toString().c_str(), children[i].ssid.c_str(), children[i].len_path, children[i].rssi, children[i].times_not_seen);
}
Serial.printf("Number of neighbours: %d\n", num_neighbours);
for (int i=0; i<num_neighbours; ++i) {
    Serial.printf("Neighbours[%d]: IP %s \t SSID: %s\t len_path: %d\t rssi: %d\t tns: %d\n",
                  i, neighbours[i].ip.toString().c_str(), neighbours[i].ssid.c_str(), neighbours[i].len_path, neighbours[i].rssi, neighbours[i].times_not_seen);
}
#endif
}


/// Select the best node in neighbours to become the new (or first) parent
/// \return true iff the functon managed to find a parent
bool final_connect() {
#if debug
Serial.printf("Networks found (pre sorting):\n");
Serial.printf("IP \t\t SSID \t\t RSSI \t PATH \t LTS\n");
for (int i=0; i<num_neighbours; ++i) {
    Serial.printf("%s \t %s \t %d \t %d \t %d\n", neighbours[i].ip.toString().c_str(), neighbours[i].ssid.c_str(), neighbours[i].rssi, neighbours[i].len_path, neighbours[i].times_not_seen);
}
#endif

    shit_sort(neighbours, num_neighbours);

#if debug
Serial.printf("Networks found (post sorting):\n");
Serial.printf("IP \t\t SSID \t\t RSSI \t PATH \t LTS\n");
for (int i=0; i<num_neighbours; ++i) {
    Serial.printf("%s \t %s \t %d \t %d \t %d\n", neighbours[i].ip.toString().c_str(), neighbours[i].ssid.c_str(), neighbours[i].rssi, neighbours[i].len_path, neighbours[i].times_not_seen);
}
#endif

    //connect to ap with best path/signal
    bool connected = false;
    int i = 0;
    while (i < num_neighbours && !connected) {
        WiFi.begin(neighbours[i].ssid.c_str(), password);
        int _try = 0;
#if debug
Serial.printf("Trying to connect to: %s\n", neighbours[i].ssid.c_str());
#endif
        while (WiFi.status() != WL_CONNECTED && _try < 10) {
            Serial.printf(".");
            delay(500);
            _try++;
        }
        if (WiFi.status() != WL_CONNECTED) {
            //connection failed, try the next one
#if debug
Serial.printf("Failed to connect to %s\n", neighbours[i].ssid.c_str());
#endif
            i++;
        } else {
#if debug
Serial.printf("Connected to: %s\n", neighbours[i].ssid.c_str());
#endif
            //this is the final connection, ask to the node if I can become one of its children
            my_ip = WiFi.localIP();
#if debug
Serial.println("Sending a change parent");
#endif
            if (change_parent(neighbours[i].ip, my_ip)) {
                //change went ok
                parent = node_info { neighbours[i].ip, neighbours[i].ssid, neighbours[i].rssi, (uint8_t) (neighbours[i].len_path+1), 0 };
                hello(parent.ip, "hello");
                connected = true;
#if debug
Serial.printf("I have been accepted as child\n");
#endif
            } else {
#if debug
Serial.println("ERROR! My parent refused me.");
#endif
                i++;
            }
        }
    }
    if (!connected) {
#if debug
Serial.println("Not connected!");
#endif
        //no one as accepted me! :(
        delay(10000);   //wait a bit to not clog the network
        return false;
    }
    //I'm connected to a parent, everything great!
    return true;
}


/// Looks for AP in the net, calls method to find a parent
/// \return true iff the node managed to connect to another node and to find a parent
bool scan_network() {

    int n = -1;
    bool found = false;

    while (!found) {
        WiFi.scanNetworks(true);

#if debug
Serial.println("Scan started");
#endif

        while (n == -1) {
            n = WiFi.scanComplete();
            delay(250);
            if (n == 0) {
#if debug
Serial.println("No network found! Scan again");
#endif
                delay(2000);
                WiFi.scanNetworks(true);
                n = -1;
            }
        }
#if debug
Serial.printf("%d network(s) found\n", n);
#endif

        for (uint8_t i = 0; i < n; i++) {
#if debug
Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n", i + 1,
              WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
              WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "closed");
#endif

            if (WiFi.SSID(i).startsWith(ssid) && num_neighbours < MAX_NEIGHBOURS) {
                WiFi.begin(WiFi.SSID(i).c_str(), password);
#if debug
Serial.printf("Starting connection with %s\n", WiFi.SSID().c_str());
#endif
                int j = 0;
                while (WiFi.status() != WL_CONNECTED && j < 20) {
#if debug
Serial.printf(".");
#endif
                    delay(600);
                    j++;
                }

                if (WiFi.status() == WL_CONNECTED) {
#if debug
Serial.println("Connected!");
#endif
                    found = true;   //found at least a WIMP_ and I am connected
                    my_ip = WiFi.localIP();

                    neighbours[num_neighbours] = node_info{WiFi.gatewayIP(), WiFi.SSID(i), WiFi.RSSI(i),
                                                           MAX_PATH_LENGTH, 0};
                    //send hello to know the path (and let the other know you)
#if debug
Serial.printf("IP AP: %s\n", WiFi.softAPIP().toString().c_str());
Serial.printf("IP local: %s\n", WiFi.localIP().toString().c_str());
Serial.printf("Updated neighbours, num_neighbours=%d, gatewayIP=%s, ssid=%s, rssi=%d\n",
              num_neighbours, neighbours[num_neighbours].ip.toString().c_str(), neighbours[num_neighbours].ssid.c_str(), neighbours[num_neighbours].rssi);
Serial.printf("Sending hello to %s\n", WiFi.gatewayIP().toString().c_str());
#endif
                    num_neighbours++;
                    parent = neighbours[num_neighbours];    //TODO: controlla, dovrebbe valere solo finchè sono qui dentro
                    hello(WiFi.gatewayIP(), "hello");
                    //parent_answer = false;
                    char data[LEN_PACKET];

                    delay(1000);

                    WIMP::read(data);   //TODO: potrei cambiare con la private / potrebbero servirne di più con tanti child

                    WiFi.disconnect();
                    //reset parent
                    parent = default_parent;
                }
#if debug
else {
    Serial.println("Not connected");
}
#endif
            }
        }
        //scan completed, free memory
        WiFi.scanDelete();
        if (!found) {
#if debug
Serial.println("No WIMP AP found!");
#endif
            delay(5000);
            n = -1;
            //num_neighbours = 0;
        }
    }
    return final_connect();
}


void on_disconnect(const WiFiEventSoftAPModeStationDisconnected& event) {
    Serial.println("I'm disconnected! Check if it reconnects by itself, probably we should simply change ap");
}

void on_connect(const WiFiEventSoftAPModeStationConnected& event) {
    Serial.println("I'm connected!");
}

/// Initializes the network
void WIMP::initialize() {
//    std::random_device rd;
//    std::mt19937 gen(rd());
//    std::uniform_int_distribution<> dis(1, 254);

    //srand((uint) time(NULL));
    //auto chip_id = (uint8_t) dis(gen);
    //auto chip_id = static_cast<uint8_t>((rand() % 254) + 1);   //system_get_chip_id();
    //TODO: POSSO ACCEDERE AL MAC DEL NODO, POTREI COSTRUIRMI L'IP DA QUELLO
    uint8_t chip_id = 7;

#if debug
Serial.printf("Random generated ip: %d\n", chip_id);
#endif

    IPAddress ip(151, 151, 12, chip_id);
    IPAddress gateway(192,168,1,9);
    IPAddress subnet(255,255,255,0);
    //TODO: check hidden network
    WiFi.softAPConfig(ip, gateway, subnet);

    // da errore ma su arduino compila
    //stationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected(&on_disconnect);
    //stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&on_connect);

    sprintf(ssid_name, "WIMP_%d", chip_id);

#if debug
Serial.printf("SSID name: %s\n", ssid_name);
#endif
    WiFi.softAP(ssid_name, password);

    //WiFi.mode(WIFI_AP_STA); //problems?

#if debug
Serial.println("Starting scan network...");
#endif

    //prepare to listen for messages to that port
    udp.begin(PORT);

    while (!scan_network()) {
        //loop forever until it manages to connect to someone
        delay(5000);
    }
}
