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
void* recvdata(void*, char *, char *, int, int);
#else
DWORD WINAPI recvdata(LPVOID);
#endif

int main(int argc, char* argv[])
{

   UDTUpDown _udt_;

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   //hints.ai_socktype = SOCK_DGRAM;

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
   //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   UDT::setsockopt(serv, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(serv, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(serv, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(serv, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));

   if (argc == 6) {
      UDT::setsockopt(serv, 0, UDT_FECROWRATE, new int(atoi(argv[4])), sizeof(int));
      UDT::setsockopt(serv, 0, UDT_FECCOLRATE, new int(atoi(argv[5])), sizeof(int));
   }

   /*CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(serv, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(500);*/

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
         if (argc == 6) 
             recvdata(new UDTSOCKET(recver), argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
         else
             recvdata(new UDTSOCKET(recver), argv[2], argv[3], 0, 0);
      #else
         CreateThread(NULL, 0, recvdata, new UDTSOCKET(recver), 0, NULL);
      #endif
   }

   UDT::close(serv);

   return 0;
}

#ifndef WIN32
void* recvdata(void* usocket, char *filename1, char *filename2, int row_rate, int col_rate)
#else
DWORD WINAPI recvdata(LPVOID usocket)
#endif
{
   UDTSOCKET recver = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   //UDT::setsockopt(recver, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(recver, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   UDT::setsockopt(recver, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(recver, 0, UDT_RCVBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(recver, 0, UDP_RCVBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(recver, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));
   //bool block = false;
   //UDT::setsockopt(recver, 0, UDT_RCVSYN, &block, sizeof(bool)); 
   if (row_rate > 0 ) {
       UDT::setsockopt(recver, 0, UDT_FECROWRATE, new int(row_rate), sizeof(int));
       UDT::setsockopt(recver, 0, UDT_FECCOLRATE, new int(col_rate), sizeof(int));
   }

   /*CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(recver, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(1000);*/

   FILE *fp1=fopen(filename1,"w");
   struct timeval t;
   char* data;
   int size = 100000;
   //int size = 50;
   data = new char[size];
   uint64_t time1, time2;

   while (true)
   {
      int rsize = 0;
      int rs;
      gettimeofday(&t, NULL);
      time1 = t.tv_sec *1000000L + t.tv_usec;
      while (rsize < size)
      {
         int rcv_size;
         int var_size = sizeof(int);
         UDT::getsockopt(recver, 0, UDT_RCVDATA, &rcv_size, &var_size);
         if (UDT::ERROR == (rs = UDT::recv(recver, data + rsize, size - rsize, 0)))
         {
            cout << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         rsize += rs;
      }

      if (rsize < size)
         break;
      else {
         gettimeofday(&t, NULL);
         time2 = t.tv_sec *1000000L + t.tv_usec;
         cout << time2 - time1 << endl;
         fprintf(fp1, "%ld \n", time2 - time1);
         fflush(fp1);
         time1 = time2;
      }
   }

   /*int counter = 0;

   uint64_t time1, time2, start;
   struct timeval t;
   FILE *fp1=fopen(filename1,"w");
   FILE *fp2=fopen(filename2,"w");

   gettimeofday(&t, NULL);
   start = t.tv_sec *1000000L + t.tv_usec;

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
         //if (UDT::ERROR == rs)
         //   break;
         gettimeofday(&t, NULL);
         time2 = t.tv_sec *1000000L + t.tv_usec;
         //fprintf(fp1, "%d %ld \n", atoi(data), time2);
         //fflush(fp1);
         cout << atoi(data) << "  "  << time2 - time1 << endl;
         //fprintf(fp2, "%ld \n", time2 - time1);
         //fflush(fp2);
         time1 = time2;
         counter+= 1;
         //if (atoi(data) - atoi(data2) != 1)
         //cout << atoi(data) << "  "  << time2 - start << endl;
    
      //}
   }*/

   delete [] data;

   UDT::close(recver);

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
