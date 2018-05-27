
public interface NetworkCommunication {

    /**
     * Initializes udp socket data structure
     * @param port port number for binding
     * @return true iff everything goes ok
     */
    boolean udpInitialize(int port);

    /**
     * Initializes tcp socket data structure
     * @param port port number for binding
     * @param det IP address of destination
     * @return true iff everything goes ok
     */
    boolean tcpInitialize(int port, String det);

    /**
     * Sends a message to another IP through udp socket (only to parent)
     * @param data message to be sent
     * @return true iff the send goes ok
     */
    boolean udpSend(String data);

    /**
     * Sends a message to the connected IP through TCP
     * @param data message to be sent
     * @return true iff the send goes ok
     */
    boolean tcpSend(String data);

    /**
     * Receives a message through the udp socket (blocking)
     * @param buffer buffer where to store the received ip
     * @return length of the received packet, -1 if error, 0 if managing packet
     */
    int udpReceive(byte[] buffer);

    /**
     * Receives a message through the tcp socket (blocking)
     * @param buffer string where to write the received message
     * @return length of the message received, -1 if error
     */
    int tcpReceive(String buffer);

}
