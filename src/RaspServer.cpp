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

#include <thread>
#include <mutex>
#include <shared_mutex>

#include <nlohmann/json.hpp>

#include "RaspServer.h"

#define debug true
#define PORT 42100
#define LEN_PACKET 512

#define MAX_CHILDREN 1

using json = nlohmann::json;


//child - parent
std::unordered_map<std::string, std::string> routes;
std::unordered_map<std::string, std::string> id_ip;
std::unordered_map<std::string, std::string> ip_id;
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
bool wait_for_ack = false;
std::string ip_to_ack;

std::mutex mutex_children, mutex_routes;

/// Shows the current connections between nodes (parent-children)
void show_routes() {

    std::cout << "Paths from Raspberry to any other node:" << std::endl;

    for (auto c_p : routes) {
        std::string child = c_p.first;
        std::vector<std::string> parents;
        
        auto p = c_p.second;
        parents.push_back(p);
        bool error = false;
        while (!error && p != "Sink") {
            if (routes.find(p) == routes.end()) {
                //there is something wrong with the path...
                error = true;
            } else {
                p = routes[p];          //is the child of someone
                parents.push_back(p);   //add other step of the path
            }
        }

        if (error) {
            std::cout << "Error building the path of " << child << "... These are partial results:" << std::endl;
        }

        std::reverse(parents.begin(), parents.end());
        for (auto const& par : parents) {
            std::cout << par << " -> ";
        }
        std::cout << child << std::endl;
    }
}


/// Changes a route new child for a known parent or a new parent for a known child
/// \param parent parent of the new child
/// \param child new child arrived, or child that is changing parents
void change_route(std::string const& parent, std::string const& child) {

    std::unique_lock<std::mutex> lock(mutex_routes);
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
        auto old_parent = routes[child];
        routes[child] = parent;
#if debug
std::cout << "Change of a known child:\n" <<
" child: " << child <<
"\n old parent: " << old_parent <<
"\n new parent: " << parent << std::endl;
#endif
    }
    lock.unlock();
#if debug
show_routes();
#endif
}


/// Removes a not valid route in the network
/// \param parent parent that has lost his child
/// \param child child that is changing the parent
void remove_route(std::string const& parent, std::string const& child) {

    if (routes.find(child) == routes.end() || routes[child] != parent) {
#if debug
std::cout << "Nothing to do..." << std::endl;
show_routes();
#endif
        return; //ok, no info
    }
    std::unique_lock<std::mutex> lock(mutex_routes);
    routes.erase(child);
    lock.unlock();
#if debug
std::cout << "Removed child " << child << std::endl;
show_routes();
#endif
    //it is possible that now others node are not reachable...
    //I don't change the others, in this way when the network recovers
    //itself is easy to change stuff
}


/// Sends a message to a neighbour (esp directly connected)
/// \param dest IP of the destination
/// \param data message to be sent
void send_direct(const char* dest, const char* data) {

    //send udp message through socket (it's a neighbour)

    client.sin_family = AF_INET;
    inet_pton(AF_INET, dest, &(client.sin_addr));
    client.sin_port = htons(PORT);

#if debug
std::cout << "Sending a message to " << dest << " data:\n " << data << std::endl;
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

    std::vector<std::string> path;

#if debug
std::cout << "Resolving path for: " << dest << std::endl;
show_routes();
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
        std::cerr << "Destination not reachable" << std::endl;
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
std::cout << "Waiting for new messages from WIMP nodes..." << std::endl;
#endif

    ssize_t rec_len;
    rec_len = recvfrom(socket_info, (char *) incoming_message, LEN_PACKET,
            MSG_WAITALL, (struct sockaddr *) &client, &cli_len);

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
        memset(incoming_message, 0, sizeof(incoming_message));
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
        //hello_doc["ssid"] = "WIMP_0";   //used with ESP8266
        hello_doc["unique_id"] = "0";
        const char* hello_risp = hello_doc.dump().c_str();

        if (doc.find("ip") == doc.end()) {
            std::cerr << "Error in parsing hello, dest=NULL" << std::endl;
            return -1;
        }

        if (doc.find("unique_id") == doc.end()) {
            std::cerr << "Error in parsing hello, id=NULL" << std::endl;
            return -1;
        }

        std::string dest = doc["ip"].get<std::string>();
        std::string id = doc["unique_id"].get<std::string>();

        //add connection id-ip
        id_ip[id] = dest;
        ip_id[dest] = id;

        //update liveness children
        bool found = false;
        int i = 0;
        std::unique_lock<std::mutex> lock(mutex_children);
        while (i < num_children && !found) {
            if (children[i].first == dest) {
                children[i].second = 0;
                found = true;
            } else {
                ++i;
            }
        }
        lock.unlock();

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
        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing change, source=NULL" << std::endl;
            return -1;
        }

        std::string source = doc["ip_source"].get<std::string>();

        if (get_child(source) != -1) {
            //it was already my child, something must have happened...
            json ack_doc;
            json json_vec = get_path(source);
            ack_doc["handle"] = "ack";
            ack_doc["ip_source"] = my_ip;
            ack_doc["type"] = false;
            ack_doc["path"] = json_vec;     //it's directly connected
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
            std::vector<std::string> path;
            path.emplace_back(source);
            json json_vec = path;       // directly connected
            ack_doc["handle"] = "ack";
            ack_doc["ip_source"] = my_ip;
            ack_doc["type"] = false;
            ack_doc["path"] = json_vec;
            const char* ack = ack_doc.dump().c_str();

            send_direct(source.c_str(), ack);
            return 0;
        }

        change_route("Sink", source);
        std::unique_lock<std::mutex> lock(mutex_children);
        children.emplace_back(source, 0);
        num_children++;
        lock.unlock();

        json ack_doc;
        json json_vec = get_path(source);
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        ack_doc["path"] = json_vec;     //directly connected
        const char* ack = ack_doc.dump().c_str();

        send_direct(source.c_str(), ack);
        get_path(source);
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

        json ack_doc;
        auto path = get_path(parent);   //it can be a distant node
        json json_vec = path;
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        ack_doc["path"] = json_vec;
        const char* ack = ack_doc.dump().c_str();


        if (op == "new_child") {
            change_route(parent, child);
            send_direct(path[0].c_str(), ack);
            return 0;
        }
        if (op == "removed_child") {
            remove_route(parent, child);
            send_direct(path[0].c_str(), ack);
            return 0;
        }
