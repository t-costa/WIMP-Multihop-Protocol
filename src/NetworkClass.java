import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.net.*;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;


public class NetworkClass implements NetworkCommunication {

    private DatagramSocket udpSocket;
    private int port;

    private ArrayList<NodeInfo> children;
    private ArrayList<NodeInfo> neighbours;
    private String parent;
    private String my_ip;
    private int my_id;
    private int num_children;
    private int max_children;

    private AtomicBoolean received_ans;
    private AtomicBoolean wait_for_ack;
    private AtomicBoolean positive_ack;
    private AtomicBoolean parent_answer;
    private String ack_source;
    private boolean receiveCalled;

    private int path_length;

    /**
     * Creates the class and sets private variables
     * @param max_children maximum number of children that the node can have
     * @param my_ip IP address of the node
     */
    NetworkClass(int max_children, String my_ip, int my_id) {
        this.num_children = 0;
        this.max_children = max_children;

        this.wait_for_ack = new AtomicBoolean(false);
        this.positive_ack = new AtomicBoolean(false);
        this.parent_answer = new AtomicBoolean(false);
        this.received_ans = new AtomicBoolean(false);
        this.receiveCalled = false;

        this.path_length = 255;
        this.my_ip = my_ip;
        this.my_id = my_id;
        children = new ArrayList<>(this.max_children);
        neighbours = new ArrayList<>();
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

        System.out.println("TEST: mio indirizzo: " + udpSocket.getLocalAddress().toString());
        System.out.println("TEST: mia porta: " + udpSocket.getLocalPort());

        send_hello("hello", "255.255.255.255");

        Thread initializer = new Thread(() -> {
            byte[] data = new byte[512];
            try {
                while (true) {
                    System.out.println("Waiting messages,,,");
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
            Thread.sleep(4000);
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
        change_parent();

        if (!positive_ack.get()) {
            System.out.println("Errore in inizializzazione, non ho trovato un parent!");
            return false;
        }

        Thread manager = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(30000);
                    //parent_answer = false;
                    parent_answer.set(false);
                    System.out.println("Sending hello in broadcast!");
                    send_hello("hello", "255.255.255.255");
                    Thread.sleep(5000);   //l'altro thread leggerà
                } catch (InterruptedException e) {
                    e.printStackTrace();
                    return;
                }

                if (!parent_answer.get()) {
                    System.out.println("parent is dead, change it");
                    change_parent();
                }

                for (NodeInfo nc : children) {
                    nc.incrementNotSeen();
                }

                ArrayList<Integer> toBeRemoved = new ArrayList<>();

                for (int i=0; i<num_children; ++i) {
                    if (children.get(i).get_notSeen() == 2) {
                        System.out.println("Child " + children.get(i) + " is going to be removed");
                        toBeRemoved.add(i);
                    }
                }

                for (Integer i : toBeRemoved) {
                    children.remove((int) i);
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

        System.out.println("TEST: messaggio da inviare: " + data);
        System.out.println("TEST: indirizzo destinazione: " + parent);

        DatagramPacket p;
        InetAddress addr;

        JSONObject jsonObject = new JSONObject();
        JSONObject jsonMessage = new JSONObject(data);
        jsonObject.put("data", jsonMessage);
        jsonObject.put("ip_source", my_ip);
        jsonObject.put("handle", "forward_parent");

        String toSend = jsonObject.toString();
        byte[] mex = toSend.getBytes();
        System.out.println("TEST: wrapped messsage: " + toSend);

        try {
            addr = InetAddress.getByName(parent);
        } catch (UnknownHostException e) {
            System.err.println("Error in creating packet, address unknown");
            return false;
        }

        p = new DatagramPacket(mex, mex.length, addr, port);
        System.out.println("TEST: indirizzo destinazione da inetAddress: " + addr.toString());

        positive_ack.set(false);
        wait_for_ack.set(true);
        ack_source = parent;
        int _try = 0;
        try {

            while (wait_for_ack.get() && _try < 2) {
                udpSocket.send(p);
                System.out.println("Message sent, tent = " + _try);
                Thread.sleep(3000);
                _try++;
            }

        } catch (IOException e) {
            System.err.println("Error in sending the message! I/O exception.");
            return false;
        } catch (InterruptedException e) {
            System.err.println("Interrupted exception... non so che fa");
            return positive_ack.get();
        }

        return positive_ack.get();
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
            udpSocket.receive(p);
        } catch (IOException e) {
            System.err.println("Error in receiving the message!");
            return -1;
        }

        String message = new String(buffer);
        message = message.substring(0, p.getLength());
        String sourcePacket = p.getAddress().toString();
        sourcePacket = sourcePacket.substring(1);

        if (sourcePacket.equals(my_ip) || sourcePacket.equals("127.0.0.1")) {
            System.out.println("Ricevuto un messaggio da me stesso medesimo");
            System.out.println("Ricevuto: " + message);
            return 0;
        }

        System.out.println("TEST: messaggio ricevuto: " + message);
        System.out.println("TEST: indirizzo ip sorgente: " + p.getAddress().toString());

        JSONObject jsonObject;
        try {
            jsonObject = new JSONObject(new String(buffer));
        } catch (JSONException e) {
            System.err.println("Received a not valid json file!");
            System.out.println("Received message: " + new String(p.getData()));
            return -1;
        }

        String handle = jsonObject.getString("handle");

        switch (handle) {
            case "forward_parent":
                send_direct(p.getData(), parent);
                return 0;

            case "forward_children":
                //controlla se è per te o se devi inoltrare e modificare il json

                JSONArray arr = jsonObject.getJSONArray("path");
                if (arr.getString(0).equals(my_ip) && arr.length() == 1) {
                    //i'm destination
                    String data = jsonObject.getString("data");

                    System.arraycopy(data.getBytes(), 0, buffer, 0, data.length());

                    for (int i=data.length(); i<buffer.length; ++i) {
                        buffer[i] = 0;
                    }

                    send_ack(true, parent);
                    return data.length();
                } else {

                    if (arr.getString(0).equals("broadcast")) {
                        //for everyone
                        String data = jsonObject.getString("data");
                        System.arraycopy(data.getBytes(), 0, buffer, 0, data.length());

                        for (int i=data.length(); i<buffer.length; ++i) {
                            buffer[i] = 0;
                        }
                        //forward to all children
                        for (NodeInfo ni : children) {
                            send_direct(p.getData(), ni.get_ip());
                        }
                        return data.length();
                    }
                    //forward to the right children and change message
                    arr.remove(0);
                    if (arr.length() >= 1) {
                        String nextHop = arr.getString(0);
                        String newMessage = jsonObject.toString();
                        send_direct(newMessage.getBytes(), nextHop);
                        return 0;
                    } else {
                        System.err.println("Error in receiving ack, maybe someone is dead?");
                        return -1;
                    }

                }

            case "hello":
                //rispondi con hello_risp and update info
                send_hello("hello_risp", jsonObject.getString("ip"));
                getHelloInfo(jsonObject.getString("ip"), jsonObject.getString("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "hello_risp":
                getHelloInfo(jsonObject.getString("ip"), jsonObject.getString("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "ack":
                String dest;
                String from = jsonObject.getString("ip_source");
                try {
                    JSONArray path = jsonObject.getJSONArray("path");
                    //se c'è non è vuoto
                    if (path.getString(0).equals(my_ip)) {
                        //sono la destinazione
                        System.out.println("Ricevuto ack da parent, sono la destinazione");
                        //received_ans = true;
                        received_ans.set(true);
                        if (wait_for_ack.get() && ack_source.equals(from)) {
                            //valid ack for me
                            System.out.println("Ricevuto ack valido per me");
                            //positive_ack = jsonObject.getBoolean("type");
                            //wait_for_ack = false;
                            positive_ack.set(jsonObject.getBoolean("type"));
                            wait_for_ack.set(false);
                        } else {
                            System.out.println("Forse c'è qualcosa che non quadra con l'ack...");
                            System.out.println("wait for ack: " + wait_for_ack);
                            System.out.println("ack_source: " + ack_source);
                            System.out.println("from: " + from);
                        }
                    } else {
                        //devo forwardare al children corretto
                        System.out.println("Ricevuto ack da parent, non sono la destinzazione");
                        dest = path.getString(0);
                        path.remove(0); //TODO: MODIFICA IL JSON?
                        //dest = jsonObject.getString("path");
                        String new_message = jsonObject.toString();
                        System.out.println("TEST: json modificato da inoltrare: " + new_message);
                        send_direct(new_message.getBytes(), dest);
                    }
                } catch (JSONException e) {
                    //non c'è path, devo forwardare al parent
                    System.out.println("Ricevuto ack da un children, inoltro a parent");
                    send_direct(buffer, parent);    //TODO: CLEAR BUFFER OPPURE USA P.GETDATA
                    return 0;
                }
                return 0;

            case "change":
                String child_change = jsonObject.getString("ip_source");
                if (num_children < max_children) {
                    //aggiungi
                    int new_child = getIndexNeighbour(child_change);
                    if (new_child > 0) {
                        children.add(neighbours.get(new_child));
                    }
                    //NodeInfo n = new NodeInfo(child_change, jsonObject.getString("unique_id"), this.path_length+1);
                    //children.add(n);
                    send_ack(true, child_change);
                    //informa sink
                    send_net_change(true, child_change);
                    num_children++;
                } else {
                    //troppi figli, mando ack negativo
                    send_ack(false, child_change);
                }
                return 0;

            case "leave":
                String child_leave = jsonObject.getString("ip_source");
                int index = getIndexChild(child_leave);
                if (index != -1) {
                    send_net_change(false, child_leave);
                    children.remove(index);
                    num_children--;
                }
                send_ack(true, child_leave);
                return 0;

            case "network_changed":
                //todo: controlla che sia corretto
                send_direct(p.getData(), parent);
                return 0;

            default:
                System.err.println("Received invalid message...");
                System.out.println("Received message: " + new String(p.getData()));
                return -1;
        }
    }

    /**
     * Retrieves the index of the specified child (if it exists)
     * @param child Child we are looking for
     * @return Position of the child if present, -1 otherwise
     */
    private int getIndexChild(String child) {
        boolean found = false;
        int index = 0;

        while (!found && index < num_children) {
            if (children.get(index).get_ip().equals(child)) {
                found = true;
            } else {
                index++;
            }
        }

        return (found ? index : -1);
    }

    /**
     * Retrieves the index of the specified neighbour (if it exists)
     * @param node Node we are looking for
     * @return Position of the neighbour if present, -1 otherwise
     */
    private int getIndexNeighbour(String node) {
        boolean found = false;
        int index = 0;

        while (!found && index < neighbours.size()) {
            if (neighbours.get(index).get_ip().equals(node)) {
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
            this.path_length = path + 1;
            parent_answer.set(true);
            System.out.println("Read hello/hello_risp from parent");
            return;
        }

        int index = getIndexChild(ip);
        if (index != -1) {
            children.get(index).clearNotSeen();
            System.out.println("Read hello/hello_risp from known children");
        } else {
            index = getIndexNeighbour(ip);
            if (index != -1) {
                neighbours.get(index).clearNotSeen();
                neighbours.get(index).setPathLength(path);
                System.out.println("Read hello/hello_risp from known neighbour");
            } else {
                neighbours.add(new NodeInfo(ip, id, path));
                System.out.println("Read hello/hello_risp from new neighbour");
            }
        }
    }

    /**
     * Sorts neighbours, send a change parent to the best,
     * calls read to get the ack (if not already called)
     */
    //TODO: se non trova nessuno e ho guardato tutti i vicini
    //TODO: dovrei fallire e ricominciare dal send hello!
    private void change_parent() {
        positive_ack.set(false);
        String candidate = null;

        neighbours.sort((n1, n2) -> {
            if (n1.getPathLength() > n2.getPathLength())
                return 1;
            else
                return 0;
        });

        System.out.println("TEST: Sorted neighbours:");
        for (NodeInfo n : neighbours) {
            System.out.println("Node ip: " + n.get_ip() +
                    ", node id: " + n.get_id() +
                    ", node pathLength: " + n.getPathLength());
        }

        int i = 0;

        while (!positive_ack.get()) {
            if (i >= neighbours.size()) {
                return;
            }

            candidate = neighbours.get(i).get_ip();
            System.out.println("Sending change parent to " + candidate);
            send_change(candidate);
            i++;

        }
        parent = candidate;
        System.out.println("My parent is " + parent);
    }

    /**
     * Creates an ack of type "type" and sends it to
     * the specified destination (parent or a neighbour/child)
     * @param type positive or negative ack
     * @param dest IP address where to send the message
     */
    private void send_ack(boolean type, String dest) {
        JSONObject json = new JSONObject();
        json.put("handle", "ack");
        json.put("ip_source", my_ip);
        json.put("type", type);
        if (!dest.equals(parent)) {
            JSONArray path = new JSONArray();
            path.put(dest);
            json.put("path", path);
        }

        send_direct(json.toString().getBytes(), dest);
    }

    /**
     * Builds an hello or hello_risp message (depending on type)
     * and sends it to the specified destination (broadcast or neighbour/child/parent)
     * @param type hello or hello_risp
     * @param dest IP address where to send the message
     */
    private void send_hello(String type, String dest) {
        JSONObject message = new JSONObject();
        message.put("unique_id", "" + my_id);
        message.put("ssid", "WIMP_SIM");
        message.put("path", path_length);
        message.put("ip", my_ip);
        message.put("handle", type);

        String toSend = message.toString();
        System.out.println("TEST: hello inviato: " + toSend);
        byte[] mex = toSend.getBytes();

        send_direct(mex, dest);
    }

    /**
     * Sends a change_parent message to the specified destination
     * @param dest IP address of the destination
     * @return true iff the message has been sent and acked
     */
    private boolean send_change(String dest) {
        JSONObject message = new JSONObject();
        message.put("ip_old_parent", (parent != null) ? parent : my_ip);
        message.put("ip_source", my_ip);
        message.put("handle", "change");

        String toSend = message.toString();
        System.out.println("TEST: change inviato: " + toSend);
        byte[] mex = toSend.getBytes();

        received_ans.set(false);
        wait_for_ack.set(true);
        positive_ack.set(false);
        ack_source = dest;
        byte[] buffer = new byte[512];

        send_direct(mex, dest);

        while (!received_ans.get()) {   //non sono sicuro serva
            if (!receiveCalled) {
                //prima volta in cui viene chiamata
                udpReceive(buffer);
            } else {
                try {
                    Thread.sleep(1000);
                    //aspetta semplicemente, la read è stata chiamata
                    //da un altro, non posso avere due read contemporaneamente
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
        return positive_ack.get();
    }

    /**
     * Creates and sends a leave message to the parent
     * @return true iff we received an ack
     */
    private boolean send_leave() {
        JSONObject json = new JSONObject();
        json.put("handle", "leave");
        json.put("ip_source", my_ip);

        received_ans.set(false);
        wait_for_ack.set(true);
        positive_ack.set(false);
        ack_source = parent;
        int tent = 0;

        while (!received_ans.get() && tent < 2) {   //non sono sicuro serva

            send_direct(json.toString().getBytes(), parent);
            tent++;
            try {
                Thread.sleep(1000);
                //aspetta semplicemente, la read è stata chiamata
                //da un altro, non posso avere due read contemporaneamente
            } catch (InterruptedException e) {
                e.printStackTrace();
            }

        }

        return received_ans.get();
    }

    /**
     * Sends a network_changed message to the sink of the net
     * @param add Specifies the kind of operation: new_child or removed_child
     * @param child Child that has been added/removed
     * @return true iff I received a positive ack
     */
    private boolean send_net_change(boolean add, String child) {
        JSONObject json = new JSONObject();
        json.put("handle", "network_changed");
        json.put("ip_child", child);
        json.put("ip_parent", my_ip);
        if (add) {
            json.put("operation", "new_child");
        } else {
            json.put("operation", "removed_child");
        }

        received_ans.set(false);
        wait_for_ack.set(true);
        positive_ack.set(false);
        ack_source = parent;
        int tent = 0;

        while (!received_ans.get() && tent < 2) {   //non sono sicuro serva

            send_direct(json.toString().getBytes(), parent);
            tent++;

            try {
                Thread.sleep(1000);
                //aspetta semplicemente, la read è stata chiamata
                //da un altro, non posso avere due read contemporaneamente
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        return positive_ack.get();
    }

    /**
     * Implements the actual UDP send of a message to the specified destination
     * @param buffer message to be sent
     * @param dest IP addres of the destination
     * @return true iff the send is computed without exceptions
     */
    private boolean send_direct(byte[] buffer, String dest) {
        DatagramPacket p;
        InetAddress addr;

        String toSend = new String(buffer);
        toSend = toSend.substring(0, buffer.length);

        try {
            addr = InetAddress.getByName(dest);
        } catch (UnknownHostException e) {
            System.err.println("Error in creating packet, address unknown");
            return false;
        }

        p = new DatagramPacket(buffer, buffer.length, addr, port);

        System.out.println("TEST: invio messaggio: " + toSend);
        System.out.println("TEST: destinazione: " + dest);

        try {
            udpSocket.send(p);
        } catch (IOException e) {
            System.err.println("Error in sending the message!");
            return false;
        }
        return true;
    }
}
