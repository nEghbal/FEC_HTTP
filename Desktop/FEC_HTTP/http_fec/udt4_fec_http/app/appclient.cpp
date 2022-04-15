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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <iostream>
#include <udt.h>
#include "cc.h"
#include "test_util.h"
#define SA struct sockaddr

using namespace std;

#ifndef WIN32
void* monitor(void*);
#else
DWORD WINAPI monitor(LPVOID);
#endif
 
int get_color(int i, int bg_color, int face_color, bool happy)
{
   /*if (i == 42 || i == 43 || i == 44 || i == 45 || i == 48 || i == 49 || i == 54 || i == 55 || i == 56  || i == 57 || i == 62 || i == 63 || i == 64 || i == 65)
      return face_color;
   else if (i == 68 || i == 69 || i == 74 || i == 75 || i == 76 || i == 77 || i == 82 || i == 83 || i == 84  || i == 85 || i == 88 || i == 89 || i == 94 || i == 95 || i == 96 || i == 97)
      return face_color;
   else if (i == 102 || i == 103 || i == 104 || i == 105 || i == 108 || i == 109 || i == 114 || i == 115 || i == 116 || i == 117 || i == 128 || i == 129 || i == 148 || i == 149 || i == 168 || i == 169)
      return face_color;
   else if (i == 170 || i == 171 || i == 172 || i == 173 || i == 188 || i == 189 || i == 190 || i == 191 || i == 192  || i == 193)
      return face_color;
   else if (happy && (i == 204 || i == 205 || i == 216 || i == 217 || i == 224 || i == 225 || i == 236 || i == 237 || i == 246 || i == 247 || i == 254 || i == 255 || i == 266 || i == 267 || i == 274 || i == 275 ))
      return face_color;
   else if ((i <= 293 && i >= 288) || (i <= 313 && i >= 308))
      return face_color;
   else if (!happy && (i == 326 || i == 327 || i == 334 || i == 335 || i == 346 || i == 347 || i == 354 || i == 355 || i == 364 || i == 365 || i == 376 || i == 377 || i == 384 || i == 385 || i == 396 || i == 397))
      return face_color;
   else
      return bg_color;*/
   return face_color;
}
  
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
   UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
   //UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
   UDT::setsockopt(client, 0, UDT_MSS, new int(9000), sizeof(int));
   UDT::setsockopt(client, 0, UDT_SNDBUF, new int(1050000000), sizeof(int));
   UDT::setsockopt(client, 0, UDP_SNDBUF, new int(1050000000), sizeof(int));
   //UDT::setsockopt(client, 0, UDT_MAXBW, new int64_t(1500000000), sizeof(int));

   bool order;
   if(argc > 3) {
      if (int(atoi(argv[3])) == 0)
          order = false;
      else
          order = true;
      if(argc == 6) {
          UDT::setsockopt(client, 0, UDT_FECROWRATE, new int(atoi(argv[4])), sizeof(int));
          UDT::setsockopt(client, 0, UDT_FECCOLRATE, new int(atoi(argv[5])), sizeof(int));
      }
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
   CUDPBlast* cchandle = NULL;
   int temp;
   UDT::getsockopt(client, 0, UDT_CC, &cchandle, &temp);
   if (NULL != cchandle)
   cchandle->setRate(1000);

   int size = 7000;
   char* data = new char[size];
   int face_color1 = 0;
   int face_color2 = 1;
   int bg_color1 = 9;
   bool happy1 = true;
   bool happy2 = false;
   int bg_color2 = 10;

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
   int counter1 = 0;
   int counter2 = 0;
   int counter = 0;
   char window;

   uint64_t time1, time2;
   time1 = 0;
   struct timeval t;
   //FILE *fp=fopen(argv[3],"w");
   int color;

   for (int i = 0; i < 1; i ++)
   {
      //if (time1 > 0) {
      //   gettimeofday(&t, NULL);
      //   time2 = t.tv_sec *1000000L + t.tv_usec;
      //   usleep(170000);
      //}
      for (unsigned int j = 0; j < 25; j ++)
      {

         int ss;
         window = '1';

         //sprintf(data, "%d", counter1);

         memcpy(data, &window, sizeof(char));
         //memcpy(data+sizeof(char), &j, sizeof(unsigned int));
         sprintf(data+sizeof(char), "%d", counter1);
         color = get_color(j, bg_color1, face_color1, happy1);
         memcpy(data+sizeof(char)+sizeof(int), &color, sizeof(int));
        
         if (argc < 6)          
            ss = UDT::sendmsg(client, data, size, -1, order);
         else
            ss = UDT::sendmsg(client, data, size, -1, order, true);

         if (ss < size) {
            cout << "only send " << ss << "out of " << size << endl;
            break;
         }
         counter1 += 1;
         if (counter1 == 10000)
            counter1 = 0;
         counter += 1;
      }
      
      for (unsigned int j = 0; j < 50; j ++)
      {

         int ss;
         window = '2';

         memcpy(data, &window, sizeof(char));
         //memcpy(data+sizeof(char), &j, sizeof(unsigned int));
         sprintf(data+sizeof(char), "%d", counter2);
         color = get_color(j, bg_color2, face_color2, happy2);
         memcpy(data+sizeof(char)+sizeof(int), &color, sizeof(int));

         ss = UDT::sendmsg(client, data, size, -1, order);

         if (ss < size) {
            cout << "only send " << ss << "out of " << size << endl;
            break;
         }
         
         counter2 += 1;
         if (counter2 == 10000)
            counter2 = 0;
         counter += 1;
      }
 
      if (face_color1+1 > 8)
            face_color1 = 0;
         else
            face_color1 += 1;
         if (happy1)
            happy1 = false;
         else
            happy1 = true;
         if (bg_color1 == 9)
            bg_color1 = 10;
         else
            bg_color1 = 9;
    
      if (face_color2+1 > 8)
            face_color2 = 0;
         else
            face_color2 += 1;
         if (happy2)
            happy2 = false;
         else
            happy2 = true;
         if (bg_color2 == 9)
            bg_color2 = 10;
         else
            bg_color2 = 9; 

     // if (!order)
     //    UDT::sendfence(client);
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
