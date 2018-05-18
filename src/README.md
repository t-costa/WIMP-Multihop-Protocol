# Doc and TODO

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

## TODO
Try sending messages of type forward and leave, just to check the data structures and that everything is ok,
then we need to add a third node, so that we can form a chain and try some real multihop (number of children is limited to one), if everything works, try to kill the middle node, and see if the network recovers itself.

Modify read, with a public (called only by the application) and a private (called internally by the public one and the management), so that the public one can push data received during the management process.

The random generated number is not random.

Serve testare la consistenza delle strutture dati interne.

Forse (probabilmente) serve aggiungere un id/ipsource all'ack.

Modifica per il network changed, sia in esp che in raspy (read_change, read_leave). -> ora i messaggi applicazione dovrebbero poter essere in qualsiasi formato!

Credo manchi qualcosa nella creazione di children, mi sa che qualche campo resta non settato.

## Type of messages
The following is a list of all the possible messages that can be exchanged by the ESP nodes and the sink, some of the messages might not be used in the final project (such as leave), due to lack of time.

### HELLO/HELLO_RISP
Sent every tot seconds in broadcast: let other nodes know me and my path length, and the answer let me know of other nodes. Used also to check if my parent (or children) is still alive, otherwise I have to change parent.

{
  "handle" : "hello/hello_risp",
  "ip" : "my ip",
  "path" : "length path of the source",
  "ssid" : "ssid of the AP"
}

### ACK
Used to notify the reception of a message/operation, and to confirm the outcome of the operation.

{
  "handle" : "ack",
  "ip" : "ip_source",
  "type" : "true/false"
}

### FORWARD_CHILDREN
Used to send a message from the sink to any other node in the network, specifying in the "path" field, the ip addresses of the nodes that have to forward the message. When a node receives this message, if it is not the final destination (path array empty), the node will remove the first ip address from "path", and will send the message to it (it will be one of its children).

{
  "handle" : "forward_children",
  "path" : ["ip1", "ip2", "ip3"],
  "data" : /* generic message */
}

### FORWARD_PARENT
Used to send data from a generic node to the sink; every intermediate node will forward the message to its parent.

{
  "handle" : "forward_parent",
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

### LEAVE_ME
Used by a node to notify its parent that he wish to change parent, so that the node can remove it from its children and notify the sink

{
  "handle" : "leave",
  "ip_source" : "my ip"
}
