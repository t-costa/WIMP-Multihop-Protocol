import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.net.*;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;


public class NetworkClass implements NetworkCommunication {

    private DatagramSocket udpSocket;
    private int port;

    private List<NodeInfo> children;
    private List<NodeInfo> neighbours;
    private String parent;
    private final String myIp;
    private final String myId;
    private int numChildren;
    private int maxChildren;

    private AtomicBoolean receivedAns;
    private AtomicBoolean waitForAck;
    private AtomicBoolean positiveAck;
    private AtomicBoolean parentAnswer;
    private String ackSource;
    private boolean receiveCalled;

    private int pathLength;

    /**
     * Creates the class and sets private variables
     * @param maxChildren maximum number of children that the node can have
     * @param myIp IP address of the node
     */
    NetworkClass(int maxChildren, String myIp, String myId) {
        this.numChildren = 0;
        this.maxChildren = maxChildren;

        this.waitForAck = new AtomicBoolean(false);
        this.positiveAck = new AtomicBoolean(false);
        this.parentAnswer = new AtomicBoolean(false);
        this.receivedAns = new AtomicBoolean(false);
        this.receiveCalled = false;

        this.pathLength = 255;
        this.myIp = myIp;
        this.myId = myId;
        children = Collections.synchronizedList(new ArrayList<>(this.maxChildren));
        neighbours = Collections.synchronizedList(new ArrayList<>());
        parent = null;
    }


