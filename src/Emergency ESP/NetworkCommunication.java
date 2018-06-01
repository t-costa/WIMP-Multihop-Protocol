
public interface NetworkCommunication {

    /**
     * Initializes the socket udp, sends first hello in broadcast, waits for answers
     * in a new thread, finds the first parent of the node and starts the managing thread
     * @param port port number for binding
     * @return true iff the initialization went ok and the application found a parent
     */
    boolean udpInitialize(int port);

    /**
     * Sends a message to the Sink of the net, wrapping it
     * in a forward_parent message
     * @param data message to be sent
     * @return true iff the message is correctly sent and acked
     */
    boolean udpSend(String data);

    /**
     * Blocks a thread until it receives a new UDP message,
     * discards useless/malformed messages, parse them and calls
     * the appropriate methods
     * @param buffer buffer where to store the received ip
     * @return The length of the received message if for the application,
     * 0 if it is a message of the protocol
     * -1 if an error occurs
     */
    int udpReceive(byte[] buffer);
}
