/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   This version runs forever, forking off a separate 
   process for each connection
*/
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>   /* for the waitpid() system call */
#include <signal.h> /* signal name macros, and the kill() prototype */
#include <fcntl.h>
#include <time.h>
#include <sys/utsname.h>
#include <unistd.h>

#define BUFFSIZE 512

#define HTML 0
#define GIF 1
#define JPEG 2
#define INVALID 3




void errorMsg(char* code, char* http_protocol, char* msgToBrowser,
	 char* msgToTerminal, int sock);

void error(char *msg)
{
    perror(msg);
    exit(1);
}

int findContentType(char* name) {
	if (strstr(name, ".html") != NULL) return HTML;
	if (strstr(name, ".gif") != NULL) return GIF; 
	if (strstr(name, ".jpeg") != NULL ) return JPEG;
	if (strstr(name, ".jpg") != NULL ) return JPEG;
return INVALID;
}

int main(int argc, char *argv[])
{
     int sockfd, newsockfd, portno, pid;
     socklen_t clilen;
     struct sockaddr_in serv_addr, cli_addr;

     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");  //argument number is port
         exit(1);
     }
     sockfd = socket(AF_INET, SOCK_STREAM, 0);  //create socket
     if (sockfd < 0) 
        error("ERROR opening socket");
     memset((char *) &serv_addr, 0, sizeof(serv_addr)); //reset memory
     //fill in address info
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     listen(sockfd,5);  //5 simultaneous connection at most
     
     //accept connections
     newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
     if (newsockfd < 0) 
       error("ERROR on accept");
         
     int n;
     char buffer[2048];
     memset(buffer, 0, 2048);   //reset memory
      
    //read client's message
	while(read(newsockfd,buffer,1) != 0){

		 n = read(newsockfd,&buffer[1],2046);
		 if (n < 0) 
		    error("ERROR reading from socket");
		 printf("%s\n",buffer);  //print request

		 char command[BUFFSIZE];
		 char filename[BUFFSIZE];
		 char http_type[BUFFSIZE];
		 memset(command, 0, BUFFSIZE); //reset memory
		 memset(filename, 0, BUFFSIZE); //reset memory
		 memset(http_type, 0, BUFFSIZE); //reset memory

		 //tolkenize 1st line of request: will replace ' ', \r, \n with zerobyte
		 char* substr;
		 substr = strtok(buffer, " \r\n");
		 strcpy(command, substr);
		 substr = strtok(NULL, " \r\n");
		 strcpy(filename, substr);
		 substr = strtok(NULL, " \r\n");
		 strcpy(http_type, substr);

/*
		 printf("command = %s\n",command);
		 printf("filename = %s\n",filename);
		 printf("http_type = %s\n",http_type);
*/

		//check HTTP type
		if (strcmp(http_type, "HTTP/1.0")!=0 && strcmp(http_type, "HTTP/1.1")!=0) {
			errorMsg(" 505 HTTP Version Not Supported\r\n", http_type, "505 HTTP Version Not Suported", "Unsupported HTTP Type\n", newsockfd );
			continue;
		}

		//Process filename so that there is no leading slash
		char newFilename[BUFFSIZE];
		memset(newFilename, 0, BUFFSIZE);
		strncpy(newFilename, &filename[1], strlen(filename) - 1); 
		//printf("NEW filename: %s\n", newFilename);

		int type = findContentType(newFilename);
		//415: Unsupported Media Type
		if (type == INVALID) {
			errorMsg(" 415 Unsupported Media Type\r\n", http_type, "415 Unsupported Media Type", "Unsupported Media Type\n", newsockfd);
			continue;
		}


		 //Open file and find file size
		 int fd = open(newFilename, O_RDONLY);
		 //404: file not found 
		 if (fd < 0) {
			errorMsg(" 404 Not Found\r\n", http_type, "404 Not Found", 
				"Unable to open file\n", newsockfd);
			continue;
		 }

		//get file info: size and modified time
		 struct stat st;
		 fstat(fd,&st);

		 //Dynamically allocate buffer and read in file
		 char *file = (char *)malloc(st.st_size * sizeof(char) + 1);  
		 memset(file, 0, st.st_size + 1);
		 n = read(fd,file,st.st_size);
		 if (n < 0) error("ERROR reading file to buffer");


	/*  Vaild Response message contains: 
		HTTP_TYPE STATUS PHRASE
		Content-Length
		Date
		Server
		Last-Modified
		Content-Type
		Connection 
	*/
		//get current date and time and create string
		time_t t = time(NULL);
		struct tm *ltime = gmtime(&t);
		char date[BUFFSIZE];
		strftime(date, sizeof(date), "Date: %a, %d %b %Y %H:%M:%S GMT\r\n",ltime);

		//get server name using uname()
		struct utsname serv;
		uname(&serv);
		char serverMsg[BUFFSIZE];
		sprintf(serverMsg, "Server: %s/%s (%s)\r\n", serv.nodename, serv.release, serv.sysname); 

		//get last-modified time of file from st.st_mtime
		struct tm *modifiedTime = gmtime(&st.st_mtime);
		char lastModified[BUFFSIZE];
		strftime(lastModified, sizeof(lastModified), 
			"Last-Modified: %a, %d %b %Y %H:%M:%S GMT\r\n", modifiedTime);

		//formulate response msg
		char response[BUFFSIZE*2];
		memset(response, 0, BUFFSIZE*2);
		strcpy(response, http_type);
		strcat(response, " 200 OK\r\n");
		strcat(response, date);
		strcat(response, serverMsg);
		strcat(response, lastModified);
		char lengthMsg[BUFFSIZE];
		sprintf(lengthMsg, "Content-Length: %i\r\n", (int) st.st_size);
		strcat(response, lengthMsg);

	 
	if (type == HTML)
		strcat(response, "Content-Type: text/html\r\n");
	else if (type == GIF)
		strcat(response, "Content-Type: image/gif\r\n");
	else if (type == JPEG)
		strcat(response, "Content-Type: image/jpeg\r\n");
		strcat(response, "Connection: keep-alive\r\n\r\n");

		// Write response msg and file data to socket 
		printf("Response Msg: \n%s\n", response);  // to terminal
		n = write (newsockfd, response, strlen(response));
		n = write (newsockfd, file, st.st_size);

		free(file);


	}


     close(newsockfd);
     close(sockfd);
     return 0; 
}



