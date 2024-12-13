 The server accepts TCP connection with its clients. These clients may subscribe
or unsubscribe to specific topics, and the server is responsible for sending
all the messages from a topic to all its subscribers. The server receives messages
via UDP datagrams.

 The messages between TCP clients and server are sent using the following application
layer protocol:
 - All messages contain only printable characters and '\0' and they finnish with ETX (0x3)
 - The first character of the message tell the message type:
    - '0' - authentication - client sends its ID to the server; the other part
    responds with '0OK' or '0NO' if it accepts (or rejects) the given ID
    - '1' - subscribe - client sends the topic that it wants to subscribe to
    (it may contain wildcards); server responds with '10' for success or '11' for
    failure
    - '2' - unsubscribe - client unsubscribes from a topic; server responds with
    '20' for success and '21' for failure
    - '3' - exit - one part announces that it finnishes the communication, without
    waiting for ackowledgement

 The topics have the form of a linux file path, that may contain some wildcards:
  - "*" - replaces any number of subdirectories in the path;
  - "+" - replaces exactly one subdirectory in the path.

 The program uses the following classes:
  - connection - manages a TCP connection (for both client and server); it reads
  from the socket until it would block. This class is responsible for the message
  parsing;
  - server - it implements the server that listens for both UDP and TCP messages,
  using epoll for multiplexing IO. It also creates the connections with clients,
  redirects the messages from UDP datagrams to the subscribed clients and sends
  exit messages to all connections when we input 'exit';
  - client - it represents the client, that opens a TCP connections, sends messages
  of type subscribe / unsubscribe / exit to the server based on its input and
  listens to the socket for any received message from the other part;
  - topics_tree - a database from server that stores the subscribed clients for
  each topic.
