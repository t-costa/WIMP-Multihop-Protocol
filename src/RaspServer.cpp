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

/*#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"*/
#include <nlohmann/json.hpp>

#include "RaspServer.h"

#define debug true
#define PORT 42100
#define LEN_PACKET 512

//using namespace rapidjson;

using json = nlohmann::json;


//child - parent
std::unordered_map<const char*, const char*> routes;
//initialize socket and structure
int socket_info;
struct sockaddr_in server;
struct sockaddr_in client;
socklen_t cli_len = sizeof(client);
char incoming_message[LEN_PACKET];
std::queue<const char*> buffer;
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
        //TODO: check
        std::reverse(parents.begin(), parents.end());
        for (auto par : parents) {
            std::cout << par << " -> ";
        }
        std::cout << child << std::endl;
    }

    //TODO: in realtà questo sarebbe un db, giusto per test
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
}


/// Removes a not valid route in the network
/// \param parent parent that has lost his child
/// \param child child that is changing the parent
void WIMP::remove_route(const char* parent, const char* child) {
    if (routes.find(child) == routes.end() || routes[child] != parent) {
        return; //non devo fare niente, l'info non c'è
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
std::cout << client.sin_addr.s_addr << std::endl;
std::cout << client.sin_port << std::endl;
#endif

#if debug
std::cout << "Sending a message to " << dest << " type:\n " << data << std::endl;
#endif
    cli_len = sizeof(client);
    sendto(socket_info, (const char *) data, strlen(data),
        MSG_CONFIRM, (const struct sockaddr *) &client,
            cli_len);
}


/// Reads an incoming message from a generic node
/// \param data buffer in witch there will be written the received data (if any)
/// \return number of bytes read, 0 if none for the application, -1 if an error occurred
int WIMP::read(char* data) {

    //TODO: aggiungi controllo su presenza vecchi messaggi, se c'è qualcosa
    //restituisci direttamente quelli, solo se il buffer

    ssize_t rec_len;
    rec_len = recvfrom(socket_info, (char *) incoming_message, LEN_PACKET, MSG_WAITALL, (struct sockaddr *) &client, &cli_len);

    if(rec_len  < 0) {      
        std::cerr << "Received failed!" << std::endl;
        return -1;
    }

    incoming_message[rec_len] = '\0';

#if debug
std::cout << "Received message: " << incoming_message << std::endl;
#endif
    //parse the json
    //Document doc;
    //doc.Parse(incoming_message);
    json doc;
    try {
        doc = json::parse(incoming_message);
    } catch  (json::parse_error e) {
        std::cerr << "Error in parsing!" << std::endl;
        return -1;
    }


    //assert(doc.IsObject());

    std::string handle = doc["handle"].get<std::string>();

    if (doc.find("handle") == doc.end()) {
        std::cerr << "Parsing errorm handle=NULL" << std::endl;
        return -1;
    }

    if (handle.compare("hello") == 0) {
#if debug
std::cout << "Parsing a HELLO message" << std::endl;
#endif
        //answer with an hello, let the others know you are the sink
        //TODO: check ip
        const char* hello_risp = R"({"handle":"hello_risp","ip":"172.24.1.1","path":0})";
        std::string dest = doc["ip"].get<std::string>();

        if (doc.find("ip") == doc.end()) {
            std::cerr << "Error in parsing hello, dest=NULL" << std::endl;
            return -1;
        }

        send_direct(dest.c_str(), hello_risp);
        return 0;   //no message for application
    }

    if (handle.compare("hello_risp") == 0) {
#if debug
std::cout << "Parsing a HELLO_RISP message (discarded)" << std::endl;
#endif
        //should be discarded
        return 0;
    }

    if (handle.compare("change") == 0) {
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

        WIMP::change_route("Sink", source.c_str());

        //TODO: se aggiungi altre strutture fai le op necessarie
        const char* ack = R"({"handle":"ack","type":"true"})";
        send_direct(source.c_str(), ack);
        return 0;
    }

    if (handle.compare("leave") == 0) {
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

#if debug
std::cout << "Source leave: " << source << std::endl;
#endif

        const char* ack = R"({"handle":"ack","type":"true"})";
        send_direct(source.c_str(), ack);
        return 0;            
    }

    if (handle.compare("forward_children") == 0) {
#if debug
std::cout << "Parsing a FORWARD_CHILDREN message (discarded)" << std::endl;
#endif
        //this message is simply discarded
        return 0;   
    }

    if (handle.compare("forward_parent") == 0) {
#if debug
std::cout << "Parsing a FORWARD_PARENT message" << std::endl;
#endif
        //I am the destination, message for application
        json data_value = doc["data"];

        if (doc.find("data") == doc.end()) {
            std::cerr << "Error in parsing forward_parent, data=NULL" << std::endl;
            return -1;
        }

        auto t = data_value.dump();

        memccpy(data, t.c_str(), '\0', data_value.size());

#if debug
std::cout << "Data: " << data << std::endl;
#endif

        std::string type = data_value["type"].get<std::string>();

        if (data_value.find("type") == data_value.end()) {
            std::cerr << "Error in parsing forward_parent, type=NULL" << std::endl;
            return -1;
        }

        if (type.compare("network_changed") == 0) {
            //it's management
            std::string op = data_value["operation"].get<std::string>();
            std::string child = data_value["ip_child"].get<std::string>();
            std::string parent = data_value["ip_parent"].get<std::string>();

            if (data_value.find("operation") == data_value.end()) {
                std::cerr << "Error in parsing forward_parent, op=NULL" << std::endl;
                return -1;
            }
            if (data_value.find("ip_child") == data_value.end()) {
                std::cerr << "Error in parsing forward_parent, child=NULL" << std::endl;
                return -1;
            }
            if (data_value.find("ip_parent") == data_value.end()) {
                std::cerr << "Error in parsing forward_parent, parent=NULL" << std::endl;
                return -1;
            }

            if (op.compare("new_child") == 0) {
                WIMP::change_route(parent.c_str(), child.c_str());
                return 0;
            }
            if (op.compare("removed_child") == 0) {
                WIMP::remove_route(parent.c_str(), child.c_str());
                return 0;
            }
            //sarebbe errore
            //return -1;
            std::cerr << "Unexpected error in parsing forward_parent" << std::endl;
            return -1;
        } else {
            //for application
            buffer.push(data);
            return (int) strlen(data);
        }
    }

    if (handle.compare("ack") == 0) {
#if debug
std::cout << "Parsing a ACK message" << std::endl;
#endif
        //TODO: per ora non uso ack positivo/negativo

        //bool type = doc["type"].get<bool>();

        wait_for_ack = false;

        return 0;
    }

    //this should be an error
    return -1;
}

/// Computes the (unique) path from the sink to a destination node
/// \param dest destination node for witch we want to know the path
/// \return vector with all the ordered hops that the packet has to do in
/// order to reach the destination
std::vector<const char*> get_path(const char* dest) {
    std::vector<const char*> path;

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
    std::vector<const char*> path = get_path(dest);
    //TODO: add check correctness path and type of j_vec
    json j_vec = path;

    document["handle"] = "forward_children";
    document["path"] = j_vec;

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(dest);
    //TODO: se non funziona, cambia con questo
    //inet_pton(AF_INET, dest, &(client.sin_addr.s_addr));
    client.sin_port = htons(PORT);

#if debug
    std::cout << client.sin_addr.s_addr << std::endl;
    std::cout << client.sin_port << std::endl;
    std::cout << document << std::endl;
#endif

    wait_for_ack = true;
    int _try = 0;
    while (wait_for_ack && _try < 5) {
        sendto(socket_info, document.get<std::string>().c_str(), strlen(data),
               MSG_CONFIRM, (const struct sockaddr *) &client,
               cli_len);

        char risp[LEN_PACKET];
        int n = read(risp);
        if (n > 0) {
            //TODO: should push data to application!
        }
        _try++;
    }

    return !wait_for_ack;
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
    //TODO: should clear address structure

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
    return true;
}