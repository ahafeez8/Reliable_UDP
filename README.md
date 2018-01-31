# Reliable_UDP
UDP is a transport layer protocol which transmits messages over an unreliable channel to the receiver in the form of datagrams. It is used in applications that need rapid data transfer and no retransmissions as it cannot retrieve lost data packets or duplicate packets.
Transmission Control Protocol is another transport layer protocol which transfers data over reliable channel with retransmissions and timeouts. 
This project required us to implement UDP for transferring audio file over UDP using Linux Sockets but it should be working over a reliable channel.
1.1	Specifications
 Sender is required to open an audio file, read data chunks from file and write UDP segments, send these segments on UDP. Receiver must be able to receive, reorder and write data to a file at the receiving end.


We were  required to implement following to make UDP reliable: 


a) Sequence numbers 

b) Retransmission (selective repeat) 

c) Window size of 5-10 UDP segments 

d) Re ordering on receiver side

The design of our system involves the following key features:

•	Payload of 500 bytes
  
  o	16 bytes of header, 484 bytes of data for each segment

•A window size of 5 

•	Sequence number of each packet, calculated as:
 o	Seq = last_ACK_received + i + 1 
 o	In which, ‘i’ is the number of packet in the window and 
 o	1 is used since first packet seq was marked as ‘-1’

•	Use of Acknowledgement packets

•	Use of Selective Repeated Protocol

•	Reordering of packets

•	Catering to loss Packet or Loss ACK via retransmissions

•	A timer of 3 seconds
