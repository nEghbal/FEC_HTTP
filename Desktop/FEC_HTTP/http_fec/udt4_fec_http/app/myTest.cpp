#ifndef WIN32
   #include <cstdlib>
   #include <cstring>
   #include <netdb.h>
   #include <signal.h>
   #include <unistd.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <wspiapi.h>
#endif
#include <algorithm>
#include <iostream>

#include "udt.h"

using namespace std;


const int g_IP_Version = AF_INET;
const int g_Socket_Type = SOCK_STREAM;
const char g_Localhost[] = "127.0.0.1";
const int g_Server_Port = 9000;


int createUDTSocket(UDTSOCKET& usock, int port = 0, bool rendezvous = false)
{
   addrinfo hints;
   addrinfo* res;
   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = g_IP_Version;
   hints.ai_socktype = g_Socket_Type;

   char service[16];
   sprintf(service, "%d", port);

   if (0 != getaddrinfo(NULL, service, &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return -1;
   }

   usock = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

   // since we will start a lot of connections, we set the buffer size to smaller value.
   int snd_buf = 16000;
   int rcv_buf = 16000;
   UDT::setsockopt(usock, 0, UDT_SNDBUF, &snd_buf, sizeof(int));
   UDT::setsockopt(usock, 0, UDT_RCVBUF, &rcv_buf, sizeof(int));
   snd_buf = 8192;
   rcv_buf = 8192;
   UDT::setsockopt(usock, 0, UDP_SNDBUF, &snd_buf, sizeof(int));
   UDT::setsockopt(usock, 0, UDP_RCVBUF, &rcv_buf, sizeof(int));
   int fc = 16;
   UDT::setsockopt(usock, 0, UDT_FC, &fc, sizeof(int));
   bool reuse = true;
   UDT::setsockopt(usock, 0, UDT_REUSEADDR, &reuse, sizeof(bool));
   UDT::setsockopt(usock, 0, UDT_RENDEZVOUS, &rendezvous, sizeof(bool));

   if (UDT::ERROR == UDT::bind(usock, res->ai_addr, res->ai_addrlen))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   freeaddrinfo(res);
   return 0;
}

int connect(UDTSOCKET& usock, int port)
{
   addrinfo hints, *peer;
   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family =  g_IP_Version;
   hints.ai_socktype = g_Socket_Type;

   char buffer[16];
   sprintf(buffer, "%d", port);

   if (0 != getaddrinfo(g_Localhost, buffer, &hints, &peer))
   {
      return -1;
   }

   UDT::connect(usock, peer->ai_addr, peer->ai_addrlen);

   freeaddrinfo(peer);
   return 0;
}

// Test basic data transfer.

const int g_TotalNum = 10000;

#ifndef WIN32
void* Test_1_Srv(void* param)
#else
DWORD WINAPI Test_1_Srv(LPVOID param)
#endif
{
   cout << "Testing simple data transfer.\n";

   UDTSOCKET serv;
   if (createUDTSocket(serv, g_Server_Port) < 0)
      return NULL;

   UDT::listen(serv, 1024);
   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);
   UDTSOCKET new_sock = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen);
   UDT::close(serv);

   if (new_sock == UDT::INVALID_SOCK)
   {
      return NULL;
   }

   int32_t buffer[g_TotalNum];
   fill_n(buffer, 0, g_TotalNum);

   int torecv = g_TotalNum * sizeof(int32_t);
   while (torecv > 0)
   {
      int rcvd = UDT::recv(new_sock, (char*)buffer + g_TotalNum * sizeof(int32_t) - torecv, torecv, 0);
      if (rcvd < 0)
      {
         cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
         return NULL;
      }
      torecv -= rcvd;
   }

   // check data
   for (int i = 0; i < g_TotalNum; ++ i)
   {
      if (buffer[i] != i)
      {
         cout << "DATA ERROR " << i << " " << buffer[i] << endl;
         break;
      }
   }

   int eid = UDT::epoll_create();
   UDT::epoll_add_usock(eid, new_sock);
   /*
   set<UDTSOCKET> readfds;
   if (UDT::epoll_wait(eid, &readfds, NULL, -1) > 0)
   {
      UDT::close(new_sock);
   }
   */

   UDTSOCKET readfds[1];
   int num = 1;
   if (UDT::epoll_wait2(eid, readfds, &num, NULL, NULL, -1) > 0)
   {
      UDT::close(new_sock);
   }

   return NULL;
}

#ifndef WIN32
void* Test_1_Cli(void* param)
#else
DWORD WINAPI Test_1_Cli(LPVOID param)
#endif
{
   UDTSOCKET client;
   if (createUDTSocket(client, 0) < 0)
      return NULL;

   connect(client, g_Server_Port);

   int32_t buffer[g_TotalNum];
   for (int i = 0; i < g_TotalNum; ++ i)
      buffer[i] = i;

   int tosend = g_TotalNum * sizeof(int32_t);
   while (tosend > 0)
   {
      int sent = UDT::send(client, (char*)buffer + g_TotalNum * sizeof(int32_t) - tosend, tosend, 0);
      if (sent < 0)
      {
         cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
         return NULL;
      }
      tosend -= sent;
   }

   UDT::close(client);
   return NULL;

}
  
int main()
{
   const int test_case = 1;

#ifndef WIN32
   void* (*Test_Srv[test_case])(void*);
   void* (*Test_Cli[test_case])(void*);
#else
   DWORD (WINAPI *Test_Srv[test_case])(LPVOID);
   DWORD (WINAPI *Test_Cli[test_case])(LPVOID);
#endif

   Test_Srv[0] = Test_1_Srv;
   Test_Cli[0] = Test_1_Cli;

   for (int i = 0; i < 1; ++ i)
   {
      cout << "Start Test # " << i + 1 << endl;
      UDT::startup();

#ifndef WIN32
      pthread_t srv, cli;
      pthread_create(&srv, NULL, Test_Srv[i], NULL);
      pthread_create(&cli, NULL, Test_Cli[i], NULL);
      pthread_join(srv, NULL);
      pthread_join(cli, NULL);
#else
      HANDLE srv, cli;
      srv = CreateThread(NULL, 0, Test_Srv[i], NULL, 0, NULL);
      cli = CreateThread(NULL, 0, Test_Cli[i], NULL, 0, NULL);
      WaitForSingleObject(srv, INFINITE);
      WaitForSingleObject(cli, INFINITE);
#endif

      UDT::cleanup();
      cout << "Test # " << i + 1 << " completed." << endl;
   }

   return 0;
}
