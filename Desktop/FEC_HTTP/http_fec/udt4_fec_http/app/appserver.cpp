#ifndef WIN32
   #include <unistd.h>
   #include <cstdlib>
   #include <cstring>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <wspiapi.h>
#endif
#include <sys/time.h>
#include <iostream>
#include <udt.h>
#include "cc.h"
#include "test_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#define SA struct sockaddr
#define SRV_SOCK_PATH   "/tmp/ux_socket"

using namespace std;

#ifndef WIN32
void* recvdata(void*, char *, int, int, int);
#else
DWORD WINAPI recvdata(LPVOID);
#endif

int connet_to_application()
{
    int sockfd, connfd;
    socklen_t len;
    struct sockaddr_un server;
    struct sockaddr cli;

    memset(&server, 0, sizeof(struct sockaddr_un));
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, SRV_SOCK_PATH, sizeof(server.sun_path) - 1);
    if (access(server.sun_path, F_OK) == 0)
        unlink (server.sun_path);
    /* socket create and verification */
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    //bzero(&servaddr, sizeof(servaddr));

    /* assign IP, PORT */
    /*servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(10000);

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        printf("setsockopt(SO_REUSEADDR) failed \n");*/

    /* Binding newly created socket to given IP and verification */
    if ((bind(sockfd, (SA*)&server, sizeof(server))) != 0) {
        printf("socket bind failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully binded..\n");

    /* Now server is ready to listen and verification */
    if ((listen(sockfd, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    }
    else
        printf("Server listening..\n");
    len = (socklen_t)sizeof(cli);

    /* Accept the data packet from client and verification */
    connfd = accept(sockfd, &cli, &len);
    if (connfd < 0) {
        printf("server accept failed...\n");
        exit(0);
    }
    else
        printf("server accept the client...\n");

    return connfd;
}


int main(int argc, char* argv[])
{

   int app_sock;// = connet_to_application();

   UDTUpDown _udt_;

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   //hints.ai_socktype = SOCK_STREAM;
   hints.ai_socktype = SOCK_DGRAM;

   string service("9000");
   if (2 == argc)
      service = argv[1];

   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return 0;
   }

   UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

   // UDT Options
   UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   UDT::setsockopt(serv, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(serv, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(serv, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(serv, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));

   if (argc == 5) {
      UDT::setsockopt(serv, 0, UDT_FECROWRATE, new int(atoi(argv[3])), sizeof(int));
      UDT::setsockopt(serv, 0, UDT_FECCOLRATE, new int(atoi(argv[4])), sizeof(int));
   }

   CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(serv, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(1000);

   if (UDT::ERROR == UDT::bind(serv, res->ai_addr, res->ai_addrlen))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   freeaddrinfo(res);

   cout << "server is ready at port: " << service << endl;

   if (UDT::ERROR == UDT::listen(serv, 10))
   {
      cout << "listen: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);

   UDTSOCKET recver;

   while (true)
   {
      if (UDT::INVALID_SOCK == (recver = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen)))
      {
         cout << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
         return 0;
      }

      char clienthost[NI_MAXHOST];
      char clientservice[NI_MAXSERV];
      getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
      cout << "new connection: " << clienthost << ":" << clientservice << endl;

      #ifndef WIN32
         //pthread_t rcvthread;
         //pthread_create(&rcvthread, NULL, recvdata, new UDTSOCKET(recver));
         //pthread_detach(rcvthread);
         if (argc == 5) {
             recvdata(new UDTSOCKET(recver), argv[2], atoi(argv[3]), atoi(argv[4]), app_sock);
             break;
         }
         else {
             recvdata(new UDTSOCKET(recver), argv[2], 0, 0, app_sock);
             break;
         }
      #else
         CreateThread(NULL, 0, recvdata, new UDTSOCKET(recver), 0, NULL);
      #endif
   }

   UDT::close(serv);

   return 0;
}

#ifndef WIN32
void* recvdata(void* usocket, char *filename1, int row_rate, int col_rate, int app_sock)
#else
DWORD WINAPI recvdata(LPVOID usocket)
#endif
{
   UDTSOCKET recver = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   UDT::setsockopt(recver, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(recver, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   UDT::setsockopt(recver, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(recver, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(recver, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(recver, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));
   bool block = false;
   UDT::setsockopt(recver, 0, UDT_RCVSYN, &block, sizeof(bool)); 
   if (row_rate > 0 ) {
       UDT::setsockopt(recver, 0, UDT_FECROWRATE, new int(row_rate), sizeof(int));
       UDT::setsockopt(recver, 0, UDT_FECCOLRATE, new int(col_rate), sizeof(int));
   }

   CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(recver, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(1000);

   char* data;
   int size = 7000;
   data = new char[size];
   uint64_t* arr_time = new uint64_t[75];
   char ** messages = new char*[75];

   int counter = 0;

   uint64_t time1, time2, start;
   struct timeval t;
   FILE *fp1=fopen(filename1,"w");

   gettimeofday(&t, NULL);
   start = t.tv_sec *1000000L + t.tv_usec;
   char *message;

   while (true)
   {
      int rs;
      int rsize = 0;
      gettimeofday(&t, NULL);
      time1 = t.tv_sec *1000000L + t.tv_usec;
      
      int rcv_size;
      int var_size = sizeof(int);
      //UDT::getsockopt(recver, 0, UDT_RCVDATA, &rcv_size, &var_size);
      //if (rcv_size >= 1) {
         //if (counter > 0)
         //   memcpy(data2, data, size);
         while (rsize < size)
         {
            if (UDT::ERROR == (rs = UDT::recvmsg(recver, data+rsize, size-rsize)))
            {
               //cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
               //break;
               continue;
            }
            rsize += rs;
         }  
         message = new char[6];
         memcpy(message, data, 6);
         messages[counter] = message;
         //if (UDT::ERROR == rs)
         //   break;
         gettimeofday(&t, NULL);
         time2 = t.tv_sec *1000000L + t.tv_usec;
         arr_time[counter] = time2;     
         //fprintf(fp1, "%d %ld \n", atoi(data), time2);
         //fflush(fp1);
         //send(app_sock, data, size, 0);
         //printf("Oh dear, something went wrong with read()! %s\n", strerror(errno));
         //gettimeofday(&t, NULL);
         //time2 = t.tv_sec *1000000L + t.tv_usec;
         //fprintf(fp1, "%d %ld \n", atoi(data), time2);
         //fflush(fp1);
         //cout << endl;
         //cout << "rec: " << atoi(data) << endl;
         //fprintf(fp2, "%ld \n", time2 - time1);
         //fflush(fp2);
         //time1 = time2;
         counter+= 1;
         if (counter == 75)
             break;
         //if (atoi(data) - atoi(data2) != 1) 
      //}
   }

   for (int i = 0; i<75; i++) {
      fprintf(fp1, "%c ", messages[i][0]);
      fprintf(fp1, "%d ", atoi(messages[i]+1));
      fprintf(fp1, "%d ", messages[i][5]);
      fprintf(fp1, "%ld \n", arr_time[i]);
      delete [] messages[i];
   }
   fflush(fp1);

   delete [] data; 
   delete [] arr_time;
   delete [] messages;

   UDT::close(recver);

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
