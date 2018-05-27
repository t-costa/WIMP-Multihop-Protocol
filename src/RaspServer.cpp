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
std::unordered_map<std::string, std::string> routes;
std::unordered_map<int32_t, std::string> id_ip;
std::unordered_map<std::string, int32_t> ip_id;
std::vector<std::pair<std::string, int32_t>> children;
int num_children = 0;

//initialize socket and structure
int socket_info;
struct sockaddr_in server;
struct sockaddr_in client;
socklen_t cli_len = sizeof(client);
char my_ip[20];

//data for incoming messages
char incoming_message[LEN_PACKET];
//std::queue<const char*> buffer;
bool wait_for_ack = false;
std::string ip_to_ack;


/// Shows the current connections between nodes (parent-children)
void WIMP::show_routes() {
#if debug
    std::cout << "\nCalled show_routes" << std::endl;
#endif

    std::cout << "Paths from Raspberry to any other node:" << std::endl;

    for (auto c_p : routes) {
        std::string child = c_p.first;
        std::vector<std::string> parents;
        
        auto p = c_p.second;
        //parents.push_back(child);
        parents.push_back(p);
        bool error = false;
        while (!error && p != "Sink") {
            if (routes.find(p) == routes.end()) {
                //there is something wrong with the path...
                error = true;
            } else {
                p = routes[p];  //is the child of someone
                parents.push_back(p);   //add other step of the path
            }
        }

        if (error) {
            std::cout << "Error building the path of " << child << "... These are partial results:" << std::endl;
        }

        std::reverse(parents.begin(), parents.end());
        for (auto par : parents) {
            std::cout << par << " -> ";
        }
        std::cout << child << std::endl;
    }
}


