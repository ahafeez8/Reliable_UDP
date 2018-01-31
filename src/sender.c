#include <signal.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_PACKET_SIZE 500
#define DATA 0
#define ACK 1
#define FIN 2
#define FIN_ACK 3

int64_t timeOut = 1;
int last_ack_rcvd = -1; 
const int swnd = 5;  

//packet header
typedef struct {   
	uint64_t sent_time;    
	uint16_t seq_no; 
	uint16_t content_type;   
}Header;

//handler for timeout
void timer_handler(int signum){
	//printf("[TMR] timeout occured.\n");
}

//setting timeout timer
void start_timer(){
	//printf("[TMR] timer started\n");
	struct itimerval timeout_timer;
	
 	//expire timer after 3 seconds
 	timeout_timer.it_value.tv_sec = 1;
 	timeout_timer.it_value.tv_usec = 0;
 	
 	timeout_timer.it_interval.tv_sec = 0;
 	timeout_timer.it_interval.tv_usec = 0;
 	
 	//start timer
 	setitimer (ITIMER_REAL, &timeout_timer, NULL);
}

//counting total acknowledgement received 
int count_total_acks(int acked[], int size){
	int acks_count = 0;
	for (int a = 0; a < size; a++){
		if(acked[a]==1){
			acks_count++;
		}
	}
	return acks_count;
}

