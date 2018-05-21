#include <vector>
#include <iostream>
#include <unordered_map>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdlib>
#include <netinet/in.h>
#include <cstring>
#include <arpa/inet.h>
#include <queue>
#include <algorithm>

#include <nlohmann/json.hpp>
#include <thread>

#include "RaspServer.h"

#define debug true
#define PORT 42100
#define LEN_PACKET 512

#define MAX_CHILDREN 1

using json = nlohmann::json;


//child - parent
std::unordered_map<const char*, const char*> routes;
std::unordered_map<int32_t, const char*> id_ip;
std::unordered_map<const char*, int32_t> ip_id;
//initialize socket and structure
int socket_info;
struct sockaddr_in server;
struct sockaddr_in client;
socklen_t cli_len = sizeof(client);
char incoming_message[LEN_PACKET];
std::queue<const char*> buffer;
std::vector<std::pair<std::string, int32_t>> children;
int num_children = 0;
bool wait_for_ack = false;


/// Shows the current connections between nodes (parent-children)
void WIMP::show_routes() {

    std::cout << "Paths from Raspberry to any other node:" << std::endl;

    for (auto c_p : routes) {
        const char* child = c_p.first;
        std::vector<const char*> parents;
        
        auto p = c_p.second;
        //parents.push_back(child);
        parents.push_back(p);

        while (strcmp(p, "Sink") != 0) {
            p = routes[p];  //is the child of someone
            parents.push_back(p);   //add other step of the path
        }
        std::reverse(parents.begin(), parents.end());
        for (auto par : parents) {
            std::cout << par << " -> ";
        }
        std::cout << child << std::endl;
    }
}


/// Changes a route (change of parent or children) or adds a new route
/// \param parent parent of the new child
/// \param child new child arrived, or child that is changing parents
void WIMP::change_route(const char* parent, const char* child) {

    if (routes.find(child) == routes.end()) {
        //new child
        routes.emplace(child, parent);
#if debug
    std::cout << "Change of a unknown child:\n" <<
    " child: " << child <<
    "\n new parent: " << parent << std::endl;
#endif
    } else {
        //known child
        auto old_parent = routes[child];    //forse non mi serve l'old parent
        routes[child] = parent;
#if debug
    std::cout << "Change of a known child:\n" <<
    " child: " << child <<
    "\n old parent: " << old_parent <<
    "\n new parent: " << parent << std::endl;
#endif
    }
#if debug
WIMP::show_routes();
#endif
}


/// Removes a not valid route in the network
/// \param parent parent that has lost his child
/// \param child child that is changing the parent
void WIMP::remove_route(const char* parent, const char* child) {
    if (routes.find(child) == routes.end() || routes[child] != parent) {
        return; //ok, no info
    }
    routes.erase(child);
}


/// Sends a message to a neighbour (esp directly connected)
/// \param dest IP of the destination
/// \param data message to be sent
void send_direct(const char* dest, const char* data) {
    //send udp message through socket (it's a neighbour)

    client.sin_family = AF_INET;
    //client.sin_addr.s_addr = inet_addr(dest);
    inet_pton(AF_INET, dest, &(client.sin_addr));
    client.sin_port = htons(PORT);

#if debug
std::cout << "Sending a message to " << dest << " type:\n " << data << std::endl;
#endif
    cli_len = sizeof(client);
    sendto(socket_info, data, strlen(data),
        MSG_CONFIRM, (const struct sockaddr *) &client,
            cli_len);
}


int get_child(std::string child) {
    bool found = false;
    int i=0;
    while (!found && i<num_children) {
        if (children[i].first == child) {
            found = true;
        } else {
            ++i;
        }
    }

    return found ? i : -1;
}

