# Documentation

## Warnings
The code in the ESP directory is not tested (and almost certainly not working) since we had an hardware problem with our ESP modules. The directory Emergency ESP contains a functioning java implementation of the protocol, with the difference that every node acts only as a station and not as station+AP.

In every single file of this repository there might be an infinite number of bugs (features) and errors, some comments might be in Italian or simply nonsense. The code has been written in a rush for a university exam, so it might be so awful to look at that your eyes could bleed. Read and use these files at your own risk. Enjoy!

## Protocol functioning (new node enters the network)
The ESP starts searching for WIMP nodes to connect to and for each found node, it connects to it and sends an hello to let the other node know its presence (and IP). Then it selects the "best" node (shortest path and stronger signal), reconnects to it and asks to become his child. If accepted, it enters in the normal functioning and the parent notifies the sink; otherwise, the node asks the second best node and so on.

### Sequence of messages:
new_ESP - - - - - - - - - - - - - - old_ESP/SINK

scan_network

begin connection

send hello - - - - - - - - - - - - > receives hello

received risp < - - - - - - - - - - sends hello_risp

//loop to find the best parent

send change_parent - - - - - - - - > receives change

received answer < - - - - - - - - - sends positive ack

//enters in management - - - - - - - notifies sink of the new node

//enters in loop of read and management

## Protocol functioning (periodic messages)
Every ESP node sends periodically an HELLO message and gathers the answers of the other nodes to update his knowledge of the network.

## Protocol functioning (node leaves/dies)
If a node becomes unreachable, sooner or later his children and parent will notice it, so the children will look for another parent (like if they were new nodes) and the old parent will notify the sink about the dead node.

## Type of messages
The following is a list of all the possible messages that can be exchanged by the ESP nodes and the sink.

### HELLO/HELLO_RISP
Sent every tot seconds in broadcast: let other nodes know me and my path length, and the answer let me know of other nodes. Used also to check if my parent (or children) is still alive, otherwise I have to change parent.

{
  "handle" : "hello/hello_risp",
  "ip" : "my ip",
  "path" : "length path to the source",
  "ssid" : "ssid of the AP",
  "unique_id" : "my id"
}

### ACK
Used to notify the reception of a message/operation, and to confirm the outcome of the operation.

{
  "handle" : "ack",
  "ip_source" : "ip_source",
  "type" : true/false,
  "path" : ["ip1", "ip2"]
}

### FORWARD_CHILDREN
Used to send a message from the sink to any other node in the network, specifying in the "path" field, the ip addresses of the nodes that have to forward the message. When a node receives this message, if it is not the final destination (last ip in path array), the node will remove the first ip address from "path", and will send the message to the next hop (it will be one of its children).

{
  "handle" : "forward_children",
  "path" : ["ip1", "ip2", "ip3"],
  "data" : /* generic message */
}

### FORWARD_PARENT
Used to send data from a generic node to the sink; every intermediate node will forward the message to its parent.

{
  "handle" : "forward_parent",
  "ip_source" : "ip",
  "data" : /* generic message */
}

### NETWORK_CHANGED
Used to inform the sink that a change has occured in the network (a new node joined the network or one is dead or changed parent).

{
  "handle" : "network_changed",
  "operation" : "new_child/removed_child",
  "ip_child" : "ip1",
  "ip_parent" : "ip2"
}

### CHANGE_PARENT
Used by a generic ESP node to change its parent from the old one to the one who receives this message. The old parent field is usefull for debugging but also to check if the probably dead parent is also my parent, in which case, the node cannot accept the request.

{
  "handle" : "change",
  "ip_source" : "my ip",
  "ip_old_parent" : "ip2"
}

## Limitations
Both in the java simulation and in the ESP code the case in which an orphan node sends a CHANGE_PARENT to one of its siblings it's not treated. This case could possibly create an avoidable longer path in the network.