/// Changes a route new child for a known parent or a new parent for a known child
/// \param parent parent of the new child
/// \param child new child arrived, or child that is changing parents
void WIMP::change_route(std::string const& parent, std::string const& child) {

#if debug
    std::cout << "\nCalled change_route, parent = " << parent << "\t child = " << child << std::endl;
#endif

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
void WIMP::remove_route(std::string const& parent, std::string const& child) {

#if debug
    std::cout << "\nCalled remove_route: parent = " << parent << "\t child = " << child << std::endl;
#endif

    if (routes.find(child) == routes.end() || routes[child] != parent) {
#if debug
std::cout << "o non c'è child, o ha un parent diverso, in ogni caso non faccio niente..." << std::endl;
WIMP::show_routes();
#endif
        return; //ok, no info
    }
    routes.erase(child);
#if debug
std::cout << "Removed child " << child << std::endl;
WIMP::show_routes();
#endif
    //it is possible that now others node are not reachable...
    //I don't change the others, in this way when the network recovers
    //itself is easy to change stuff
    //TODO: should inform cloud! (malfunctioning?)
}


/// Sends a message to a neighbour (esp directly connected)
/// \param dest IP of the destination
/// \param data message to be sent
void send_direct(const char* dest, const char* data) {

#if debug
    std::cout << "\nCalled send_direct: dest = " << dest << "\t data = " << data << std::endl;
#endif

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


/// Finds the position of the wanted child in children
/// \param child child we are looking for
/// \return index of the child if found, -1 otherwise
int get_child(std::string const& child) {

#if debug
    std::cout << "\nCalled get_chil: child = " << child << std::endl;
#endif

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

/// Computes the (unique) path from the sink to a destination node
/// \param dest destination node for witch we want to know the path
/// \return vector with all the ordered hops that the packet has to do in
/// order to reach the destination, empty vector if error
std::vector<std::string> get_path(std::string const& dest) {

#if debug
    std::cout << "\nCalled get_pat: dest = " << dest << std::endl;
#endif

    std::vector<std::string> path;

#if debug
std::cout << "Resolving path for: " << dest << std::endl;
WIMP::show_routes();
#endif

#if debug
for (auto cp : routes) {
    if (cp.first == dest) {
        std::cout << "cp.first = " << cp.first << " ed è uguale a " << dest << "." << std::endl;
    } else {
        std::cout << "cp.first = " << cp.first << " ed è diverso da " << dest << "." << std::endl;
    }
    if (cp.second == dest) {
        std::cout << "cp.second = " << cp.second << " ed è uguale a " << dest << "." << std::endl;
    } else {
        std::cout << "cp.second = " << cp.second << " ed è diverso da " << dest << "." << std::endl;
    }
}
#endif

    if (routes.find(dest) == routes.end()) {
#if debug
std::cout << "dest not found, returning empty" << std::endl;
#endif
        return path;
    }

    std::string p = routes[dest];
    bool error = false;

    while (!error && p != "Sink") {
        if (routes.find(p) == routes.end()) {
            error = true;
        } else {
            path.push_back(p);
            p = routes[p];
        }
    }

    if (error) {
        std::cerr << "the destination is not reachable" << std::endl;
        path.clear();
        return path;
    }

    std::reverse(path.begin(), path.end());

    //add final destination
    path.push_back(dest);

    return path;
}


/// Reads an incoming message from a generic node
/// \param data buffer in witch there will be written the received data (if any)
/// \return number of bytes read, 0 if none for the application, -1 if an error occurred
int WIMP::read(char* data) {

#if debug
    std::cout << "\nCalled read" << std::endl;
#endif

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
        std::cerr << "Error in parsing json!" << std::endl;
        return -1;
    }

    if (doc.find("handle") == doc.end()) {
        std::cerr << "Parsing error handle=NULL" << std::endl;
        return -1;
    }

    std::string handle = doc["handle"].get<std::string>();

    if (handle == "hello") {
#if debug
std::cout << "Parsing a HELLO message" << std::endl;
#endif
        //answer with an hello, let the others know you are the sink
        json hello_doc;
        hello_doc["handle"] = "hello_risp";
        hello_doc["ip"] = my_ip;
        hello_doc["path"] = 0;
        hello_doc["ssid"] = "WIMP_0";
        hello_doc["unique_id"] = 0;
        const char* hello_risp = hello_doc.dump().c_str();
        //const char* hello_risp = R"({"handle":"hello_risp","ip":"172.24.1.1","path":0,"ssid":"WIMP_0","unique_id":0})";

        if (doc.find("ip") == doc.end()) {
            std::cerr << "Error in parsing hello, dest=NULL" << std::endl;
            return -1;
        }

        if (doc.find("unique_id") == doc.end()) {
            std::cerr << "Error in parsing hello, id=NULL" << std::endl;
            return -1;
        }

        std::string dest = doc["ip"].get<std::string>();
        uint32_t id = doc["unique_id"].get<int32_t>();

        //add connection id-ip
        id_ip[id] = dest;
        ip_id[dest] = id;

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
        hello_doc.clear();
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
        //const Value& old = doc["ip_old_parent"];
        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing change, source=NULL" << std::endl;
            return -1;
        }

        std::string source = doc["ip_source"].get<std::string>();

        if (get_child(source) != -1) {
            //era già mio figlio, deve essergli successo qualcosa, povero cucciolo
            //const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
            json ack_doc;
            json json_vec = get_path(source);
            ack_doc["handle"] = "ack";
            ack_doc["ip_source"] = my_ip;
            ack_doc["type"] = false;
            ack_doc["path"] = json_vec; //è direttamente mio vicino
            const char* ack = ack_doc.dump().c_str();
            send_direct(source.c_str(), ack);
            ack_doc.clear();
            return 0;
        }

        if (num_children >= MAX_CHILDREN) {
            //can't have more children
#if debug
std::cout << "Received a change request, but I have too many children!" << std::endl;
#endif
            //send negative ack
            json ack_doc;
            json json_vec = get_path(source);
            ack_doc["handle"] = "ack";
            ack_doc["ip_source"] = my_ip;
            ack_doc["type"] = false;
            ack_doc["path"] = json_vec; //è direttamente mio vicino
            const char* ack = ack_doc.dump().c_str();
            //const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"false"})";
            send_direct(source.c_str(), ack);
            return 0;
        }

        WIMP::change_route("Sink", source.c_str());
        children.emplace_back(source, 0);
        num_children++;

        json ack_doc;
        json json_vec = get_path(source.c_str());
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        ack_doc["path"] = json_vec; //è direttamente mio vicino
        const char* ack = ack_doc.dump().c_str();

        std::cout << "Invio ack" << ack << std::endl;

        //const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
        send_direct(source.c_str(), ack);
        get_path(source.c_str());
        return 0;
    }

    if (handle == "leave") {
#if debug
std::cout << "Parsing a LEAVE message" << std::endl;
#endif
        //someone wants to leave me (really??)
        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing leave, source=NULL" << std::endl;
            return -1;
        }
        std::string source = doc["ip_source"].get<std::string>();

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
        json ack_doc;
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        //const char* ack = R"({"handle":"ack","ip_source":"172.24.1.1","type":"true"})";
        const char* ack = ack_doc.dump().c_str();

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
        json data_value = doc["data"];
        //data = doc["data"].get<std::string>();
        const char* msg = data_value.dump().c_str();
        for (size_t i=0; i<strlen(msg); ++i) {
            data[i] = msg[i];
        }
#if debug
std::cout << "Data: " << data << std::endl;
#endif
        //for application
        //buffer.push(data);
        std::string source = doc["ip_source"].get<std::string>();
        json ack_doc;
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        std::vector<std::string> path = get_path(source);
        json j_vec = path; //get_path(source.c_str());
        ack_doc["path"] = j_vec;

        const char* ack = ack_doc.dump().c_str();

#if debug
std::cout << "Sending" << ack << " to " << source << std::endl;
#endif

//        if (path.empty()) {
//            send_direct(source.c_str(), ack);   //è direttamente collegato
//        } else {
//            send_direct(path[0], ack);
//        }
        send_direct(path[0].c_str(), ack);

        return (int) strlen(data);
    }

    if (handle == "ack") {
#if debug
std::cout << "Parsing a ACK message" << std::endl;
#endif

        //bool type = doc["type"].get<bool>();

        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing ack message, ip_source=NULL" << std::endl;
            return -1;
        }
        if (doc.find("type") == doc.end()) {
            std::cerr << "Error in parsing ack message, type=NULL" << std::endl;
            return -1;
        }

        bool type = doc["type"].get<bool>();
        std::string source = doc["ip_source"].get<std::string>();

#if debug
if (type) {
    std::cout << "Received positive ack ";
} else {
    std::cout << "Received negative ack ";
}
std::cout << " from " << source << std::endl;
#endif

        if (type && source == ip_to_ack) {
            wait_for_ack = false;
        }

        return 0;
    }

    //this should be an error