//TODO: dovrai prendere l'ip in qualche modo!
/// Reads an incoming message from a generic node
/// \param data buffer in witch there will be written the received data (if any)
/// \return number of bytes read, 0 if none for the application, -1 if an error occurred
int WIMP::read(char* data) {

    ssize_t rec_len;
    rec_len = recvfrom(socket_info, (char *) incoming_message, LEN_PACKET, MSG_WAITALL, (struct sockaddr *) &client, &cli_len);

    if(rec_len  < 0) {
#if debug
std::cerr << "Received failed!" << std::endl;
#endif
        return -1;
    }

    incoming_message[rec_len] = '\0';

#if debug
std::cout << "Received message: " << incoming_message << std::endl;
#endif

    //parse the json
    json doc;
    try {
        doc = json::parse(incoming_message);
    } catch  (json::parse_error &e) {
        std::cerr << "Error in parsing!" << std::endl;
        return -1;
    }

    std::string handle = doc["handle"].get<std::string>();

    if (doc.find("handle") == doc.end()) {
        std::cerr << "Parsing error handle=NULL" << std::endl;
        return -1;
    }

    if (handle == "hello") {
#if debug
std::cout << "Parsing a HELLO message" << std::endl;
#endif
        //answer with an hello, let the others know you are the sink
        const char* hello_risp = R"({"handle":"hello_risp","ip":"172.24.1.1","path":0,"ssid":"WIMP_0","unique_id":0})";
        std::string dest = doc["ip"].get<std::string>();
        uint32_t id = doc["unique_id"].get<int32_t>();

        if (doc.find("ip") == doc.end()) {
            std::cerr << "Error in parsing hello, dest=NULL" << std::endl;
            return -1;
        }

        //add connection id-ip
        id_ip[id] = dest.c_str();
        ip_id[dest.c_str()] = id;

        //update liveness children
        bool found = false;
        int i = 0;
        while (i < num_children && !found) {
            if (children[i].first == dest) {
                children[i].second = 0;
                found = true;
            } else {
                ++i;
            }
        }

        send_direct(dest.c_str(), hello_risp);
        return 0;   //no message for application
    }

    if (handle == "hello_risp") {
#if debug
std::cout << "Parsing a HELLO_RISP message (discarded)" << std::endl;
#endif
        //should be discarded
        return 0;
    }

    if (handle == "change") {
#if debug
std::cout << "Parsing a CHANGE message" << std::endl;
#endif
        //someone wants me as parent
        std::string source = doc["ip_source"].get<std::string>();
        //const Value& old = doc["ip_old_parent"];
        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing change, source=NULL" << std::endl;
            return -1;
        }

        if (get_child(source) != -1) {
            //era già mio figlio, deve essergli successo qualcosa, povero cucciolo
            const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
            send_direct(source.c_str(), ack);
            return 0;
        }

        if (num_children >= MAX_CHILDREN) {
            //can't have more children
#if debug
std::cout << "Received a change request, but I have too many children!" << std::endl;
#endif
            //send negative ack
            const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"false"})";
            send_direct(source.c_str(), ack);
            return 0;
        }

        WIMP::change_route("Sink", source.c_str());
        children.emplace_back(source, 0);
        num_children++;
        const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
        send_direct(source.c_str(), ack);
        return 0;
    }

    if (handle == "leave") {
#if debug
std::cout << "Parsing a LEAVE message" << std::endl;
#endif
        //someone wants to leave me (really??)
        std::string source = doc["ip_source"].get<std::string>();
        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing leave, source=NULL" << std::endl;
            return -1;
        }
        routes.erase(source.c_str());
        //remove child
        auto i_remove = children.end();
        for (auto it = children.begin(); it != children.end(); it++) {
            if ((*it).first == source) {
                i_remove = it;
            }
        }
        if (i_remove != children.end()) {
            children.erase(i_remove);
            num_children--;
        }

#if debug
std::cout << "Source leave: " << source << std::endl;
#endif

        const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
        send_direct(source.c_str(), ack);
        return 0;            
    }

    if (handle == "forward_children") {
#if debug
std::cout << "Parsing a FORWARD_CHILDREN message (discarded)" << std::endl;
#endif
        //this message is simply discarded
        return 0;   
    }

    if (handle == "network_changed") {
#if debug
std::cout << "Parsing a NETWORK_CHANGED message" << std::endl;
#endif
        if (doc.find("operation") == doc.end()) {
            std::cerr << "Error in parsing network_changed, operation=NULL" << std::endl;
            return -1;
        }
        if (doc.find("ip_child") == doc.end()) {
            std::cerr << "Error in parsing network_changed, ip_child=NULL" << std::endl;
            return -1;
        }
        if (doc.find("ip_parent") == doc.end()) {
            std::cerr << "Error in parsing network_changed, ip_parent=NULL" << std::endl;
            return -1;
        }

        std::string op = doc["operation"].get<std::string>();
        std::string child = doc["ip_child"].get<std::string>();
        std::string parent = doc["ip_parent"].get<std::string>();

        if (op == "new_child") {
            WIMP::change_route(parent.c_str(), child.c_str());
            return 0;
        }
        if (op == "removed_child") {
            WIMP::remove_route(parent.c_str(), child.c_str());
            return 0;
        }
#if debug
std::cerr << "Unexpected error in parsing network_changed" << std::endl;
#endif
        return -1;

    }

    if (handle == "forward_parent") {
#if debug
std::cout << "Parsing a FORWARD_PARENT message" << std::endl;
#endif

        if (doc.find("data") == doc.end()) {
            std::cerr << "Error in parsing forward_parent, data=NULL" << std::endl;
            return -1;
        }

        //I am the destination, message for application
        //json data_value = doc["data"];
        std::string data_value = doc["data"].get<std::string>();

#if debug
std::cout << "Data: " << data << std::endl;
#endif
        //for application
        buffer.push(data);
        return (int) strlen(data);
    }

    if (handle == "ack") {
#if debug
std::cout << "Parsing a ACK message" << std::endl;
#endif
        //TODO: per ora non uso ack positivo/negativo

        //bool type = doc["type"].get<bool>();

        wait_for_ack = false;

        return 0;
    }

    //this should be an error
