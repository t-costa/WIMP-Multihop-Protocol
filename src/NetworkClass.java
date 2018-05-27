import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.net.*;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;


public class NetworkClass implements NetworkCommunication {

    private DatagramSocket udpSocket;
    private Socket tcpSocket;
    private int port;

    private ArrayList<NodeInfo> children;
    private ArrayList<NodeInfo> neighbours;
    private String parent;
    private String my_ip;
    private int num_children;
    private int max_children;

    private boolean received_ans;
    private boolean wait_for_ack;
    private boolean positive_ack;
    private boolean parent_answer;
    private String ack_source;

    private int path_length;

    NetworkClass(int max_children, String my_ip) {
        this.num_children = 0;
        this.max_children = max_children;
        this.wait_for_ack = false;
        this.positive_ack = false;
        this.parent_answer = false;
        this.path_length = 255;
        this.my_ip = my_ip;
        children = new ArrayList<>(this.max_children);
        neighbours = new ArrayList<>();
    }


    @Override
    public boolean tcpInitialize(int port, String dest) {
        this.port = port;

        try {
            if (dest.equals("localhost")) {
                tcpSocket = new Socket(dest, port);
            } else {
                tcpSocket = new Socket(InetAddress.getByName(dest), port);
            }
        } catch (UnknownHostException e) {
            System.err.println("Error in creating socket, destination address unknown");
            return false;
        } catch (IOException e) {
            System.err.println("Error in creating socket, I/O error");
            return false;
        }
        System.out.println("TEST: mio indirizzo: " + tcpSocket.getLocalAddress().toString());
        System.out.println("TEST: mia porta: " + tcpSocket.getLocalPort());

        return true;
    }
    @Override
    public int tcpReceive(String buffer) {

        BufferedReader bufferedReader;
        try {
            bufferedReader = new BufferedReader(new InputStreamReader(tcpSocket.getInputStream()));
        } catch (IOException e) {
            System.err.println("I/O exception in getting input stream from tcp socket");
            return -1;
        }

        try {
            buffer = bufferedReader.readLine();
        } catch (IOException e) {
            System.err.println("I/O exception in reading from tcp stream");
            return -1;
        }

        return buffer.length();
    }
    @Override
    public boolean tcpSend(String data) {

        DataOutputStream outputStream;
        try {
            outputStream = new DataOutputStream(tcpSocket.getOutputStream());
        } catch (IOException e) {
            System.err.println("I/O excpetion in getting output stream from tcp socket");
            return false;
        }

        try {
            outputStream.writeBytes(data);
        } catch (IOException e) {
            System.err.println("I/O exception in writing the message on the tcp socket");
            return false;
        }

        return true;
    }

