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
#include <iostream>
#include <udt.h>
#include "cc.h"
#include "test_util.h"
#include <sys/time.h>

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

   // Automatically start up and clean up UDT module.
   UDTUpDown _udt_;

   struct addrinfo hints, *local, *peer;

   memset(&hints, 0, sizeof(struct addrinfo));

   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   //hints.ai_socktype = SOCK_DGRAM;

   if (0 != getaddrinfo(NULL, "9000", &hints, &local))
   {
      cout << "incorrect network address.\n" << endl;
      return 0;
   }

   UDTSOCKET client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);

   // UDT Options
   //UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   UDT::setsockopt(client, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(client, 0, UDT_SNDBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(client, 0, UDP_SNDBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(client, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));
   //UDT::setsockopt(client, 0, UDT_FECRATE, new int(70), sizeof(int));
   if (argc == 6) {
      UDT::setsockopt(client, 0, UDT_FECROWRATE, new int(atoi(argv[4])), sizeof(int));
      UDT::setsockopt(client, 0, UDT_FECCOLRATE, new int(atoi(argv[5])), sizeof(int));
   }
   // Windows UDP issue
   // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
   #ifdef WIN32
      UDT::setsockopt(client, 0, UDT_MSS, new int(1052), sizeof(int));
   #endif

   // for rendezvous connection, enable the code below
   /*
   UDT::setsockopt(client, 0, UDT_RENDEZVOUS, new bool(true), sizeof(bool));
   if (UDT::ERROR == UDT::bind(client, local->ai_addr, local->ai_addrlen))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   */

   freeaddrinfo(local);

   if (0 != getaddrinfo(argv[1], argv[2], &hints, &peer))
   {
      cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2] << endl;
      return 0;
   }

   // connect to the server, implict bind
   if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   freeaddrinfo(peer);

   // using CC method
   //CUDPBlast* cchandle = NULL;
   //int temp;
   //UDT::getsockopt(client, 0, UDT_CC, &cchandle, &temp);
   //if (NULL != cchandle)
   //   cchandle->setRate(500);

   int size = 100000+sizeof(int);
   char* data = new char[size];
   char* data_2 = new char[sizeof(int)];
   struct timeval t;
   FILE *fp=fopen(argv[3],"w");
   int num_of_messages = 10000;
   uint64_t sent_time[num_of_messages]; 
   uint64_t rec_time[num_of_messages];
   int seq_rec, seq_send = 0;

   #ifndef WIN32
      pthread_create(new pthread_t, NULL, monitor, &client);
   #else
      CreateThread(NULL, 0, monitor, &client, 0, NULL);
   #endif

   for (int i = 0; i < num_of_messages; i ++)
   {
      int ssize = 0;
      int ss;
      memcpy(data, &seq_send, sizeof(int));  
      while (ssize < size)
      {
         if (UDT::ERROR == (ss = UDT::send(client, data + ssize, size - ssize, 0)))
         {
            cout << "send:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         ssize += ss;
      }
      gettimeofday(&t, NULL);
      sent_time[seq_send] = t.tv_sec *1000000L + t.tv_usec;
      seq_send += 1;

      int rsize = 0;
      int rs;
      while (rsize < sizeof(int))
      {
         if (UDT::ERROR == (rs = UDT::recv(client, data_2 + rsize, sizeof(int) - rsize, 0)))
         {
            cout << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         rsize += rs;
      }
      memcpy(&seq_rec, data_2, sizeof(int));
      gettimeofday(&t, NULL);
      rec_time[seq_rec] = t.tv_sec *1000000L + t.tv_usec;
      cout << "rec: " << seq_rec << "RTT: " << rec_time[seq_rec]-sent_time[seq_rec] << endl;
   }

   for (int i = 0; i < num_of_messages; i ++)
      fprintf(fp, "%ld \n", rec_time[i]-sent_time[i]);

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
