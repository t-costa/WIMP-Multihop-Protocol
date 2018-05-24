
/**
 * ci sarebbe da fare uno scriptino magari per settare le impostazioni
 * e far diventare il raspi un access point
*/

namespace WIMP {

    /**
     * Initializes structures for communication
    */
    bool initialize(const char* ip_address);

    /**
     * Shows the current connections between nodes (parent-children)
    */
    void show_routes();

    /**
     * Changes a route (change of parent or children) or adds the new route
    */
    void change_route(const char* parent, const char* child);

    /**
     * Removes a not valide route in the network
    */
    void remove_route(const char* parent, const char* child);

    /**
     * Sends a message to the specific destination
    */
    bool send(const char* data, const char* dest);

    /**
     * Sends a message to the specific node, through its unique id
     * @param data message to be sent
     * @param dest id of destination
     * @return true iff the message is correctly sent
     */
    bool send(const char* data, int dest);

    /**
     * Reads an incoming message from a generic node
    */
    int read(std::string data);

}