#if debug
std::cerr << "qualquadra non cosa" << std::endl;
#endif
    return -1;
}

/// Computes the (unique) path from the sink to a destination node
/// \param dest destination node for witch we want to know the path
/// \return vector with all the ordered hops that the packet has to do in
/// order to reach the destination
std::vector<const char*> get_path(const char* dest) {
    std::vector<const char*> path;

    if (routes.find(dest) == routes.end()) {
        return path;
    }

    const char* p = routes[dest];
    while (!strcmp(p, "Sink")) {
        path.push_back(p);
        p = routes[p];
    }

    std::reverse(path.begin(), path.end());

    return path;
}


/// Sends a message to the specific destination
/// \param data message to be sent
/// \param dest destination node
/// \return true iff the node has been correctly sent and received by the destination
bool WIMP::send(const char* data, const char* dest) {

    //devo costruire il json con forward children e
    //il path che deve fare il messaggo
    // document is the root of a json message
    json document;

#if debug
std::cout << "Original message to be sent: " << data << std::endl;
#endif

    std::vector<const char*> path = get_path(dest);

    document["handle"] = "forward_children";
    json j_vec;

    if (path.empty()) {
        if (strcmp(dest, "255.255.255.255") != 0) {
            std::cerr << "Error in searching the path, dest not found" << std::endl;
            return false;
        }
        //send in broadcast
#if debug
std::cout << "Sending message in broadcast" << std::endl;
#endif
        path.push_back("broadcast");
        j_vec = path;
    } else {
#if debug
std::cout << "Sending message in unicast" << std::endl;
#endif
    }

    j_vec = path;
    document["path"] = j_vec;
    document["data"] = data;

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(dest);
    //inet_pton(AF_INET, dest, &(client.sin_addr.s_addr));
    client.sin_port = htons(PORT);

#if debug
std::cout << client.sin_addr.s_addr << std::endl;
std::cout << client.sin_port << std::endl;
std::cout << document << std::endl;
#endif

    const char* payload = document.dump().c_str();

    wait_for_ack = true;
    int _try = 0;
    while (wait_for_ack && _try < 5) {
        sendto(socket_info, payload, strlen(data),
               MSG_CONFIRM, (const struct sockaddr *) &client,
               cli_len);

        //sleep(5);    //o 5? -> il thread read si occuperà del resto
#if debug
wait_for_ack = false;
#endif
        _try++;
    }

    return !wait_for_ack;
}


/// Endless loop that checks if my children are alive, if not, it removes them
void check_alive() {

    //TODO: POSSONO ESSERCI PROBLEMI DI MULTITHREADING CON CHILDREN!

    while (true) {
        sleep(10);

#if debug
std::cout << "Started check_alive, incrementing counter" << std::endl;
#endif

        for (auto& c : children) {
            c.second++;
        }

#if debug
for (auto& c : children) {
    std::cout << "ip: " << c.first << " \t counter: " << c.second << std::endl;
}
#endif

        sleep(5);

        bool found = true;
        while (found) {
            auto i_remove = children.end();
            for (auto it = children.begin(); it != children.end(); it++) {
                if ((*it).second > 5) {
                    i_remove = it;
                }
            }
            if (i_remove != children.end()) {
#if debug
std::cout << "removing " << (*i_remove).first << std::endl;
#endif
                routes.erase((*i_remove).first.c_str());    //remove route
                children.erase(i_remove);
                num_children--;
            } else {
                found = false;  //esco se li ho trovati tutti
            }
        }

        sleep(10);
    }
}


/// Initializes the data structures used by the library
/// \return true iff the initialization has been completed without errors
bool WIMP::initialize() {
    socket_info = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_info == -1) {
        std::cerr << "Could not create socket" << std::endl;
        return false;
    }
#if debug
std::cout << "Socket created" << std::endl;
#endif

    memset(&server, 0, sizeof(server));
    memset(&client, 0, sizeof(client));

    //assign values
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( PORT );

#if debug
std::cout << server.sin_addr.s_addr << std::endl;
std::cout << server.sin_port << std::endl;
#endif

    //bind socket with ip
    if (bind(socket_info, (struct sockaddr *)&server, sizeof(server)) < 0) {
        std::cerr << "Connection error" << std::endl;
        return false;
    }
#if debug
std::cout << "Bind done" << std::endl;
#endif

    std::thread aliveness(check_alive);
    aliveness.detach();

    return true;
}