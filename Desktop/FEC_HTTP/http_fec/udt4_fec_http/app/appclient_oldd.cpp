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
void* monitor(void*);
#else
DWORD WINAPI monitor(LPVOID);
#endif

int main(int argc, char* argv[])
{
   /*if ((3 != argc) || (0 == atoi(argv[2])))
   {
      cout << "usage: appclient server_ip server_port" << endl;
      return 0;
   }*/

   UDTUpDown _udt_;

   struct addrinfo hints, *local, *peer;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   //hints.ai_socktype = SOCK_STREAM;
   hints.ai_socktype = SOCK_DGRAM;

   if (0 != getaddrinfo(NULL, "9000", &hints, &local))
   {
      cout << "incorrect network address.\n" << endl;
      return 0;
   }

   UDTSOCKET client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);

   //UDT Options
   //UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   //UDT::setsockopt(client, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(client, 0, UDT_SNDBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(client, 0, UDP_SNDBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(client, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));

   if(argc > 3) {
      UDT::setsockopt(client, 0, UDT_FECROWRATE, new int(atoi(argv[3])), sizeof(int));
      UDT::setsockopt(client, 0, UDT_FECCOLRATE, new int(atoi(argv[4])), sizeof(int));
   }

   #ifdef WIN32
      UDT::setsockopt(client, 0, UDT_MSS, new int(1052), sizeof(int));
   #endif


   freeaddrinfo(local);

   if (0 != getaddrinfo(argv[1], argv[2], &hints, &peer))
   {
      cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2] << endl;
      return 0;
   }

   if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   freeaddrinfo(peer);

   // using CC method
   /*CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(client, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(1000);*/

   int size = 80000;
   //int size = 50;
   char* data = new char[size];

   #ifndef WIN32
      //pthread_create(new pthread_t, NULL, monitor, &client);
   #else
      CreateThread(NULL, 0, monitor, &client, 0, NULL);
   #endif

   /*for (int i = 0; i < 20; i ++)
   {
      int ssize = 0;
      int ss;
      while (ssize < size)
      {
         if (UDT::ERROR == (ss = UDT::send(client, data + ssize, size - ssize, 0)))
         {
            cout << "send:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         ssize += ss;
      }

      if (ssize < size)
         break;
   }*/
   int counter = 0;

   uint64_t time1;
   struct timeval t;
   //FILE *fp=fopen(argv[3],"w");


   for (int i = 0; i < 1000; i ++)
   {
      int ss;

      sprintf(data, "%d", i);

      ss = UDT::sendmsg(client, data, size, -1, true); // bool inorder = false
      
      if (ss < size) {
         cout << "only send " << ss << "out of " << size << endl;
         break;
      }
      //gettimeofday(&t, NULL);
      //time1 = t.tv_sec *1000000L + t.tv_usec;
      //cout <<i << " "<< time1 << endl;
      //fprintf(fp, "%d %ld \n", i, time1);
      //fflush(fp);
      counter += 1;
   }

   cout << "# of messages: " << counter << endl;

   //sleep (10000);

   UDT::close(client);
   delete [] data;
   return 0;
}

#ifndef WIN32
void* monitor(void* s)
#else
DWORD WINAPI monitor(LPVOID s)
#endif
{
   UDTSOCKET u = *(UDTSOCKET*)s;

   UDT::TRACEINFO perf;

   cout << "SendRate(Mb/s)\tRTT(ms)\tCWnd\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

   while (true)
   {
      #ifndef WIN32
         sleep(1);
      #else
         Sleep(1000);
      #endif

      if (UDT::ERROR == UDT::perfmon(u, &perf))
      {
         cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
         break;
      }

      cout << perf.mbpsSendRate << "\t\t" 
           << perf.msRTT << "\t" 
           << perf.pktCongestionWindow << "\t" 
           << perf.usPktSndPeriod << "\t\t\t" 
           << perf.pktRecvACK << "\t" 
           << perf.pktRecvNAK << endl;
   }

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