/*  Error Response message contains: 
	HTTP_TYPE STATUS PHRASE
	Date
	Server		
	Content-Length
	Connection 
*/

void errorMsg(char* code, char* http_protocol, char* msgToBrowser,
	 char* msgToTerminal, int sock)
{
	char response[BUFFSIZE*2];
	strcpy(response, http_protocol);
	strcat(response, code);

	//get current date and time and create string
	time_t t = time(NULL);
	struct tm *ltime = gmtime(&t);
	char date[BUFFSIZE];
	strftime(date, sizeof(date), "Date: %a, %d %b %Y %H:%M:%S GMT\r\n",ltime);
	//get server name using uname()
	struct utsname serv;
	uname(&serv);
	char serverMsg[BUFFSIZE];
	sprintf(serverMsg, "Server: %s/%s (%s)\r\n", serv.nodename, serv.release, serv.sysname); 
	
	strcat(response, date);
	strcat(response, serverMsg);

	char lengthMsg[BUFFSIZE];
	sprintf(lengthMsg, "Content-Length: %i\r\n", strlen(msgToBrowser));
	strcat(response, lengthMsg);
	strcat(response, "Connection: keep-alive\r\n\r\n");

	printf("Error Response msg:\n%s\n", response);
	write (sock, response, strlen(response));
	write (sock, msgToBrowser, strlen(msgToBrowser) );

	fprintf(stderr,"%s\n", msgToTerminal); 


}








