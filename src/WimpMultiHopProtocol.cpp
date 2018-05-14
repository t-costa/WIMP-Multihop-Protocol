#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "WiFiUdp.h"
#include <cstring>
#include <ctime>
#include "ArduinoJson.h"
#include "WimpMultiHopProtocol.h"

//arduino.h potrebbe non servire almeno per ora

#define PORT 42100
#define MAX_CHILDREN 1
#define MAX_NEIGHBOURS 10
#define LEN_BUFFER 10
#define LEN_PACKET 256
#define MAX_PATH_LENGTH 255

#define debug true

struct node_info {
    IPAddress ip;
    String ssid;
    int32_t rssi;
    uint8_t len_path;
    uint8_t times_not_seen;
};

#pragma region global_variables
//maybe they could be an association ID-IP
IPAddress my_ip;
node_info parent;
node_info children[MAX_CHILDREN];
node_info neighbours[MAX_NEIGHBOURS];   //dovrebbe essere ordinato per path_length e power signal!
//maybe I need also the raspberry IP
char buffer[LEN_BUFFER][LEN_PACKET];  //keep list of incoming messages (circular)
bool wait_for_ack = false, positive_ack = false;
bool message_waiting = false;
bool parent_answer = false;

int8_t first_message = 0;
int8_t last_message = 0;

uint8_t path_length = MAX_PATH_LENGTH; 

uint8_t num_children = 0, num_neighbours = 0;

WiFiUDP udp;
//const char* ssid = "WIMP_";
//const char* password = "7settete7&IPA";

const char* ssid = "Mi";
const char* password = "4e8b149679da";

StaticJsonBuffer<LEN_PACKET> json_buffer;
#pragma endregion

/**
 * Actual send of the packet with udp
*/
#pragma region udp_send
void udp_send(IPAddress ip_dest, int port_dest, char* data) {
    //TODO: potrebbe servire un check disponibiltà o robe del genere di esp
    udp.beginPacket(ip_dest, port_dest);
    udp.write(data);
    udp.endPacket();
}
#pragma endregion

