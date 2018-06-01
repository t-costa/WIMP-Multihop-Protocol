
namespace WIMP {

    /**
     * Initializes structures for communication
    */
    bool initialize(const char* ip_address);

    /**
     * Sends a message to the specific destination
    */
    bool send(const char* data, const char* dest);

    /**
     * Reads an incoming message from a generic node
    */
    int read(char* data);

}