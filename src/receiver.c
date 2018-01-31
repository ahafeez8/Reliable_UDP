#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

#define MAX_PACKET_SIZE 500
#define MAXPACKETS 500000
#define DATA 0
#define ACK 1
#define FIN 2
#define FIN_ACK 3

int NFE = 0, LFA = -1;
uint8_t received[MAXPACKETS];

FILE * recv_file;

//packet header
typedef struct {   
	uint64_t sent_time; 
	uint16_t seq_no; 
	uint16_t content_type;   
}Header;

//Reliably data receive using UDP
int rdrUDP(char * udpPort, char* destinationFile) {
	int sockfd, rv, bytes_in_packet, i;
	struct addrinfo myinfo;
	struct addrinfo *srvr_info;
	struct addrinfo *selected_info;
	struct sockaddr sender_addr;
	char buffer[MAX_PACKET_SIZE];
	socklen_t their_addr_len = sizeof(sender_addr);

	memset(&myinfo, 0, sizeof myinfo);
	//set ai_family to AF_INET for IPv4 address
    myinfo.ai_family = AF_INET; 
    myinfo.ai_socktype = SOCK_DGRAM;
    //use my own IP address
    myinfo.ai_flags = AI_PASSIVE; 

    if ((rv = getaddrinfo(NULL, udpPort, &myinfo, &srvr_info)) != 0) {
    	//stderr = gai_strerror(rv);
    	printf("error: %s\n", gai_strerror(rv));
    	return 1;
    }

    for(selected_info = srvr_info; selected_info != NULL; selected_info = selected_info->ai_next) {
    	if ((sockfd = socket(selected_info->ai_family, selected_info->ai_socktype, selected_info->ai_protocol)) == -1) {
    		perror("receiver: socket creation failed");
    		continue;
    	}

    	//bind the socket
    	if (bind(sockfd, selected_info->ai_addr, selected_info->ai_addrlen) == -1) {
    		close(sockfd);
    		perror("receiver: binding failed");
    		continue;
    	}
    	break;
    }

    if (selected_info == NULL) {
    	fprintf(stderr, "receiver: failed to bind socket\n");
    	return 2;
    }

    freeaddrinfo(srvr_info);

    printf("Receiver: waiting to receive...\n");

    Header header;
    //store size of header in TCP_size variable
    int TCP_size = (int) sizeof(header);
 
	printf("Connection Established\n");
	printf("-----------------------\n");
	//calculate data length of packet  (MSS)
	int data_len = MAX_PACKET_SIZE - TCP_size;

	recv_file = fopen(destinationFile, "wb");

	printf("Recieving File...\n");
	printf("-----------------------\n");
	uint16_t content_type, seq, length;

	//loop for receiving data packets and sending Acks to them
	while(1){
		bytes_in_packet = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&sender_addr, &their_addr_len);
		//if received packet is invalid  
		if (bytes_in_packet < TCP_size)
			continue;
		//save the first 16 bytes (TCP Size) to header
		memcpy(&header, buffer, TCP_size);
		//read the content type of packet from its header
		content_type = header.content_type;
		
		//if it is FIN packet; break the loop
		if (content_type == FIN){
			printf("---------------------------\n");
			printf("[SUCCESS] File Received.\n");
			printf("---------------------------\n");
			printf("[RCV] FIN received. \n");
			break;		
		}

		//if it is neither FIN nor DATA, seems like corrupted packet 
		if (content_type != DATA){
			continue;
		}

		//read the sequence number from pakcet header
		seq = header.seq_no;
		//calculating lenth of packet
		length = bytes_in_packet - TCP_size;

		//if the packet is received for the first time
		if(received[seq] == 0){
			printf("[RCV] PKT-%d received\n", seq);
			received[seq] = 1;
			//if pointer is not at the correct writing location of file for which data is received
			if (SEEK_CUR != seq * data_len){
				//move the pointer to correct location => seq_no*data_length
				fseek(recv_file, seq * data_len, SEEK_SET);
			}
			//write the data into file
			fwrite(buffer + TCP_size, 1, length, recv_file);
			while(received[NFE]){
				NFE++;
			}
		//if packet is already received, don't write into file as we writing above.
		} else{
			printf("[RCV] Duplicate PKT-%d received\n", seq);
		}
		
		//set content_type in header to ACK
		header.content_type = ACK;
		//copy the header into buffer
		memcpy(buffer, &header, TCP_size);
		//send ACK packet
		if ((bytes_in_packet = sendto(sockfd, buffer, TCP_size, 0, (struct sockaddr *)&sender_addr, 				their_addr_len)) == -1){
			perror("receiver: error sending ACK packet");
			exit(2);
		}
		printf("[SND] Ack-%d sent\n", seq);
		//if sequence number is greater the LFA (last frame acknowledged), update the LFA
		if(seq > LFA){
			LFA = seq;	
		}
	}

	//set content_type in header to FIN_ACK
	header.content_type = FIN_ACK;
	//copy the header into buffer
	memcpy(buffer, &header, TCP_size);
	printf("Connection closed by sender\n");

	//Until ACK received from other party for FIN_ACK, keep sending FIN_ACK
	while(header.content_type != ACK){
		//send FIN_ACK packet
		if ((bytes_in_packet = sendto(sockfd, buffer, TCP_size, 0, (struct sockaddr *)&sender_addr, 
			their_addr_len)) == -1) 
		{
			perror("receiver: error sending FIN_ACK packet");
			exit(2);
		}
		printf("[SND] FIN_ACK Sent. \n");	
		//wait for ACK packet in respoonse of your FIN_ACK
		bytes_in_packet = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&sender_addr, &their_addr_len);
		if (bytes_in_packet == TCP_size){
			//copy the buffer into header
			memcpy(&header, buffer, TCP_size);
			//if ACK is received
			if (header.content_type == ACK){
				printf("[RCV] ACK received. \n");
			}
		}
	}
	printf("---------------------------\n");
	//close the file
	fclose(recv_file);
	//close the socket
	close(sockfd);
}

int main(int argc, char** argv){
	//command line argument should be 3
	if(argc != 3){
		fprintf(stderr, "usage: %s port filename_to_save\n\n", argv[0]);
		exit(1);
	}
	//reliably data receive
	rdrUDP(argv[1], argv[2]);
}