#if debug
std::cerr << "Unexpected error in parsing network_changed" << std::endl;
#endif
        ack_doc["type"] = false;
        ack = ack_doc.dump().c_str();
        send_direct(path[0].c_str(), ack);
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

        const char* msg = data_value.dump().c_str();
        for (size_t i=0; i<strlen(msg); ++i) {
            data[i] = msg[i];
        }
#if debug
std::cout << "Data for application: " << data << std::endl;
#endif

        std::string source = doc["ip_source"].get<std::string>();

        //building ack
        json ack_doc;
        ack_doc["handle"] = "ack";
        ack_doc["ip_source"] = my_ip;
        ack_doc["type"] = true;
        std::vector<std::string> path = get_path(source);
        json j_vec = path;
        ack_doc["path"] = j_vec;

        const char* ack = ack_doc.dump().c_str();

        send_direct(path[0].c_str(), ack);      //can be directly connected or not

        return (int) strlen(data);
    }

    if (handle == "ack") {
#if debug
std::cout << "Parsing a ACK message" << std::endl;
#endif

        if (doc.find("ip_source") == doc.end()) {
            std::cerr << "Error in parsing ack message, ip_source=NULL" << std::endl;
            return -1;
        }
        if (doc.find("type") == doc.end()) {
            std::cerr << "Error in parsing ack message, type=NULL" << std::endl;
            return -1;
        }

        auto type = doc["type"].get<bool>();
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
std::cerr << "Something's wrong..." << std::endl;
#endif
    return -1;
}


/// Sends a message to the specific destination
/// \param data message to be sent
/// \param dest IP of destination node
/// \return true iff the message has been correctly sent and received
bool WIMP::send(const char* data, const char* dest_id) {

    //build json with forward_children and path
    json document;

#if debug
std::cout << "Original message to be sent: " << data << std::endl;
#endif

    std::string dest = id_ip[dest_id];  //get ip of the node

    std::vector<std::string> path = get_path(dest);

    document["handle"] = "forward_children";
    json j_vec;

    if (path.empty()) {
        std::cerr << "Error in searching the path, dest not found!" << std::endl;
        return false;
    } else {
        j_vec = path;
    }

    document["path"] = j_vec;
    document["data"] = json::parse(data);

    std::string hop = path.front();

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(hop.c_str());
    client.sin_port = htons(PORT);

    const char* payload = document.dump().c_str();

#if debug
std::cout << "Message to be sent (json object): " << document << std::endl;
std::cout << "Message to be sent (actual message): " << payload << std::endl;
#endif

    wait_for_ack = true;
    ip_to_ack = dest;

#if debug
std::cout << "dest: " << dest << std::endl;
std::cout << "ip_to_ack: " << ip_to_ack << std::endl;
#endif

    int _try = 0;
    while (wait_for_ack && _try < 5) {
        sendto(socket_info, payload, strlen(payload),
               MSG_CONFIRM, (const struct sockaddr *) &client,
               cli_len);

        sleep(3);    //the read thread will change wait_for_ack

        _try++;
    }

    return !wait_for_ack;
}


/// Endless loop that checks if my children are alive, if not, it removes them
void check_alive() {

    while (true) {
        sleep(10);

#if debug
std::cout << "Started check_alive, incrementing counter" << std::endl;
#endif
        std::unique_lock<std::mutex> lock(mutex_children);
        for (auto& c : children) {
            c.second++;
        }

#if debug
for (auto& c : children) {
    std::cout << "ip: " << c.first << " \t counter: " << c.second << std::endl;
}
#endif
        lock.unlock();

        sleep(5);

        lock.lock();
        bool found = true;
        while (found) {
            auto i_remove = children.end();
            for (auto it = children.begin(); it != children.end(); it++) {
                if ((*it).second > 2) {
                    i_remove = it;
                }
            }
            if (i_remove != children.end()) {
#if debug
std::cout << "removing " << (*i_remove).first << std::endl;
#endif
                std::unique_lock<std::mutex> lock1(mutex_routes);
                routes.erase((*i_remove).first);    //remove route
                mutex_routes.unlock();
                children.erase(i_remove);
                num_children--;
            } else {
                found = false;      //found all the dead children
            }
        }
        lock.unlock();


        sleep(10);
    }
}


/// Initializes the data structures used by the library
/// \return true iff the initialization has been completed without errors
bool WIMP::initialize(const char* ip) {

    struct sockaddr_in sa{};
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