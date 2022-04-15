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

using namespace std;

#ifndef WIN32
void* recvdata(void*, int, int);
#else
DWORD WINAPI recvdata(LPVOID);
#endif

int main(int argc, char* argv[])
{
   /*if ((1 != argc) && ((2 != argc) || (0 == atoi(argv[1]))))
   {
      cout << "usage: appserver [server_port]" << endl;
      return 0;
   }*/

   // Automatically start up and clean up UDT module.
   UDTUpDown _udt_;

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   //hints.ai_socktype = SOCK_DGRAM;

   string service("9000");
   if (argc >= 2)
      service = argv[1];

   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return 0;
   }

   UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

   // UDT Options
   //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   UDT::setsockopt(serv, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(serv, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(serv, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(serv, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));
  
   if (argc == 4) {
      UDT::setsockopt(serv, 0, UDT_FECROWRATE, new int(atoi(argv[2])), sizeof(int));
      UDT::setsockopt(serv, 0, UDT_FECCOLRATE, new int(atoi(argv[3])), sizeof(int));
   }

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
         if (argc == 4) 
             recvdata(new UDTSOCKET(recver), atoi(argv[2]), atoi(argv[3]));
         else
             recvdata(new UDTSOCKET(recver), 0, 0);
         //pthread_detach(rcvthread);
      #else
         CreateThread(NULL, 0, recvdata, new UDTSOCKET(recver), 0, NULL);
      #endif
   }

   UDT::close(serv);

   return 0;
}

#ifndef WIN32
void* recvdata(void* usocket, int row_rate, int col_rate)
#else
DWORD WINAPI recvdata(LPVOID usocket)
#endif
{
   UDTSOCKET recver = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   UDT::setsockopt(recver, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(recver, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(recver, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(recver, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));
   if (row_rate > 0 ) {
       UDT::setsockopt(recver, 0, UDT_FECROWRATE, new int(row_rate), sizeof(int));
       UDT::setsockopt(recver, 0, UDT_FECCOLRATE, new int(col_rate), sizeof(int));
   }
   char* data;
   int size = 100000+sizeof(int);
   data = new char[size];
   char *data_2 = new char[sizeof(int)];
   int num_of_messages = 10000;
   int i;

   for (i = 0; i< num_of_messages; i++)
   {
      int rsize = 0;
      int rs;
      while (rsize < size)
      {
         if (UDT::ERROR == (rs = UDT::recv(recver, data + rsize, size - rsize, 0)))
         {
            cout << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         rsize += rs;
      }

      int ssize = 0;
      int ss;
      memcpy(data_2, data, sizeof(int));
      while (ssize < sizeof(int))
      {
         if (UDT::ERROR == (ss = UDT::send(recver, data_2 + ssize, sizeof(int) - ssize, 0)))
         {
            cout << "send:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         ssize += ss;
      }
   }

   delete [] data;

   UDT::close(recver);

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