    @Override
    public boolean udpInitialize(int port) {
        this.port = port;
        try {
            udpSocket = new DatagramSocket(port);
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
            Thread.sleep(1000);
            udpSocket.close();
            Thread.sleep(100);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        //initializer.interrupt();

        //così il thread in read dovrebbe morire
        try {
            udpSocket = new DatagramSocket(port);
        } catch (SocketException e) {
            System.err.println("Error in creating DatagramSocket");
            return false;
        }

        //dovrei cercare in neighbours un potenziale parent
        change_parent();

        Thread manager = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(30000);
                    parent_answer = false;
                    System.out.println("Sending hello in broadcast!");
                    send_hello("hello", "255.255.255.255");
                    Thread.sleep(5000);   //l'altro thread leggerà
                } catch (InterruptedException e) {
                    e.printStackTrace();
                    return;
                }

                if (!parent_answer) {
                    System.out.println("parent is dead, change it");
                    change_parent();
                }

                for (NodeInfo nc : children) {
                    nc.incrementNotSeen();
                }

                ArrayList<Integer> toBeRemoved = new ArrayList<Integer>();

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

    private void change_parent() {
        positive_ack = false;
        String candidate = null;

        neighbours.sort((n1, n2) -> {
            if (n1.getPathLength() > n2.getPathLength())
                return 1;
            else
                return 0;
        });

        //TODO: dovrei vedere se ho sortato bene
        int i = 0;

        while (!positive_ack) {
            candidate = neighbours.get(i).get_ip();
            System.out.println("Sending change parent to " + candidate);
            send_change(candidate);    //TODO: TEST

            received_ans = false;
            wait_for_ack = true;
            ack_source = candidate;
            byte[] buffer = new byte[512];

            while (!received_ans) {
                udpReceive(buffer);
            }
            System.out.println("Ho ricevuto una risposta che ha settato a true receivedme");
            i++;
        }
        parent = candidate;
        System.out.println("My parent is " + parent);
    }

    @Override
    public boolean udpSend(String data) {

        System.out.println("TEST: messaggio da inviare: " + data);
        System.out.println("TEST: indirizzo destinazione: " + parent);

        DatagramPacket p = null;
        InetAddress addr = null;

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


        positive_ack = false;
        wait_for_ack = true;
        ack_source = parent;
        int _try = 0;
        try {

            while (wait_for_ack && _try < 5) {
                udpSocket.send(p);
                System.out.println("Message sent, tent = " + _try);
                Thread.sleep(1000);
                _try++;
            }

        } catch (IOException e) {
            System.err.println("Error in sending the message!");
            return false;
        } catch (InterruptedException e) {
            System.err.println("Interrupted exception... non so che fa");
            return positive_ack;
        }

        return positive_ack;
    }

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

    private void getHelloInfo(String ip, int id, int path) {

        if (ip.equals(parent)) {
            this.path_length = path + 1;
            parent_answer = true;
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

    @Override
    public int udpReceive(byte[] buffer) {
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
                //todo: controlla che sia corretto
                send_direct(p.getData(), parent);
                return 0;

            case "forward_children":
                //controlla se è per te o se devi inoltrare e modificare il json

                JSONArray arr = jsonObject.getJSONArray("path");
                if (arr.getString(0).equals(my_ip)) {
                    //i'm destination
                    String data = jsonObject.getString("data");
                    buffer = data.getBytes();
                    for (int i=data.length(); i<buffer.length; ++i) {
                        buffer[i] = 0;
                    }
                    return data.length();
                } else {
                    //forward to children
                    String nextHop = arr.getString(0);
                    arr.remove(0);
                    String newMessage = jsonObject.toString();
                    send_direct(newMessage.getBytes(), nextHop);
                }

            case "hello":
                //rispondi con hello_risp and update info
                send_hello("hello_risp", jsonObject.getString("ip_source"));
                getHelloInfo(jsonObject.getString("ip"), jsonObject.getInt("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "hello_risp":
                getHelloInfo(jsonObject.getString("ip"), jsonObject.getInt("unique_id"), jsonObject.getInt("path"));
                return 0;

            case "ack":
                String dest = null;
                String from = jsonObject.getString("ip_source");
                try {
                    JSONArray path = jsonObject.getJSONArray("path");
                    //se c'è non è vuoto
                    if (path.getString(0).equals(my_ip)) {
                        //sono la destinazione
                        System.out.println("Ricevuto ack da parent, sono la destinazione");
                        received_ans = true;
                        if (wait_for_ack && ack_source.equals(from)) {
                            //valid ack for me
                            System.out.println("Ricevuto ack valido per me");
                            positive_ack = jsonObject.getBoolean("type");
                            wait_for_ack = false;
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
                    //TODO: mi sa che in change non metto unique_id, controlla!
                    NodeInfo n = new NodeInfo(child_change, jsonObject.getInt("unique_id"), this.path_length+1);
                    children.add(n);
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

    private void send_ack(boolean type, String dest) {
        //costruisce un ack positivo/negativo inviato al sink o un vicino
        JSONObject json = new JSONObject();
        json.put("handle", "ack");
        json.put("ip_source", my_ip);
        json.put("type", type);

        send_direct(json.toString().getBytes(), dest);
    }

    private void send_hello(String type, String dest) {
        JSONObject message = new JSONObject();
        message.put("unique_id", 7);
        message.put("ssid", "WIMP_SIM");
        message.put("path", path_length);
        message.put("ip", my_ip);
        message.put("handle", type);

        String toSend = message.toString();
        System.out.println("TEST: hello inviato: " + toSend);
        byte[] mex = toSend.getBytes();

        send_direct(mex, dest);

//        InetAddress addr = null;
//        try {
//            addr = InetAddress.getByName("255.255.255.255");
//        } catch (UnknownHostException e) {
//            System.err.println("messaggio in broadcast, non dovrei avere errori");
//        }
//        DatagramPacket p = new DatagramPacket(mex, mex.length, addr, this.port);
//        try {
//            udpSocket.send(p);
//        } catch (IOException e) {
//            System.err.println("error in sending hello");
//        }
    }

    private void send_change(String dest) {
        JSONObject message = new JSONObject();
        message.put("ip_old_parent", my_ip);
        message.put("ip_source", my_ip);
        message.put("handle", "change");

        String toSend = message.toString();
        System.out.println("TEST: change inviato: " + toSend);
        byte[] mex = toSend.getBytes();

        send_direct(mex, dest);
    }

    private boolean send_leave(String dest) {
        //todo: l'ack è sempre positivo ma se non lo ricevo devo rimandare!
        JSONObject json = new JSONObject();
        json.put("handle", "leave");
        json.put("ip_source", my_ip);

        send_direct(json.toString().getBytes(), parent);
        return false;
    }

    private void send_net_change(boolean add, String child) {
        JSONObject json = new JSONObject();
        json.put("handle", "network_changed");
        json.put("ip_child", child);
        json.put("ip_parent", my_ip);
        if (add) {
            json.put("operation", "new_child");
        } else {
            json.put("operation", "removed_child");
        }

        send_direct(json.toString().getBytes(), parent);
    }

    private boolean send_direct(byte[] buffer, String dest) {
        DatagramPacket p = null;
        InetAddress addr = null;


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