//reliably data transfer using UDP
int rdtUDP(char* hostname, char * udpPort, char* filename){
	//signal action for timeouts
	struct sigaction signal_action;
 	memset (&signal_action, 0, sizeof (signal_action));
 	//set the timer_handler function as sigaction handler
 	signal_action.sa_handler = &timer_handler;
 	sigaction (SIGALRM, &signal_action, NULL);

	int sockfd, rv, bytes_in_packet, i;
	
	struct addrinfo myinfo;
	struct addrinfo *server_info;
	struct addrinfo *selected_info;

	struct sockaddr_in bind_addr, sendto_addr;
	struct sockaddr receiver_addr;
	socklen_t their_addr_len = sizeof(receiver_addr);
	char buffer[MAX_PACKET_SIZE];

	//setting up sendto address
	uint16_t sendto_port = (uint16_t)atoi(udpPort);
	memset(&sendto_addr, 0, sizeof(sendto_addr));

	sendto_addr.sin_family = AF_INET;
	sendto_addr.sin_port = htons(sendto_port);
	inet_pton(AF_INET, hostname, &sendto_addr.sin_addr);

	memset(&myinfo, 0, sizeof myinfo);
	myinfo.ai_family = AF_INET;
	myinfo.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(hostname, udpPort, &myinfo, &server_info)) != 0) {
		printf("error: %s\n", gai_strerror(rv));
		return 1;
	}

	for(selected_info = server_info; selected_info != NULL; selected_info = selected_info->ai_next) {
		if ((sockfd = socket(selected_info->ai_family, selected_info->ai_socktype, selected_info->ai_protocol)) == -1) {
			perror("sender: socket error");
			continue;
		}
		break;
	}

	if (selected_info == NULL) {
		fprintf(stderr, "sender: socket creation failed\n");
		return 2;
	}

	//extracting port number
	uint16_t my_port = ((struct sockaddr_in *)selected_info->ai_addr )->sin_port; 
	
	freeaddrinfo(server_info);  //don't need it anymore
	
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	//set the port number
	bind_addr.sin_port = htons(my_port);
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	//bind the socket
	if (bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(struct sockaddr_in)) == -1) 
	{
		close(sockfd);
		perror("sender: binding failed");
		exit(1);
	}

	//initialize headers for sender and receiver
	Header sender_header, receiver_header;
	//store the size of sender_header to TCP_size
	int TCP_size = (int) sizeof(sender_header);
	
	FILE* out_file = fopen(filename, "rb");
	
	fseek(out_file, 0, SEEK_END);
	//store the size of file into file_size variable
    int file_size = ftell(out_file);
    fseek(out_file, 0, SEEK_SET);

    //calculate data length of packet  (MSS)
	int data_len = MAX_PACKET_SIZE - TCP_size;
	//calculate the number of segments in which complete file can be transferred
	int segments = file_size/data_len;
	//if file_size is not evenly divisble by data length then some bytes are remained.
	//save those number of bytes 
	int final_seg_size = file_size % data_len;

	//if there are remaining bytes which are not complete segment, make one more segment for them.
	if (final_seg_size > 0)
		segments++;

	printf("---------------------------\n");
	printf("File Size: %d bytes\n", file_size);
	printf("Header Size: %d bytes\n", TCP_size);
	printf("Data Length: %d bytes\n", data_len);
	printf("Segments: %d\n", segments);
	printf("Sender Window Size: %d\n", swnd);
	printf("---------------------------\n");

	//set the sequence number of first packet to 0
	sender_header.seq_no = 0;
	//set the content_type in header to DATA
	sender_header.content_type = DATA;
	//double_sent counts the numbers of retransmitted packets
	int double_sent = 0;

	printf("Sending File...\n");
	printf("-----------------\n");

	uint16_t seq, length;
	
	//initialize 2 arrays ACKed and sent to save the state of each segment
	int *ACKed = (int *)malloc(sizeof(int)*segments);
	int *sent = (int *)malloc(sizeof(int)*segments);
	
	//set all values of arrays to 0
	for (int i = 0; i < sizeof(ACKed); i++){
		ACKed[i] = 0;
		sent[i] = 0;		
	}

	//ack counter
	int ack_count;

	//loop until all acks are received
	while (ack_count != segments){
		ack_count = 0;

		//loop throught the window size
		for (int i = 0; i < swnd; i++){
			//find the sequence no of packet which is to be sent next
			seq = last_ack_rcvd+i+1;
			//if that ACK of sending packet is not received & 
			//sequence no is not exceeding the total no. of segments 
			if (ACKed[seq] == 0 && seq < segments){
				//if the packet is already sent, increment the doube_sent counter
				if (sent[seq] == 1){
					double_sent++;		//counter for retransmitted packets.
				//if the packet is being sent for the first time
				} else{
					//mark the packet as sent.
					sent[seq] = 1;
					//set the sequence no in the header
					sender_header.seq_no = seq;
					//copy the header into buffer
					memcpy(buffer, &sender_header, TCP_size);
					//if the pointer is not at correct location of reading data
					if (SEEK_CUR != seq * data_len)
						//move the pointer to correct location
						fseek(out_file, data_len * seq, SEEK_SET);
					//if it is last segment and it is not complete but just some remained bytes
					if (seq == segments-1 && final_seg_size > 0){
						//set lenth of packet to that number of remaining bytes
						length = final_seg_size;	
					}
					//set the length of packet to complete segment data length.
					else{
						length = data_len;
					}
					//read the data from file and put it into buffer
					fread(buffer + TCP_size, 1, length, out_file);
					//send the packet
					if (bytes_in_packet = sendto(sockfd, buffer, length + TCP_size, 0,
				 		(struct sockaddr*)&sendto_addr, sizeof(sendto_addr)) == -1) 
					{	
						perror("sender: error sending data packet");
						exit(1);
					}

					printf("[SND] PKT-%d sent\n", seq);	
				}
			}
		} 
		//start the timeout timer
		start_timer();

		//loop for receving acknowledgements
		for (int i = 0; i < swnd; i++){
			//skipping if all total acks are equal to number of segments
			//if not so, wait for acks
			if(count_total_acks(ACKed,segments) != segments){
				bytes_in_packet = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, &receiver_addr, &their_addr_len);
				if(bytes_in_packet == TCP_size){
					memcpy(&receiver_header, buffer, TCP_size);
					//if the ACK is received.
					if (receiver_header.content_type == ACK){
						printf("[RCV] Ack-%d received\n", receiver_header.seq_no);
						ACKed[receiver_header.seq_no] = 1;
					}
				}
				else {
					break;	
				}
			}
		}

		//array for saving missed acks
		int missed_acks[swnd];
		//fill the array with all 0's
		memset(&missed_acks, 0, sizeof(missed_acks));
		//counter for counting total missed acks
		int count = 0;
		for (int i=last_ack_rcvd+1; i < last_ack_rcvd+swnd; i++){
			//if all segments are transferred 
			if(i >= segments){
				break;
			} else{
				//if Ack of current segment is not received
				if (!ACKed[i]){
					//store sequence number of missed ack
					missed_acks[count] = i;			
					count++;
					printf("[OPS] PKT-%d is lost\n", i); 
					
					//set the sequence number in header
					sender_header.seq_no = i;
					//copy the header into buffer
					memcpy(buffer, &sender_header, TCP_size);
					//if the pointer is not at correct location of reading data
					if (SEEK_CUR != i * data_len)
						//move the pointer to correct location
						fseek(out_file, data_len * i, SEEK_SET);
					//if it is last segment and it is not complete but just some remained bytes
					if (i == segments-1 && final_seg_size > 0){
						//set lenth of packet to that number of remaining bytes
						length = final_seg_size;	
					}
					//set the length of packet to complete segment data length.
					else{
						length = data_len;
					}
					//read the data from file and put into the buffer
					fread(buffer + TCP_size, 1, length, out_file);
					//send the packet
					if (bytes_in_packet = sendto(sockfd, buffer, length + TCP_size, 0,
				 		(struct sockaddr*)&sendto_addr, sizeof(sendto_addr)) == -1) 
					{	
						perror("sender: sendto");
						exit(1);
					}
					//mark the packet as sent.
					sent[i] = 1;
					printf("[RET] PKT-%d retransmitted\n", i);	
				}
			}
		}

		//if there is some missed ack
		if (count>0) {
			//start the timer again
			start_timer();
		}
		
		//loop through all missed acks
		for (int i=0; missed_acks[i]!=0; i++){
			//wait for acks	
			bytes_in_packet = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, &receiver_addr, &their_addr_len);
			if(bytes_in_packet == TCP_size){
				//copy the buffer into receiver_header
				memcpy(&receiver_header, buffer, TCP_size);
				//if ACK is received
				if (receiver_header.content_type == ACK){
					printf("[RCV] Ack-%d received\n", receiver_header.seq_no);
					ACKed[receiver_header.seq_no] = 1;
				}
			}
			//if ACK is not received
			else if (missed_acks[i] != 0){
				//set the seq_no of missed ack
				sender_header.seq_no = missed_acks[i];
				//copy the sender header into buffer
				memcpy(buffer, &sender_header, TCP_size);
				//if the pointer is not at correct location of reading data
				if (SEEK_CUR != missed_acks[i] * data_len)
					//move the pointer to correct location
					fseek(out_file, data_len * missed_acks[i], SEEK_SET);
				//if it is last segment and it is not complete but just some remained bytes
				if (missed_acks[i] == segments-1 && final_seg_size > 0){
					//set lenth of packet to that number of remaining bytes
					length = final_seg_size;	
				}
				//set the length of packet to complete segment data length.
				else{
					length = data_len;
				}
				//read the data from the file and put into the buffer.
				fread(buffer + TCP_size, 1, length, out_file);
				//send the packet
				if (bytes_in_packet = sendto(sockfd, buffer, length + TCP_size, 0,
			 		(struct sockaddr*)&sendto_addr, sizeof(sendto_addr)) == -1) 
				{	
					perror("sender: error sending the packet");
					exit(1);
				}
				printf("[RET] PKT-%d retransmitted\n", missed_acks[i]);	
				//decrease the counter as we have to wait for ACK for this packet again
				i--;
				//reset the timer
				start_timer();
				//continue the loop
				continue;	
			} else{
				break;
			}
		
		}

		//check if all acked received.
		ack_count = count_total_acks(ACKed, segments);
		last_ack_rcvd = ack_count-1;
	}

	//Starting Teardown of connection
	printf("----------------------\n");
	printf("[SUCCESS] File sent.\n");
	printf("----------------------\n");
	printf("Closing connection...  \n");
	printf("----------------------\n");
	//loop until FIN_ACK packet is received
	while(receiver_header.content_type != FIN_ACK){
		//set the content_type of header to FIN
		sender_header.content_type = FIN;
		//copy the sender_header to buffer
		memcpy(buffer, &sender_header, TCP_size);
		//send the FIN packet
		if (bytes_in_packet = sendto(sockfd, buffer, TCP_size, 0,
			 (struct sockaddr*)&sendto_addr, sizeof(sendto_addr)) == -1) 
		{	
			perror("sender: error sending FIN packet");
			exit(1);
		}
		printf("[SND] FIN Sent. \n");
		//wait the FIN_ACK pakcet
		bytes_in_packet = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, &receiver_addr, &their_addr_len);
		if(bytes_in_packet == TCP_size){
			memcpy(&receiver_header, buffer, TCP_size);
			//if FIN_ACK packet is received
			if (receiver_header.content_type == FIN_ACK){
				printf("[RCV] FIN_ACK Received. \n");
			}
		}
	}

	//set the content type of header to ACK
	sender_header.content_type = ACK;
	//copy the sender_header to buffer
	memcpy(buffer, &sender_header, TCP_size);
	//send the last ACK packet and Pray :P
	if (bytes_in_packet = sendto(sockfd, buffer, TCP_size, 0,
		 (struct sockaddr*)&sendto_addr, sizeof(sendto_addr)) == -1) 
	{	
		perror("sender: error sending last ACK packet");
		exit(1);
	}

	printf("[SND] ACK Sent. \n");
	printf("----------------------\n");
	printf("Connection closed \n");
	printf("----------------------\n");
	//close the file
	fclose(out_file);
	//close the socket
	close(sockfd);
}

int main(int argc, char** argv){
	//4 command line arguments are required to run this program
	if(argc != 4){
		fprintf(stderr, "usage: %s hostname port filename_to_transfer\n\n", argv[0]);
		exit(1);
	}
	//reliably data transfer using UDP
	rdtUDP(argv[1], argv[2], argv[3]);
} 