    /**
     * Initializes the socket udp, sends first hello in broadcast, waits for answers
     * in a new thread, finds the first parent of the node and starts the managing thread
     * @param port port number for binding
     * @return true iff the initialization went ok and the application found a parent
     */
    @Override
    public boolean udpInitialize(int port) {
        this.port = port;
        try {
            udpSocket = new DatagramSocket(port);
            udpSocket.setBroadcast(true);
        } catch (SocketException e) {
            System.err.println("Error in creating DatagramSocket");
            return false;
        }

        sendHello("hello", "255.255.255.255");

        Thread initializer = new Thread(() -> {
            byte[] data = new byte[512];
            try {
                while (true) {
                    udpReceive(data);
                    if (udpSocket.isClosed())
                        return;
                }
            } catch (Exception e) {
                e.printStackTrace();
            }

        });
        initializer.start();

        try {
            Thread.sleep(10000);
            udpSocket.close();  //kills initializer
            Thread.sleep(100);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        try {
            //rebuild socket
            udpSocket = new DatagramSocket(port);
            udpSocket.setBroadcast(true);
        } catch (SocketException e) {
            System.err.println("Error in creating DatagramSocket");
            return false;
        }

        //look in neighbours a parent
        receiveCalled = false;
        changeParent();

        if (!positiveAck.get()) {
            System.out.println("Error in initialization! Cannot find a parent!");
            udpSocket.close();
            return false;
        }

        Thread manager = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(30000);
                    parentAnswer.set(false);
                    System.out.println("Sending hello in broadcast!");
                    sendHello("hello", "255.255.255.255");
                    Thread.sleep(5000);   //there will be another thread blocked on reading
                } catch (InterruptedException e) {
                    e.printStackTrace();
                    return;
                }

                if (!parentAnswer.get()) {
                    System.out.println("parent is dead, change it");
                    changeParent();
                }

                ArrayList<Integer> toBeRemoved = new ArrayList<>();

                synchronized (children) {
                    for (NodeInfo nc : children) {
                        nc.incrementNotSeen();
                    }

                    for (int i = 0; i< numChildren; ++i) {
                        if (children.get(i).getNotSeen() == 5) {
                            System.out.println("Child " + children.get(i) + " is going to be removed");
                            toBeRemoved.add(i);
                        }
                    }

                    for (Integer i : toBeRemoved) {
                        children.remove((int) i);
                        sendNetChange(false, parent);   //inform sink of the change
                    }
                }
            }
        });

        manager.start();

        return true;
    }

    /**
     * Sends a message to the Sink of the net, wrapping it
     * in a forward_parent message
     * @param data message to be sent
     * @return true iff the message is correctly sent and acked
     */
    @Override
    public boolean udpSend(String data) {

        DatagramPacket p;
        InetAddress addr;

        JSONObject jsonObject = new JSONObject();
        JSONObject jsonMessage = new JSONObject(data);
        jsonObject.put("data", jsonMessage);
        jsonObject.put("ip_source", myIp);
        jsonObject.put("handle", "forward_parent");

        String toSend = jsonObject.toString();
        byte[] mex = toSend.getBytes();
        System.out.println("TEST: wrapped message: " + toSend);

        try {
            addr = InetAddress.getByName(parent);
        } catch (UnknownHostException e) {
            System.err.println("Error in creating packet, address unknown");
            return false;
        }

        p = new DatagramPacket(mex, mex.length, addr, port);

        positiveAck.set(false);
        waitForAck.set(true);
        ackSource = parent;
        int _try = 0;
        try {

            while (waitForAck.get() && _try < 2) {
                udpSocket.send(p);
                System.out.println("Message sent, attempt = " + _try);
                Thread.sleep(3000);
                _try++;
            }

        } catch (IOException e) {
            System.err.println("Error in sending the message! I/O exception.");
            return false;
        } catch (InterruptedException e) {
            System.err.println("Received an interrupted exception while waiting for an answer.");
            return positiveAck.get();
        }

        return positiveAck.get();
    }

    /**
     * Blocks a thread until it receives a new UDP message,
     * discards useless/malformed messages, parse them and calls
     * the appropriate methods
     * @param buffer buffer where to store the received ip
     * @return The length of the received message if for the application,
     * 0 if it is a message of the protocol
     * -1 if an error occurs
     */
    @Override
    public int udpReceive(byte[] buffer) {
        receiveCalled = true;
        DatagramPacket p = new DatagramPacket(buffer, buffer.length);
        try {
            System.out.println("Waiting to receive messages...");
            udpSocket.receive(p);
        } catch (IOException e) {
            System.err.println("Error in receiving the message!");
            return -1;
        }

        String message = new String(buffer);
        message = message.substring(0, p.getLength());
        String sourcePacket = p.getAddress().toString();
        sourcePacket = sourcePacket.substring(1);

        if (sourcePacket.equals(myIp) || sourcePacket.equals("127.0.0.1")) {
            //Received a message from myself
            return 0;
        }

        System.out.println("TEST: received message: " + message);
        System.out.println("TEST: ip source: " + sourcePacket);

        JSONObject jsonObject;
        try {
            jsonObject = new JSONObject(message);
            System.out.println("TEST: Parsed json object is: " + jsonObject.toString());
        } catch (JSONException e) {
            System.err.println("Received a not valid json file!");
            return -1;
        }

        String handle = jsonObject.getString("handle");

        switch (handle) {
            case "forward_parent":
                System.out.println("TEST: received a message to be forwarded to the parent");

                sendDirect(p.getData(), parent);
                return 0;

            case "forward_children":
                //check if it's for me or for a child

                JSONArray arr = jsonObject.getJSONArray("path");
                if (arr.getString(0).equals(myIp) && arr.length() == 1) {
                    //I'm destination
                    System.out.println("TEST: received a message for me!");
                    JSONObject data = jsonObject.getJSONObject("data");

                    //writing the application message to the buffer
                    for(int i=0; i<data.toString().length(); i++) {
                        buffer[i] = data.toString().getBytes()[i];
                    }
                    for (int i=data.length(); i<buffer.length; ++i) {
                        buffer[i] = (byte) '\0';
                    }

                    sendAck(true, parent); //parent is the only possible source
                    return data.toString().length();
                } else {

                    if (arr.getString(0).equals("broadcast")) {
                        //for everyone
                        System.out.println("TEST: Received a broadcast message!");
                        JSONObject data = jsonObject.getJSONObject("data");

                        for(int i=0; i<data.toString().length(); i++) {
                            buffer[i] = data.toString().getBytes()[i];
                        }
                        for (int i=data.length(); i<buffer.length; ++i) {
                            buffer[i] = 0;
                        }

                        //forward to all children
                        for (NodeInfo ni : children) {
                            sendDirect(p.getData(), ni.getIp());
                        }
                        //no ack for broadcast messages
                        return data.toString().length();
                    }

                    //forward to the right children and change message header
                    if (arr.length() > 1) {
                        arr.remove(0);  //remove myself from the path
                        String nextHop = arr.getString(0);
                        String newMessage = jsonObject.toString();

                        System.out.println("TEST: Received a message to be forwarded to " + nextHop);
                        sendDirect(newMessage.getBytes(), nextHop);
                        return 0;
                    } else {
                        System.err.println("Error in receiving the message, path field is incorrect.");
                        return -1;
                    }
                }

            case "hello":
                //answer with hello_risp and update info
                System.out.println("TEST: received an hello message!");

                sendHello("hello_risp", jsonObject.getString("ip"));
                getHelloInfo(jsonObject.getString("ip"), jsonObject.getString("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "hello_risp":
                //simply update info
                System.out.println("TEST: received an hello_risp message!");

                getHelloInfo(jsonObject.getString("ip"), jsonObject.getString("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "ack":
                String dest;
                String from = jsonObject.getString("ip_source");
                try {
                    JSONArray path = jsonObject.getJSONArray("path");
                    //if there is a path, it's not empty
                    if (path.getString(0).equals(myIp) && path.length() == 1) {
                        //I'm destination
                        System.out.println("TEST: received an ack for me!");

                        if (waitForAck.get() && ackSource.equals(from)) {
                            //valid ack for me
                            System.out.println("TEST: the ack is valid!");

                            positiveAck.set(jsonObject.getBoolean("type"));
                            waitForAck.set(false);
                            receivedAns.set(true);
                        } else {
                            System.out.println("Received an ack that I did not expect.");
                            System.out.println("wait for ack: " + waitForAck);
                            System.out.println("ackSource: " + ackSource);
                            System.out.println("from: " + from);
                        }
                    } else {
                        //forward the ack to the correct child
                        System.out.println("TEST: received an ack from parent, I'm not the destination!");

                        dest = path.getString(0);
                        path.remove(0); //modify the header

                        String new_message = jsonObject.toString();
                        System.out.println("TEST: modified json to be forwarded: " + new_message);
                        sendDirect(new_message.getBytes(), dest);
                    }
                } catch (JSONException e) {
                    //there is no path, it's an ack for the parent
                    System.out.println("TEST: received an ack from children for the sink!");

                    sendDirect(p.getData(), parent);
                }

                return 0;

            case "change":
                System.out.println("TEST: Received a change message!");

                String child_change = jsonObject.getString("ip_source");
                if (numChildren < maxChildren) {
                    //I can add the new child
                    int new_child = getIndexNeighbour(child_change);
                    if (new_child >= 0) {
                        children.add(neighbours.get(new_child));
                        System.out.println("TEST: added a new child!");
                        //I don't need it in neighbours
                        neighbours.remove(new_child);
                    } else {
                        System.err.println("TEST: added the new child but with partial info.");
                        children.add(new NodeInfo(child_change, "", 255));
                    }

                    sendAck(true, child_change);
                    //inform sink
                    sendNetChange(true, child_change);
                    numChildren++;
                } else {
                    //too many children, send negative ack
                    System.out.println("Too many children for me!");
                    sendAck(false, child_change);
                }
                return 0;

            case "network_changed":
                System.out.println("TEST: Received a network_changed message, forwarding it to parent!");

                sendDirect(p.getData(), parent);
                return 0;

            default:
                System.err.println("Received an invalid message!");
                System.out.println("Received message: " + new String(p.getData()));
                return -1;
        }
    }

    /**
     * Retrieves the index of the specified child (if it exists)
     * @param childIP Child we are looking for
     * @return Position of the child if present, -1 otherwise
     */
    private int getIndexChild(String childIP) {
        boolean found = false;
        int index = 0;

        while (!found && index < numChildren) {
            if (children.get(index).getIp().equals(childIP)) {
                found = true;
            } else {
                index++;
            }
        }

        return (found ? index : -1);
    }

    /**
     * Retrieves the index of the specified neighbour (if it exists)
     * @param nodeIP Node we are looking for
     * @return Position of the neighbour if present, -1 otherwise
     */
    private int getIndexNeighbour(String nodeIP) {
        boolean found = false;
        int index = 0;

        while (!found && index < neighbours.size()) {
            if (neighbours.get(index).getIp().equals(nodeIP)) {
                found = true;
            } else {
                index++;
            }
        }

        return (found ? index : -1);
    }

    /**
     * Parses a received hello/hello_risp message, extracting all the
     * useful informations
     * @param ip Source IP of the sender of the message
     * @param id Unique ID of the sender of the message
     * @param path Path length specified in the message
     */
    private void getHelloInfo(String ip, String id, int path) {

        if (ip.equals(parent)) {
            this.pathLength = path + 1;
            parentAnswer.set(true);
            System.out.println("TEST: Read hello/hello_risp from parent");
            return;
        }

        int index = getIndexChild(ip);
        if (index != -1) {
            children.get(index).clearNotSeen();
            System.out.println("TEST: Read hello/hello_risp from known children");
        } else {
            index = getIndexNeighbour(ip);
            if (index != -1) {
                neighbours.get(index).clearNotSeen();
                neighbours.get(index).setPathLength(path);
                System.out.println("TEST: Read hello/hello_risp from known neighbour");
            } else {
                neighbours.add(new NodeInfo(ip, id, path));
                System.out.println("TEST: Read hello/hello_risp from new neighbour");
            }
        }
    }

    /**
     * Sorts neighbours, send a change parent to the best,
     * calls read to get the ack (if not already called)
     */
    private void changeParent() {
        positiveAck.set(false);
        String candidate = null;

        if (parent != null) {
            int index = getIndexNeighbour(parent);
            if (index >= 0) {
                neighbours.remove(index);
            }
        }

        if (neighbours.isEmpty()) {
            return;
        }

        neighbours.sort((n1, n2) -> {
            if (n1.getPathLength() < n2.getPathLength())
                return 1;
            else
                return 0;
        });

        System.out.println("TEST: Sorted neighbours:");
        for (NodeInfo n : neighbours) {
            System.out.println("Node ip: " + n.getIp() +
                    ", node id: " + n.getId() +
                    ", node pathLength: " + n.getPathLength());
        }

        int i = 0;
        int loops = 0;
        while (!positiveAck.get() && loops < 3) {
            if (i >= neighbours.size()) {
                i = 0;  //restart iteration
                loops++;
            }

            candidate = neighbours.get(i).getIp();
            System.out.println("TEST: Sending change parent to " + candidate);
            sendChange(candidate);
            i++;

        }
        if (candidate != null) {
            parent = candidate;
            System.out.println("My parent is " + parent);
        } else {
            System.err.println("Can't find a parent!");
        }

    }

    /**
     * Creates an ack of type "type" and sends it to
     * the specified destination (parent or a neighbour/child)
     * @param type positive or negative ack
     * @param dest IP address where to send the message
     */
    private void sendAck(boolean type, String dest) {
        JSONObject json = new JSONObject();
        json.put("handle", "ack");
        json.put("ip_source", myIp);
        json.put("type", type);
        if (!dest.equals(parent)) {
            //path is needed only if destination is different from parent
            JSONArray path = new JSONArray();
            path.put(dest);
            json.put("path", path);
        }
        System.out.println("TEST: sending ack...");
        sendDirect(json.toString().getBytes(), dest);
    }

    /**
     * Builds an hello or hello_risp message (depending on type)
     * and sends it to the specified destination (broadcast or neighbour/child/parent)
     * @param type hello or hello_risp
     * @param dest IP address where to send the message
     */
    private void sendHello(String type, String dest) {
        JSONObject message = new JSONObject();
        message.put("unique_id", myId);
        message.put("path", pathLength);
        message.put("ip", myIp);
        message.put("handle", type);

        String toSend = message.toString();
        System.out.println("TEST: sending hello...");
        byte[] mex = toSend.getBytes();

        sendDirect(mex, dest);
    }

    /**
     * Sends a changeParent message to the specified destination
     * @param dest IP address of the destination
     */
    private void sendChange(String dest) {
        JSONObject message = new JSONObject();
        message.put("ip_old_parent", (parent != null) ? parent : myIp);
        message.put("ip_source", myIp);
        message.put("handle", "change");

        String toSend = message.toString();
        System.out.println("TEST: sending change...");
        byte[] mex = toSend.getBytes();

        receivedAns.set(false);
        waitForAck.set(true);
        positiveAck.set(false);
        ackSource = dest;
        byte[] buffer = new byte[512];

        sendDirect(mex, dest);
        int attempt = 0;

        while (!receivedAns.get() && attempt < 5) {
            if (!receiveCalled) {
                //no one is reading
                udpReceive(buffer);
            } else {
                try {
                    Thread.sleep(2000); //another thread is blocked on the read
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            attempt++;
        }
    }

    /**
     * Sends a network_changed message to the sink of the net
     * @param add Specifies the kind of operation: new_child or removed_child
     * @param child Child that has been added/removed
     */
    private void sendNetChange(boolean add, String child) {
        JSONObject json = new JSONObject();
        json.put("handle", "network_changed");
        json.put("ip_child", child);
        json.put("ip_parent", myIp);
        if (add) {
            json.put("operation", "new_child");
        } else {
            json.put("operation", "removed_child");
        }

        receivedAns.set(false);
        waitForAck.set(true);
        positiveAck.set(false);
        ackSource = parent;
        int attempt = 0;

        while (!receivedAns.get() && attempt < 2) {
            System.out.println("TEST: sending network_changed... attempt = " + attempt);
            sendDirect(json.toString().getBytes(), parent);
            attempt++;

            try {
                Thread.sleep(3000);
                //receive called by another thread
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    /**
     * Implements the actual UDP send of a message to the specified destination
     * @param buffer message to be sent
     * @param dest IP address of the destination
     */
    private void sendDirect(byte[] buffer, String dest) {
        DatagramPacket p;
        InetAddress addr;

        String toSend = new String(buffer);
        toSend = toSend.substring(0, buffer.length);

        try {
            addr = InetAddress.getByName(dest);
        } catch (UnknownHostException e) {
            System.err.println("Error in creating packet, address unknown");
            return;
        }

        p = new DatagramPacket(buffer, buffer.length, addr, port);

        System.out.println("TEST: Sending message : " + toSend);
        System.out.println("to: " + dest);

        try {
            udpSocket.send(p);
        } catch (IOException e) {
            System.err.println("Error in sending the message!");
        }
    }
}