#if debug
std::cerr << "qualquadra non cosa" << std::endl;
#endif
    return -1;
}


/// Sends a message to the specific destination
/// \param data message to be sent
/// \param dest destination node
/// \return true iff the node has been correctly sent and received by the destination
bool WIMP::send(const char* data, const char* dest) {

#if debug
    std::cout << "\nCalled send (ip): data = " << data << "\t dest = " << dest << std::endl;
#endif

    //build json with forward_children and path
    //document is the root of a json message
    json document;

#if debug
std::cout << "Original message to be sent: " << data << std::endl;
#endif

    std::vector<std::string> path = get_path(dest);

    document["handle"] = "forward_children";
    json j_vec;

    if (path.empty()) {
        //TODO: dovrebbero esserci funzioni per vedere se un
        //TODO: dato ip è un indirizzo di broadcast...
        if (strcmp(dest, "255.255.255.255") != 0) {
            std::cerr << "Error in searching the path, dest not found and not broadcast" << std::endl;
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
        j_vec = path;
    }

    document["path"] = j_vec;
    document["data"] = data;

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(dest);
    //inet_pton(AF_INET, dest, &(client.sin_addr.s_addr));
    client.sin_port = htons(PORT);

    const char* payload = document.dump().c_str();


#if debug
std::cout << "Message to be sent (json object): " << document << std::endl;
std::cout << "Message to be sent (actual message): " << payload << std::endl;
#endif

    wait_for_ack = true;
    ip_to_ack = dest;   //TODO: check

#if debug
std::cout << "dest: " << dest << std::endl;
std::cout << "ip_to_ack: " << ip_to_ack << std::endl;
#endif

    int _try = 0;
    //TODO: per ora la send è singola, quindi aspetto un ack da un solo ip
    while (wait_for_ack && _try < 5) {
        sendto(socket_info, payload, strlen(data),
               MSG_CONFIRM, (const struct sockaddr *) &client,
               cli_len);

        sleep(3);    //the read thread will change stuff

        _try++;
    }

    return !wait_for_ack;
}

bool WIMP::send(const char *data, int dest) {

#if debug
    std::cout << "\nCalled send (ID): data " << data << "\t dest: " << dest << std::endl;
#endif

    if (id_ip.find(dest) == id_ip.end()) {
#if debug
std::cerr << "Destination" << dest << " not found!" << std::endl;
#endif
        return false;
    }

    std::string ip = id_ip[dest];

    return WIMP::send(data, ip.c_str());
}


//TODO: POSSONO ESSERCI PROBLEMI DI MULTITHREADING CON CHILDREN!
/// Endless loop that checks if my children are alive, if not, it removes them
void check_alive() {

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

        //giusto per non avere la chiazza gialla del loop che non termina...
        if (num_children > MAX_CHILDREN) {
#if debug
std::cout << "qualcosa di impossibile è appena successa..." << std::endl;
#endif
            break;
        }
    }
}


/// Initializes the data structures used by the library
/// \return true iff the initialization has been completed without errors
bool WIMP::initialize(const char* ip) {

    struct sockaddr_in sa;
    if (inet_pton(AF_INET, ip, &(sa.sin_addr)) <= 0) {
        std::cerr << "Invalid ip address" << std::endl;
        return false;
    }

    size_t ip_len = strlen(ip);
    for (size_t i=0; i<ip_len; ++i) {
        my_ip[i] = ip[i];
    }
    my_ip[ip_len] = '\0';

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
std::cout << "Starting child management thread..." << std::endl;
#endif

    std::thread aliveness(check_alive);
    aliveness.detach();

    return true;
}