/**
 * Search a specific ip in the given list
 * returns the index of the element in the list, -1 if not present
*/
#pragma region search
int search(IPAddress other, node_info list[], int length) {
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
#pragma endregion

/**
 * Reads a string until a \n or the end of file,
 * returns the line read
*/
#pragma region readline
/*char* readline(char* message, int length, int* start) {
    char res[64];
    int i = 0;
    
    while (*start < length && res[*start] != '\n') {
        res[i] = message[*start];
        i++;
        (*start)++;
    }

    return res;
}*/
#pragma endregion 

#pragma region swap
void swap(node_info* v, int i, int j) {
    node_info temp = v[i];
    v[i] = v[j];
    v[j] = temp;
}
#pragma endregion

#pragma region shit_sort
void shit_sort(node_info* v, int length) {
    
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
#pragma endregion

/**
 * Sends an ack to a node
 * TODO: need heavy refactoring!
 * unless it's just for management packet, in that case
 * it should not be in the library
 * flag indicates if it is a positive or negative ack
*/
#pragma region WIMP::ack
void WIMP::ack(IPAddress dest, bool flag) {
    char msg[64];
    JsonObject& ans = json_buffer.createObject();
    
    ans["handle"] = "ack";
    ans["type"] = flag ? "true" : "false";
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
#pragma endregion

/**
 * Parse a received ack and sets global variables
*/
#pragma region read_ack
void read_ack(JsonObject& root) {
    const char* result = root["type"];

    if (strcmp(result, "true")) {
        positive_ack = true;
    } else {
        positive_ack = false;
    }

#if debug
    if (positive_ack) {
        Serial.printf("Read a positive ack\n");
    } else {
        Serial.printf("Read a negative ack\n");
    }
#endif
    wait_for_ack = false;
}
#pragma endregion

/**
 * Parse the received message of type hello
*/
#pragma region read_hello
void read_hello(JsonObject& root) {
    //TODO: check parent answer
    //add timers for children
    //update infro
    //maybe change parent
    IPAddress other;
    uint8_t other_path;
	String s;
    //other.fromString(root["ip"].asString());
	root["ip"].printTo(s);
	other.fromString(s);
    other_path = root["path"];
    if (parent.ip.operator==(other)) {
        //update path length
#if debug
Serial.printf("Read an hello from parent:\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
        parent_answer = true;
        parent.len_path = other_path+1;
        parent.times_not_seen = 0;
    } else {
        int i = search(other, children, MAX_CHILDREN);
        if (i != -1) {
            //update info -> non sono sicuro serva...
#if debug
Serial.printf("Read an hello from children:\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
            children[i].len_path = (other_path < path_length+1) ? other_path : path_length+1;
            children[i].times_not_seen = 0;
        } else {
            i = search(other, neighbours, MAX_NEIGHBOURS);
            if (i != -1) {
#if debug
Serial.printf("Read an hello from neighbour (known):\n");
Serial.printf("IP: %s\n PATH: %d\n", other.toString().c_str(), other_path);
#endif
                neighbours[i].len_path = other_path;
                neighbours[i].times_not_seen = 0;
                //TODO: should update also the power signal!
            } else {
                //new neighbour, maybe I still don't know anyone
                Serial.println("New neighbourg");
                if (num_neighbours < MAX_NEIGHBOURS) {
                    //add it
                    //TODO: devo aggiungere ssid e rssi all'hello!
                    neighbours[num_neighbours] = node_info {other, "NULL", 0, other_path, 0};
                    num_neighbours++;
                }
#if debug
Serial.printf("Read an hello from neighbour (unknown):\n");
Serial.printf("IP: %s\n PATH: %d\nNum_neighbours: %d\n", other.toString().c_str(), other_path, num_neighbours);
#endif
            }
        }
    }
}
#pragma endregion

/**
 * Forwards the received packet to the specific child, or to the application
 * if I am the destination 
*/
#pragma region read_forward_children
int read_forward_children(char* complete_packet, JsonObject& root) {
    //nel campo IP avrà la lista da seguire
    JsonArray& ip_path = root["path"];
    if (ip_path.measureLength() == 0) {
        //I am the dest
        //read message and deliver
        const char* data = root["data"];
        int len_packet = root["data"].measureLength();  //TODO: check

        for (int i=0; i<len_packet; ++i) {
            buffer[last_message][i] = data[i];
        }

        last_message++;
        last_message = (last_message % LEN_BUFFER); //circular buffer

        message_waiting = true;
#if debug
Serial.printf("Read a forward children directed to me (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("DATA: %s\n", data);
#endif
        return len_packet;
    } else {
        String next = ip_path[0];
        if (next.equals("broadcast")) {
            //TODO: send to all children, message doesn't change
            //deliver data to application
            const char* data = root["data"];
            int len_packet = root["data"].measureLength();  //TODO: check

            for (int i=0; i<len_packet; ++i) {
                buffer[last_message][i] = data[i];
            }
            
            last_message++;
            last_message = (last_message % LEN_BUFFER); //circular buffer

            message_waiting = true;
#if debug
Serial.printf("Read a forward children broadcast (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("DATA: %s\n", data);
#endif
            for (int i=0; i<num_children; ++i) {
                udp_send(children[i].ip, PORT, complete_packet);
            }

            return len_packet;
        }
        //remove my ip from the path and forward to the right child
        IPAddress next_hop;
        next_hop.fromString((const char*) ip_path[0]);

        Serial.printf("Next hop: %s\n", next_hop.toString().c_str());

        //ip_path.removeAt(0);// deprecato, dovrei usare remove
        ip_path.remove(0);
        char new_data[LEN_PACKET];
        root.printTo(new_data);

#if debug
Serial.printf("Read a forward children NOT directed to me (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("DATA: %s\n", new_data);
#endif
        
        udp_send(next_hop, PORT, new_data);

        return 0;
    }            
}
#pragma endregion

/**
 * Forwards the received packet to my parent 
*/
#pragma region read_forward_parent
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
#pragma endregion

/**
 * A node wants me as father
*/
#pragma region read_change
void read_change(JsonObject& root) {
    //I received a change, add the node to children
    //if possible
    IPAddress new_child, old_par;
    new_child.fromString(root["ip_source"].asString());//asString() deprecato
    old_par.fromString(root["ip_old_parent"].asString());
    
    if (num_children == MAX_CHILDREN) {
        //send error, can't have more children
#if debug
Serial.printf("Read a change parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Refused because reached max_children\n");
#endif
        WIMP::ack(new_child, false);
    } else {
        //add to children and send positive ack
        //TODO: se old parent è uguale al mio parent, 
        //devo ricorsivamente chiamare old parent su un neighbour 
        //perchè non risolverei niente!

        children[num_children].ip = new_child;
        children[num_children].len_path = path_length+1;
        //TODO: send ack to children and change in the network to sink
        num_children++; 
#if debug
Serial.printf("Read a change parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Child accepted! num_children=%d\n", num_children);
#endif
        WIMP::ack(new_child, true);

        //inform sink
        JsonObject& root = json_buffer.createObject();
        root["type"] = "network_changed";
        root["operation"] = "new_child";
        root["ip_child"] = new_child.toString().c_str();
        root["ip_parent"] = my_ip.toString().c_str();
        char data[LEN_PACKET];
        root.printTo(data);

        WIMP::send(data);
#if debug
Serial.printf("Send change in the net to the raspi\n");
#endif
    }
}
#pragma endregion

/**
 * A child wants to leave me
*/
#pragma region read_leave
void read_leave(JsonObject& root) {
    //my child is a piece of shit, I have to kill it
    IPAddress ip;
    ip.fromString(root["ip_source"].as<char*>());//asString() deprecato
    
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
    obj["type"] = "network_changed";
    obj["operation"] = "removed_child";
    obj["ip_child"] = ip.toString().c_str();
    char data[LEN_PACKET];
    obj.printTo(data);

#if debug
Serial.printf("Read a leave parent (my_ip: %s):\n", my_ip.toString().c_str());
Serial.printf("Accepted, inform raspi\n");
#endif

    WIMP::send(data);
}
#pragma endregion

/**
 * Deliver an old received packet to the application
 * or NULL if there is no pending packet
*/
#pragma region WIMP::retrieve_packet
char* WIMP::retrieve_packet() {
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
#pragma endregion

/**
 * Sends to all the neighbours (parent+children+neighbours) info
 * on his IP, parent, number of children and path_lenght;
 * if parent doesn't answer, loop
 * type: HELLO or HELLO_RISP
*/
#pragma region hello
void hello(IPAddress dest, const char* type) {  
    char msg[LEN_PACKET];
    JsonObject& ans = json_buffer.createObject();
    
    ans["handle"] = type;
    ans["ip"] = my_ip.toString();
    ans["path"] = path_length;
    ans.printTo(msg);

#if debug
Serial.printf("Sending hello (my_ip: %s) (dest: %s)\n", my_ip.toString().c_str(), dest.toString().c_str());
Serial.printf("Data: %s\n", msg);
#endif

    //FIXME: la reference usa root.printTo(udp), forse così evito gli errori?

    udp_send(dest, PORT, msg);
    json_buffer.clear();

}
#pragma endregion

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
#pragma region WIMP::read
int WIMP::read(char* data) {

    //maybe need delay?
    int packet_size = udp.parsePacket();
    int i = 0;
#if debug
    Serial.printf("Waiting for arriving packet ... \n");
#endif
    while (!packet_size && i < 5) {
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

    //there is some packet
#if debug
    Serial.printf("Received packet: %d byte from %s ip, port %d\n", 
                    packet_size, 
                    udp.remoteIP().toString().c_str(), 
                    udp.remotePort());
#endif

    int len_packet = udp.read(data, LEN_PACKET);
    if (len_packet > 0 && len_packet < LEN_PACKET) {
        //non so se serve...
        data[len_packet] = '\0';
    }
    
    JsonObject& root = json_buffer.parseObject(data);
#if debug
Serial.printf("Text of received packet: \n%s", data);
root.prettyPrintTo(Serial); //credo...
#endif
    if (!root.success()) {
        Serial.println("Error in parsing received message!");
        return -1;
    }

    const char* handle = root["handle"];

    if (strcmp(handle, "hello") == 0) {
#if debug
Serial.println("Parsing a HELLO message");
#endif
        read_hello(root);
        //always answer
        hello(udp.remoteIP(), "hello_risp");//dice "deprecated conversion"
        return 0;   //no message for application
    }

    if (strcmp(handle, "hello_risp") == 0) {
#if debug
Serial.println("Parsing a HELLO_RISP message");
#endif
        read_hello(root);    
        return 0;
    }

    if (strcmp(handle, "change") == 0) {
#if debug
Serial.println("Parsing a CHANGE message");
#endif
        read_change(root);
        return 0;
    }

    if (strcmp(handle, "leave") == 0) {
#if debug
Serial.println("Parsing a LEAVE message");
#endif
        read_leave(root);
        return 0;            
    }

    if (strcmp(handle, "forward_children") == 0) {
#if debug
Serial.println("Parsing a FORWARD_CHILDREN message");
#endif
        return read_forward_children(data, root);   
    }

    if (strcmp(handle, "forward_parent") == 0) {
#if debug
Serial.println("Parsing a FORWARD_PARENT message");
#endif
        read_forward_parent(data);
        return 0;
    }

    if (strcmp(handle, "ack") == 0) {
#if debug
Serial.println("Parsing a ACK message");
#endif
        read_ack(root);
        return 0;
    }

    //this should be an error
    return -1;
}
#pragma endregion

/**
 * Send data to sink (and only to sink!)
 * loop is needed for reliability
 * returns true if also the ack is received, 
 * false if after some tries the packet is lost
*/
#pragma region WIMP::send
bool WIMP::send(char* data) {

    JsonObject& root = json_buffer.createObject();
    root["handle"] = "forward_parent";
    root["data"] = data;

    char msg[256];
    root.printTo(msg, root.measureLength() + 1);
    //msg[root.measureLength()] = '\0';   //serve?

#if debug
Serial.printf("Sending a message to raspi (my_ip: %s) (parent: %s):\n", my_ip.toString().c_str(), parent.ip.toString().c_str());
Serial.printf("DATA: %s\n", msg);
#endif

    udp_send(parent.ip, PORT, msg);

    return false;
}
#pragma endregion

/**
 * Calls all the needed functions for the first start (at least):
 * - hello()
 * - change_parent()
 * maybe it needs the local ip
*/
#pragma region manage_network
void WIMP::manage_network() {

    //dovrei avere almeno un parent sempre
    IPAddress broadcast(255, 255, 255, 255);

#if debug
Serial.printf("Sending hello in broadcast (my_ip: %s):\n", my_ip.toString().c_str());
#endif

    //manda hello a tutti (sarebbero parent e children in teoria)
    hello(broadcast, "hello");//dice "deprecated conversion"

    //teoricamente dovrei aspettare un po' e fare la read
    char data[LEN_PACKET];
    parent_answer = false;
#if debug
Serial.printf("Preparing to read answer (my_ip: %s):\n", my_ip.toString().c_str());
#endif

    read(data); //elabora risposte ricevute

    //TODO:
    if (!parent_answer) {
        //so cazzi, potrei provarci un'altra volta o fallire
        //direttamente e fare scan
#if debug
Serial.printf("Parent didn't answer to hello (my_ip: %s):\n", my_ip.toString().c_str());
#endif
    } else {
#if debug
Serial.printf("Parent ansered to hello (my_ip: %s):\n", my_ip.toString().c_str());
#endif
    }
    //altrimenti tutto ok, diciamo che finchè il parent risponde 
    //non lo cambio, anche se potrebbe esserci un nodo con path
    //minoreb  
}
#pragma endregion

/**
 * Removes "connection" with old_parent
 * asks to new_parent to became the new parent,
 * has to wait for ack;
 * if errors, it has to be called again with a new new_parent
*/
#pragma region change_parent
bool change_parent(IPAddress new_parent, IPAddress old_parent) {

    JsonObject& root = json_buffer.createObject();

    root["handle"] = "change";
    root["ip_source"] = my_ip.toString();
    root["ip_old_parent"] = old_parent.toString();

    char data[LEN_PACKET];
    root.printTo(data);

    parent.ip = new_parent; //non so se conviene farlo qui o nel chiamante

#if debug
Serial.printf("Sending change parent (my_ip: %s) (new_parent: %s) (old_parent: %s)\n", my_ip.toString().c_str(), new_parent.toString().c_str(), old_parent.toString().c_str());
Serial.printf("Data: %s\n", data);
#endif

    udp_send(new_parent, PORT, data);
    wait_for_ack = true;

    //se faccio delay non verrà mai chiamata la read... devo chiamarla io
    positive_ack = false;
    //qui c'è un delay che dovrebbe salvarmi...
#if debug
Serial.printf("Waiting for ack (my_ip: %s)\n", my_ip.toString().c_str());
#endif
    WIMP::read(data);
    //se è false, dovrà essere chiamata di nuovo con un new parent

#if debug
if (positive_ack) {
    Serial.printf("Received positive ack\n");
} else {
    Serial.printf("Received negative ack or not received ack!\n");
}
#endif

    return positive_ack;
}
#pragma endregion

/**
 * Look for AP in the net
 * stores parent and neighbours (not children)
 * done only at init
*/
#pragma region scan_network
void WIMP::scan_network() {
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
                Serial.println("No network found! Scan again");
                WiFi.scanNetworks(true);
                n = -1;
            }
        }
        //se non ci sono reti non esce mai dal loop...
#if debug
Serial.printf("%d network(s) found\n", n);
#endif

        for (int i = 0; i < n; i++) {
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
                while (WiFi.status() != WL_CONNECTED && j < 10) {
                    delay(500);
                    j++;
                }

                if (WiFi.status() == WL_CONNECTED) {
#if debug
Serial.println("Connected!");
#endif
                    found = true;   //found at least a WIMP_
                    my_ip = WiFi.localIP();

                    neighbours[num_neighbours] = node_info {WiFi.gatewayIP(), WiFi.SSID(i), WiFi.RSSI(i), MAX_PATH_LENGTH, 0};
                    //send hello to know the path (and let the other know you)
#if debug
Serial.printf("IP AP: %s\n", WiFi.softAPIP().toString().c_str());
Serial.printf("IP local: %s\n", WiFi.localIP().toString().c_str());
Serial.printf("Sending hello to %s\n", WiFi.gatewayIP().toString().c_str());
#endif
                    hello(WiFi.gatewayIP(), "hello");//dice "deprecated conversion"
                    parent_answer = false;
                    char data[LEN_PACKET];

                    read(data);
                    //non so se mi serve sapere se il parent mi ha risposto

                    WiFi.disconnect();
                    num_neighbours++;
                }
            }
        }
        WiFi.scanDelete();
        if (!found) {
#if debug
Serial.println("No WIMP AP found!");
#endif
            delay(5000);
            n = -1;
        }
    }



#if debug
Serial.printf("Networks found (pre sorting):\n");
Serial.printf("IP \t\t SSID \t RSSI \t PATh \t LTS\n");
for (int i=0; i<num_neighbours; ++i) {
    Serial.printf("%s \t %s \t %d \t %d \t %d\n", neighbours[i].ip.toString().c_str(), neighbours[i].ssid.c_str(), neighbours[i].rssi, neighbours[i].len_path, neighbours[i].times_not_seen);
}
#endif

    shit_sort(neighbours, num_neighbours);

#if debug
Serial.printf("Networks found (post sorting):\n");
Serial.printf("IP \t\t SSID \t RSSI \t PATh \t LTS\n");
for (int i=0; i<num_neighbours; ++i) {
    Serial.printf("%s \t %s \t %d \t %d \t %d\n", neighbours[i].ip.toString().c_str(), neighbours[i].ssid.c_str(), neighbours[i].rssi, neighbours[i].len_path, neighbours[i].times_not_seen);
}
#endif

    //mi collego al primo -> dovrebbe essere il migliore tra path e segnale
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
            //non sono riuscito a connettermi
            i++;
        } else {
#if debug
Serial.printf("Connected to: %s\n", neighbours[i].ssid.c_str());
#endif
            //ora sono connesso, devo farlo diventare mio padre
            my_ip = WiFi.localIP();
#if debug
            Serial.println("Sending a change parent");
#endif
            if (change_parent(neighbours[i].ip, my_ip)) {
                //cambio avvenuto correttamente
                parent = node_info { neighbours[i].ip, neighbours[i].ssid, neighbours[i].rssi, MAX_PATH_LENGTH, 0 };
                hello(parent.ip, "hello");  //prendo info sul path - deprecated conversion
                connected = true;
#if debug
Serial.printf("I have been accepted as child\n");
#endif
            } else {
                Serial.println("ERROR! My parent refused me.");
                i++;
            }
        }   
    }
    if (!connected) {
        Serial.println("Not connected!");
    }
    //sono connesso fisicamente al parent e conosco alcuni dei miei vicini, great!
}
#pragma endregion

/**
 * Initializes the network
*/
#pragma region initialize
void WIMP::initialize() {

    //FIXME:
    srand(static_cast<unsigned int>(time(nullptr)));
    uint8_t chip_id;
    chip_id = static_cast<uint8_t>((rand() % 254) + 1);   //system_get_chip_id();

#if debug
Serial.printf("Random generated ip: %d\n", chip_id);
#endif

    IPAddress ip(151, 151, 12, chip_id);
    IPAddress gateway(192,168,1,9);
    IPAddress subnet(255,255,255,0);
    //credo la rete dovrebbe essere hidden
    WiFi.softAPConfig(ip, gateway, subnet);


    //const char* ssid_name = ssid + chip_id; //funzionerà?
    char ssid_name[10];

    sprintf(ssid_name, "WIMP_%d", chip_id);
    /*if (chip_id >= 100) {
        //3
        ssid_name[5] = (chip_id/100) - '0';
        ssid_name[6] = (chip_id/10) - '0';
        ssid_name[7] = (chip_id%10) - '0';
        ssid_name[8] = '\0';
    } else {
        if (chip_id >= 10) {
            //2
            ssid_name[5] = (chip_id/10) - '0';
            ssid_name[6] = (chip_id%10) - '0';
            ssid_name[7] = '\0';
        } else {
            //1
            ssid_name[5] = chip_id - '0';
            ssid_name[6] = '\0';
        }
    }*/


#if debug
Serial.printf("SSID name: %s\n", ssid_name);
#endif
    WiFi.softAP(ssid_name, password);

    //WiFi.mode(WIFI_AP_STA); potrebbe dare problemi
    //parent = node_info{ my_ip, NULL, 150, MAX_PATH_LENGTH, 0 };

#if debug
    Serial.println("Starting scan network...");
#endif

    //prepare to listen for messages to that port
    udp.begin(PORT);

    WIMP::scan_network();

#if debug
    Serial.println("Starting first management");
#endif

    WIMP::manage_network();
}

/**
 * Asks the parent to remove me from his children
 * also this need a loop I guess
*/
#pragma region leave_me
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
    //forse un delay piccolo piccolo ci può stare...
    wait_for_ack = true;
    positive_ack = false;
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
#pragma endregion
