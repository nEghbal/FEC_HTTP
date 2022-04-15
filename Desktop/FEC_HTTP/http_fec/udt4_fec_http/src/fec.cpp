// Added by Nooshin

#include <iostream>
#include "fec.h"
#include "core.h"
#include <sys/time.h>
#include <cstring>

using namespace std;
int current_xor_row = 0;
int current_xor_col = 0;
uint64_t total_packets = 0;
struct timeval t;
int32_t pre_seq = -1;

CFEC::CFEC():
m_UDT(NULL),
m_prevLostSeq(-1)
{
}

void CFEC::setPayloadSize(int size)
{
   m_iPayloadSize = size;
}

void CFEC::writeToLogFile(const CPacket* packet)
{
   for (int i = 0; i < 4; ++ i)
      cout << packet->m_nHeader[i] << " ";
   cout << (char*)packet->m_pcData << endl;
}

//
XORFEC::XORFEC():
m_FECRate(-1),
m_current(NULL),
m_next(NULL),
m_currentSize(0),
m_nextSize(0),
m_FECIteration(0),
m_firstSeqCurrent(0),
m_SndXOR(NULL),
m_RcvXOR(NULL),
m_RecoveredPkt()
{
}

XORFEC::~XORFEC()
{
   delete [] m_next;
   delete [] m_current;
}

int XORFEC::findIndexInsert(int32_t firstSeq, const CPacket* packet)
{
   int index = -1;
   
   if (firstSeq + m_FECRate - 1 <= CSeqNo::m_iMaxSeqNo &&
       packet->m_iSeqNo >= firstSeq &&  
       packet->m_iSeqNo <= firstSeq + m_FECRate - 1)       
      index = packet->m_iSeqNo - firstSeq;                  
   
   else if (firstSeq + m_FECRate - 1 > CSeqNo::m_iMaxSeqNo) {
      if (packet->m_iSeqNo >= firstSeq && packet->m_iSeqNo <= CSeqNo::m_iMaxSeqNo)  
         index = packet->m_iSeqNo - firstSeq;
      else if (packet->m_iSeqNo <= (m_FECRate - 1 - (CSeqNo::m_iMaxSeqNo - firstSeq + 1)))
         index = (CSeqNo::m_iMaxSeqNo - firstSeq + 1) + packet->m_iSeqNo;
   }

   return index;
}

void XORFEC::insert(const CPacket* packet, CPacket** list, int index)
{
   list[index] = new CPacket();
   list[index]->m_pcData = new char[packet->getLength()];

   // copy packet content
   memcpy(list[index]->m_nHeader, packet->m_nHeader, CPacket::m_iPktHdrSize);
   memcpy(list[index]->m_pcData, packet->m_pcData, packet->getLength());
   list[index]->setLength(packet->getLength());
}

void XORFEC::setRate(int FECRate)
{
   m_FECRate = FECRate;

   m_current = new CPacket* [FECRate];
   m_next = new CPacket* [FECRate];
   for (int i = 0; i < FECRate; ++ i) {
      m_current[i] = NULL;
      m_next[i] = NULL;
   }
   m_firstSeqNext = m_firstSeqCurrent + m_FECRate;

   m_logFile.open ("/home/eghbal/udt4_fec/logFile.txt");
}

int XORFEC::getRate()
{
   return m_FECRate;
}

void XORFEC::init()
{
   //CUDT* u = CUDT::getUDTHandle(m_UDT);
   if (m_UDT != NULL)
      m_firstSeqCurrent = m_UDT->m_iPeerISN;
   
   if (m_FECRate != -1)
      m_firstSeqNext = m_firstSeqCurrent + m_FECRate;

   m_SndXOR = new XORPacket(m_iPayloadSize); 
}

void XORFEC::onPktSent(const CPacket* packet) //updating m_SndXOR
{
   
  
   if (m_SndXOR->m_payload_len == 0)
      m_SndXOR->init(packet);
   else 
      m_SndXOR->XOR(packet);
   
   m_FECIteration ++;

   // every m_FECRate packets, m_SndXOR is sent and m_FECIteration is reset to 0
   if (m_FECIteration == m_FECRate) {

      //for (int i = 0; i < 4; ++ i)
       //  cout << m_SndXOR->m_XORHeader[i] << " ";
     // cout << "firstSeq: " << m_SndXOR->m_firstSeq << " ";

      //cout << (char*)m_SndXOR->m_payload << endl;

     // cout << "----------\n";
      //CUDT* u = CUDT::getUDTHandle(m_UDT);
      m_UDT->sendRedundantPkt(m_SndXOR);
      
      m_SndXOR->m_payload_len = 0;
      m_FECIteration = 0;
   }
}

void XORFEC::onPktReceived(const CPacket* packet) //adding the packet to m_current/m_next and updating m_currentSize/m_nextSize
{
   //cout << " Packet receieved:" << packet->m_iSeqNo << endl << std::flush; 
   if (CSeqNo::seqcmp(packet->m_iSeqNo, m_firstSeqCurrent) < 0)
      return; // modify this to make sure all lost packet got received!

   int index = findIndexInsert(m_firstSeqCurrent, packet);
      
   if (index >= 0) {
      insert(packet, m_current, index);
      m_currentSize ++;
      //cout << "current: " << m_currentSize << "   next: "<< m_nextSize << endl << std::flush;
      if (m_currentSize == m_FECRate) {
         renewCurrentAndNext(false);
         m_RcvXOR = NULL;   
      }
      return;
   }                  

   index = findIndexInsert(m_firstSeqNext, packet);
   if (index >= 0) {
      insert(packet, m_next, index);
      m_nextSize ++;
      //cout << "current: " << m_currentSize << "   next: "<< m_nextSize << endl << std::flush;
      if (m_nextSize == m_FECRate) // m_next is full
         lossDetection(); 
      return;
   }
   
   //cout << "current: " << m_currentSize << "   next: "<< m_nextSize << endl << std::flush;
   //packet is out of m_current/m_next seq. range
   lossDetection(packet); 
   //cout << " Packet receieved: end" << endl << std::flush;
}

void XORFEC::processCustomMsg(const CPacket* XOR) //updating m_RcvXOR
{   
   //cout << " XOR receieved" << endl << std::flush;
   //build a XORPacket from XOR
   XORPacket* newXOR = new XORPacket(m_iPayloadSize);
   memcpy(newXOR->m_XORHeader, XOR->m_pcData, CPacket::m_iPktHdrSize);
   memcpy(&(newXOR->m_XORpayload_len), XOR->m_pcData+CPacket::m_iPktHdrSize, sizeof(uint32_t));
   newXOR->m_XORpayload_len = (uint32_t)newXOR->m_XORpayload_len;
   memcpy(&(newXOR->m_firstSeq), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t), sizeof(int32_t));
   newXOR->m_firstSeq = (int32_t)newXOR->m_firstSeq;
   memcpy(newXOR->m_payload, XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t), 
          XOR->getLength()- (CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)));
   newXOR->m_payload_len = XOR->getLength()- (CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t));

   //cout <<  "XOR: \n"<< std::flush;
   //for (int i = 0; i < 4; ++ i)
     // cout << newXOR->m_XORHeader[i] << " "<< std::flush;
   //cout << "firstSeq: " << newXOR->m_firstSeq << " " << std::flush;

   //cout << (char*)newXOR->m_payload << std::flush;
   //cout << "\n****************\n" << std::flush;

   if (CSeqNo::seqcmp(newXOR->m_firstSeq, m_firstSeqCurrent) < 0) {
      delete [] newXOR->m_payload;
      delete newXOR;
      // delete [] XOR.m_pcData; ???
      return;
   }
   
   m_RcvXOR = newXOR;
   if (m_RcvXOR->m_firstSeq == m_firstSeqCurrent)
      return;
   
   if (CSeqNo::seqcmp(m_RcvXOR->m_firstSeq, CSeqNo::decseq(m_firstSeqNext)) > 0)
      lossDetection();
}

int XORFEC::lossRecovery()  
{  
   if (m_currentSize == m_FECRate - 1 && m_RcvXOR != NULL &&
            m_RcvXOR->m_firstSeq == m_firstSeqCurrent && m_nextSize > 0) {
   
      int lostPktIndex = -1;
      for (int j = 0; j < m_FECRate; ++ j) {
         if (m_current[j] != NULL) {
            m_RcvXOR->XOR(m_current[j]);
            if (m_nextSize < m_FECRate) {
               delete [] m_current[j]->m_pcData;
               delete m_current[j];
               m_current[j] = m_next[j];
               m_next[j] = NULL;
            }
            else {
               delete [] m_current[j]->m_pcData;
               delete m_current[j];
               delete [] m_next[j]->m_pcData;
               delete m_next[j];
               m_current[j] = NULL;
               m_next[j] = NULL;
            }
         }
         else {
            lostPktIndex = j;
            if (m_prevLostSeq < 0)
               m_prevLostSeq = j + m_firstSeqCurrent;
            else {
               m_logFile << "Gap:" << j + m_firstSeqCurrent - m_prevLostSeq << endl;
               m_logFile.flush();
               //cout << "Lost gap: " << j + m_firstSeqCurrent - m_prevLostSeq << endl << std::flush;
               m_prevLostSeq = j + m_firstSeqCurrent;
            }
            if (m_nextSize < m_FECRate) {
               m_current[j] = m_next[j];
               m_next[j] = NULL; 
            }
            else {
               delete [] m_next[j]->m_pcData;
               delete m_next[j];
               m_current[j] = NULL;
               m_next[j] = NULL;
            }          
         }
      }
      if (m_nextSize < m_FECRate) {
         m_currentSize = m_nextSize;
         m_firstSeqCurrent = m_firstSeqNext; 
         m_nextSize = 0;
      }
      else {
         m_currentSize = 0;
         m_nextSize = 0;
         m_firstSeqCurrent = CSeqNo::incseq(m_firstSeqCurrent, 2*m_FECRate);
      }
      m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_FECRate);
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXOR->m_XORpayload_len];
      // copy m_RcvXOR content
      //cout << "Recovery Time: " << int(CTimer::getTime() - m_UDT->m_StartTime) << "  m_RecoveredPkt Length: "<< m_RcvXOR->m_XORpayload_len << endl << std::flush;
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXOR->m_XORHeader[i];
         //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
      }
      m_RecoveredPkt.m_pcData = m_RcvXOR->m_payload;
      m_RecoveredPkt.setLength(m_RcvXOR->m_XORpayload_len); 
    
      m_RcvXOR = NULL; 
   
      if (lostPktIndex != -1)
         return 1;
   }
   return 0; 
}

void XORFEC::lossReport(CPacket** list, int32_t firstSeq)  //sending NAK
{
   int32_t first_loss_seq, second_loss_seq = -1;
   //CUDT* u = CUDT::getUDTHandle(m_UDT);
   
   for (int i= 0; i < m_FECRate; ++ i) {
      if (list[i] == NULL) {
         first_loss_seq = CSeqNo::incseq(firstSeq, i);
         second_loss_seq = first_loss_seq;
         while (i+1 < m_FECRate && list[i+1] == NULL) {
            i = i+1;
            second_loss_seq = CSeqNo::incseq(firstSeq, i);
         }
         for (int j= first_loss_seq; j < second_loss_seq+1; ++ j) {
            if (m_prevLostSeq < 0)
               m_prevLostSeq = j;
            else {
               m_logFile << "Gap:" << j - m_prevLostSeq << endl;
               m_logFile.flush();
               //cout << "Lost gap: " << j - m_prevLostSeq << endl << std::flush;
               m_prevLostSeq = j;
            }
         }
         m_UDT->sendNAK(first_loss_seq, second_loss_seq);
         //cout << "FEC Loss Detection: " << first_loss_seq << " " << second_loss_seq << endl << std::flush;
      }
   }
}

void XORFEC::renewCurrentAndNext(bool empty_next) {
   if (m_nextSize == m_FECRate || empty_next) {
      
      for (int i= 0; i < m_FECRate; ++ i) {
         if (m_current[i] != NULL) {
            delete [] m_current[i]->m_pcData;
            delete m_current[i];
            m_current[i] = NULL;  
         }
         if (m_next[i] != NULL) {
            delete [] m_next[i]->m_pcData;
            delete m_next[i];
            m_next[i] = NULL;
         }
      }
      m_currentSize = 0;
      m_nextSize = 0;
         
      m_firstSeqCurrent = CSeqNo::incseq(m_firstSeqCurrent, 2*m_FECRate);   
   }
   else {
      for (int i= 0; i < m_FECRate; ++ i) {
         if (m_current[i] != NULL){
            delete [] m_current[i]->m_pcData;
            delete m_current[i];
         }
         m_current[i] = m_next[i];  
         m_next[i] = NULL;
      }
      m_currentSize = m_nextSize;
      m_firstSeqCurrent = m_firstSeqNext; 
      m_nextSize = 0;
   }
   
   m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_FECRate);
}

void XORFEC::lossDetection() 
{  
   if (m_nextSize == m_FECRate) {
      m_logFile << "next is full! Loss report on m_current!" << endl;
      m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      renewCurrentAndNext(false);
      m_RcvXOR = NULL; 
   }
   else if (m_RcvXOR->m_firstSeq == m_firstSeqNext) {
      m_logFile << "receiving next XOR! Loss report on m_current!" << endl;
      m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      renewCurrentAndNext(false);
   }
   else if (CSeqNo::seqcmp(m_RcvXOR->m_firstSeq, m_firstSeqNext) > 0) {
      m_logFile << "receiving XOR for after next! Loss report on both!" << endl;
      m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      lossReport(m_next, m_firstSeqNext);
      renewCurrentAndNext(true);
   }
}

void XORFEC::lossDetection(const CPacket* packet)
{ // just recieved a packet out of m_current/m_next range
   m_logFile << "receiving a packet out of current and next! Loss report on both!" << endl;
   m_logFile.flush();
   //lossReport(m_current, m_firstSeqCurrent);
   //lossReport(m_next, m_firstSeqNext);
   renewCurrentAndNext(true);
   m_RcvXOR = NULL;
   onPktReceived(packet);
}

//
twoDXORFEC::twoDXORFEC():
m_row_FECRate(-1),
m_col_FECRate(-1),
m_current(NULL),
m_next(NULL),
m_sndCurrent(NULL),
m_sndNext(NULL),
m_row_currentSize(NULL),
m_col_currentSize(NULL),
m_totalCurrentSize(0),
m_sndNextSize(0),
m_sndCurrentSize(0),
m_row_nextSize(NULL),
m_col_nextSize(NULL),
m_totalNextSize(0),
m_firstSeqCurrent(0),
m_SndXOR(NULL),
m_RcvXORCurrent(NULL),
m_RcvXORNext(NULL),
m_sndIndices(NULL),
m_RecoveredPkt()
{
}

twoDXORFEC::~twoDXORFEC()
{
   for (int i = 0; i < m_row_FECRate; ++ i) {
      for (int j = 0; j < m_col_FECRate; ++ j){
         if (m_current[i][j] !=NULL) {
            delete [] m_current[i][j]->m_pcData;
            delete m_current[i][j];
         }
         if (m_next[i][j] !=NULL) {
            delete [] m_next[i][j]->m_pcData;
            delete m_next[i][j];
         }
         if (m_sndCurrent[i][j] !=NULL) {
            delete [] m_sndCurrent[i][j]->m_pcData;
            delete m_sndCurrent[i][j];
         }
         if (m_sndNext[i][j] !=NULL) {
            delete [] m_sndNext[i][j]->m_pcData;
            delete m_sndNext[i][j];
         }
      }
      if (m_current[i] !=NULL) 
         delete [] m_current[i];
      if (m_next[i] !=NULL)
         delete [] m_next[i];
      if (m_sndCurrent[i] !=NULL)
         delete [] m_sndCurrent[i];
      if (m_sndNext[i] !=NULL)
         delete [] m_sndNext[i];
   }
   
   delete [] m_current;
   delete [] m_next;
   delete [] m_sndCurrent;
   delete [] m_sndNext;
   
   delete [] m_row_currentSize;
   delete [] m_col_currentSize;
   delete [] m_row_nextSize;
   delete [] m_col_nextSize;
   delete [] m_RcvXORCurrent;
   delete [] m_RcvXORNext;
   delete [] m_SndXOR;
   delete [] m_sndIndices;
   delete [] thread_args;
   delete [] m_busyThreads;
}

void twoDXORFEC::init2()
{
   if (m_UDT != NULL)
      m_firstSeqCurrent = m_UDT->m_iPeerISN;

   m_row_currentSize = new int[m_row_FECRate];
   m_col_currentSize = new int[m_col_FECRate];
   m_current = new CPacket** [m_row_FECRate];
   m_next = new CPacket** [m_row_FECRate];
   m_sndCurrent = new CPacket** [m_row_FECRate];
   m_sndNext = new CPacket** [m_row_FECRate];
   m_row_nextSize = new int[m_row_FECRate];
   m_col_nextSize = new int[m_col_FECRate];
   m_SndXOR = new twoDXORPacket*[m_col_FECRate+m_row_FECRate];
   m_busyThreads = new bool[m_row_FECRate+m_col_FECRate];
   filethread = new pthread_t[m_row_FECRate+m_col_FECRate];
   thread_args = new XOR_thread_arg*[m_row_FECRate+m_col_FECRate];
   for (int i = 0; i < m_row_FECRate; ++ i) {
      m_row_currentSize[i] = 0;
      m_row_nextSize[i] = 0;
      m_current[i] = new CPacket* [m_col_FECRate];
      m_next[i] = new CPacket* [m_col_FECRate];
      m_sndCurrent[i] = new CPacket* [m_col_FECRate];
      m_sndNext[i] = new CPacket* [m_col_FECRate];
      for (int j = 0; j < m_col_FECRate; ++ j) {
         if (i == 0) {
            m_col_currentSize[j] = 0;
            m_col_nextSize[j] = 0;
         }
         m_current[i][j] = new CPacket();
         m_next[i][j] = new CPacket();
         m_sndCurrent[i][j] = new CPacket();
         m_sndNext[i][j] = new CPacket();
         if (m_UDT != NULL) {
            m_current[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_next[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndCurrent[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndNext[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         }
         m_current[i][j]->setLength(0);
         m_next[i][j]->setLength(0);
         m_sndCurrent[i][j]->setLength(0);
         m_sndNext[i][j]->setLength(0);
      }
   }

   m_RcvXORCurrent = new twoDXORPacket*[m_row_FECRate + m_col_FECRate];
   m_RcvXORTemp = NULL;
   m_RcvXORNext = new twoDXORPacket*[m_row_FECRate];  

   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i) {
      m_busyThreads[i] = false;
      m_RcvXORCurrent[i] = NULL;
      if (i<m_row_FECRate)
         m_RcvXORNext[i] = NULL;
      thread_args[i] = new XOR_thread_arg();
      if (m_UDT != NULL)
         m_SndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);
   }

   m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_row_FECRate*m_col_FECRate);

   m_sndIndices = new int[2];
   m_sndIndices[0] = 0;
   m_sndIndices[1] = 0;

   m_totalCurrentSize = 0;
   m_totalNextSize = 0;
   m_sndCurrentSize = 0;
   m_sndNextSize = 0;

   m_RecoveredTotal = 0;
   m_notRecoveredTotal = 0;

   m_logFile.open ("/home/eghbal/udt4_fec/logFile.txt");
   m_XORFileRead.open ("/dev/shm/eghbal/XOR_1.txt");
   m_XORFileWrite.open ("/dev/shm/eghbal/XOR_2.txt");
}

void twoDXORFEC::setRowRate(int row_FECRate)
{
   if (m_UDT != NULL)
      m_firstSeqCurrent = m_UDT->m_iPeerISN;
      
   m_row_FECRate = row_FECRate;

   if (m_col_FECRate > 0)
      init2();
}

void twoDXORFEC::setColRate(int col_FECRate)
{
   if (m_UDT != NULL)
      m_firstSeqCurrent = m_UDT->m_iPeerISN;

   m_col_FECRate = col_FECRate;

   if (m_row_FECRate > 0)
      init2();
}

int twoDXORFEC::getRate(bool row)
{
   if (row)
      return m_row_FECRate;
   else
      return m_col_FECRate;
}

void twoDXORFEC::init()
{
   //CUDT* u = CUDT::getUDTHandle(m_UDT);
   if (m_UDT != NULL)
      m_firstSeqCurrent = m_UDT->m_iPeerISN;
   
   if (m_row_FECRate != -1) {
      m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_row_FECRate*m_col_FECRate);

      for (int i = 0; i < m_row_FECRate; ++ i) {
         m_SndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);

         for (int j = 0; j < m_col_FECRate; ++ j) { 
            if (i == 0) 
               m_SndXOR[m_row_FECRate+j] = new twoDXORPacket(m_UDT->m_iPayloadSize);
            m_current[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_next[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndCurrent[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndNext[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         }
      }
   }
}

int* twoDXORFEC::findIndexInsert(int32_t firstSeq, int32_t seqNo)
{
   int *indices = new int [2];
   indices[0] = -1;
   indices[1] = -1;
   
   if (firstSeq + m_row_FECRate*m_col_FECRate - 1 <= CSeqNo::m_iMaxSeqNo &&
       seqNo >= firstSeq &&  
       seqNo <= firstSeq + m_row_FECRate*m_col_FECRate - 1) {
             
      indices[0] = (seqNo - firstSeq)/m_col_FECRate;
      indices[1] = (seqNo - firstSeq)%m_col_FECRate;
   }
   
   else if (firstSeq + m_row_FECRate*m_col_FECRate - 1 > CSeqNo::m_iMaxSeqNo) {
      if (seqNo >= firstSeq) { 
         indices[0] = (seqNo - firstSeq)/m_col_FECRate;
         indices[1] = (seqNo - firstSeq)%m_col_FECRate;  
      }
      else if (seqNo <= (m_row_FECRate*m_col_FECRate - 1 - (CSeqNo::m_iMaxSeqNo - firstSeq + 1))) {
         indices[0] = ((CSeqNo::m_iMaxSeqNo - firstSeq + 1) + seqNo)/m_col_FECRate;
         indices[1] = ((CSeqNo::m_iMaxSeqNo - firstSeq + 1) + seqNo)%m_col_FECRate;
      }
   }
   //cout << indices[0] << "  " << indices[1] << endl << std::flush;
   return indices;
}

void twoDXORFEC::insert(const CPacket* packet, CPacket** list, int index)
{
   //cout << " Packet insert:" << m_UDT->m_iPayloadSize << endl << std::flush;
   // copy packet content
   memcpy(list[index]->m_nHeader, packet->m_nHeader, CPacket::m_iPktHdrSize);
   memcpy(list[index]->m_pcData, packet->m_pcData, packet->getLength());
   list[index]->setLength(packet->getLength());
   //cout << " Packet insert: end" << packet->m_iSeqNo << endl << std::flush;
}

void* XOR(void *input) 
{
   XOR_thread_arg *arg = (XOR_thread_arg *)input;
   if (arg->row) {
      for (int i = 0; i < arg->size; ++ i) {
         if (i == 0) 
            arg->XOR->init(arg->list[arg->index][i], arg->row);
         else 
            arg->XOR->XOR(arg->list[arg->index][i]);
      }
   }
   else {
      for (int i = 0; i < arg->size; ++ i) {
         if (i == 0)
            arg->XOR->init(arg->list[i][arg->index], arg->row);
         else
            arg->XOR->XOR(arg->list[i][arg->index]);
      }
   }
   arg->end = true;
 }

void twoDXORFEC::rowXORProcessing(int index) 
{
   if (m_busyThreads[index] == false) { 
         thread_args[index]->list = m_sndCurrent;
         thread_args[index]->size = m_col_FECRate;
         thread_args[index]->row = true;
         thread_args[index]->index = index;
         thread_args[index]->end = false;
         thread_args[index]->XOR = m_SndXOR[index];
         m_busyThreads[index] = true;

         pthread_create(&(filethread[index]), NULL, XOR, thread_args[index]);
         pthread_detach(filethread[index]);
      }
      else
         cout << "cannot create thread for row "<< index << endl << std::flush;
}

void twoDXORFEC::colXORProcessing(int index)
{
      if (m_busyThreads[m_row_FECRate+index] == false) {
         thread_args[m_row_FECRate+index]->list = m_sndCurrent;
         thread_args[m_row_FECRate+index]->size = m_row_FECRate;
         thread_args[m_row_FECRate+index]->row = false;
         thread_args[m_row_FECRate+index]->index = index;
         thread_args[m_row_FECRate+index]->end = false;
         thread_args[m_row_FECRate+index]->XOR = m_SndXOR[m_row_FECRate+index];
         m_busyThreads[m_row_FECRate+index] = true;

         pthread_create(&(filethread[m_row_FECRate+index]), NULL, XOR, thread_args[m_row_FECRate+index]);
         pthread_detach(filethread[m_row_FECRate+index]);
      }
      else 
         cout << "cannot create thread for col "<< index << endl << std::flush;
}

void twoDXORFEC::onPktSent_read(CPacket* packet)
{
   int XOR_len;
   char* XOR;

   if (m_sndIndices[1] == 0)
      m_SndXOR[m_sndIndices[0]]->headerXOR(packet, true);
   else
      m_SndXOR[m_sndIndices[0]]->headerXOR(packet, false);
   if (m_sndIndices[1] == m_col_FECRate - 1) {
      m_XORFileRead >> XOR_len;
      XOR = new char[XOR_len];
      m_XORFileRead.read(XOR, XOR_len);
      memcpy(XOR, (char *)(m_SndXOR[m_sndIndices[0]]->m_XORHeader), CPacket::m_iPktHdrSize);
      //m_UDT->sendRedundantPkt(XOR, XOR_len);
   }

   if (m_sndIndices[0] == 0)
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->headerXOR(packet, true);
   else
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->headerXOR(packet, false);
   if (m_sndIndices[0] == m_row_FECRate - 1) {
      m_XORFileRead >> XOR_len;
      XOR = new char[XOR_len];
      m_XORFileRead.read(XOR, XOR_len);
      memcpy(XOR, (char *)(m_SndXOR[m_row_FECRate+m_sndIndices[1]]->m_XORHeader), CPacket::m_iPktHdrSize);
      //m_UDT->sendRedundantPkt(XOR, XOR_len);
   }
}

void twoDXORFEC::writeRedundantPktFile(twoDXORPacket* m_SndXOR)
{
   int XOR_len = CPacket::m_iPktHdrSize + sizeof(uint32_t) + sizeof(int32_t) + m_SndXOR->m_payload_len + sizeof(bool);
   char* XOR = new char[XOR_len];
   char *header = new char[CPacket::m_iPktHdrSize];
   memset (header, 0, CPacket::m_iPktHdrSize);
   memcpy(XOR, header, CPacket::m_iPktHdrSize);
   memcpy(XOR+CPacket::m_iPktHdrSize, &(m_SndXOR->m_XORpayload_len), sizeof(uint32_t));
   memcpy(XOR+CPacket::m_iPktHdrSize+sizeof(uint32_t), &(m_SndXOR->m_firstSeq), sizeof(int32_t));
   memcpy(XOR+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t), &(m_SndXOR->m_row), sizeof(bool));
   memcpy(XOR+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)+sizeof(bool), m_SndXOR->m_payload, m_SndXOR->m_payload_len);
   m_XORFileWrite << XOR_len;
   m_XORFileWrite.write(XOR, XOR_len);
   m_XORFileWrite.flush();
}

void twoDXORFEC::onPktSent_write(CPacket* packet)
{
   if (m_sndIndices[1] == 0)
      m_SndXOR[m_sndIndices[0]]->init(packet, true);
   else
      m_SndXOR[m_sndIndices[0]]->XOR(packet);
   if (m_sndIndices[1] == m_col_FECRate - 1) {
      writeRedundantPktFile(m_SndXOR[m_sndIndices[0]]);
   }
   
   if (m_sndIndices[0] == 0)
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->init(packet, false);
   else
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->XOR(packet);
   if (m_sndIndices[0] == m_row_FECRate - 1) {
      writeRedundantPktFile(m_SndXOR[m_row_FECRate+m_sndIndices[1]]);
   }
}

void twoDXORFEC::onPktSent(CPacket* packet)
{
   //cout << packet->m_iSeqNo << "  " << packet->getLength() << endl << std::flush;
   //onPktSent_read(packet);
   //onPktSent_write(packet);
   
   if (m_sndIndices[1] == 0)
      m_SndXOR[m_sndIndices[0]]->init(packet, true);
   else
      m_SndXOR[m_sndIndices[0]]->XOR(packet);
   if (m_sndIndices[1] == m_col_FECRate - 1) {
      m_UDT->sendRedundantPkt(m_SndXOR[m_sndIndices[0]]);
   }

   if (m_sndIndices[0] == 0)
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->init(packet, false);
   else
      m_SndXOR[m_row_FECRate+m_sndIndices[1]]->XOR(packet);
   if (m_sndIndices[0] == m_row_FECRate - 1) {
      m_UDT->sendRedundantPkt(m_SndXOR[m_row_FECRate+m_sndIndices[1]]);
   }

   if (m_sndIndices[0] == m_row_FECRate - 1 && m_sndIndices[1] == m_col_FECRate - 1) {
      m_sndIndices[0] = 0;
      m_sndIndices[1] = 0;
   }
   else if (m_sndIndices[1] < m_col_FECRate - 1)
      m_sndIndices[1]++;
   else if (m_sndIndices[1] == m_col_FECRate - 1) {
      m_sndIndices[0]++;
      m_sndIndices[1] = 0;
   }
}

void twoDXORFEC::onPktReceived(const CPacket* packet) //adding the packet to m_current/m_next and updating m_currentSize/m_nextSize
{
   //if (packet->m_iSeqNo - pre_seq2 > 1 || packet->m_iSeqNo - pre_seq2 < 0)
   //   cout << packet->m_iSeqNo << "   pre packet:   " <<  pre_seq2 << endl << std::flush; 
   //pre_seq2 = packet->m_iSeqNo;

   if (CSeqNo::seqcmp(packet->m_iSeqNo, m_firstSeqCurrent) < 0) {
      //cout << "out packet  "<< packet->m_iSeqNo << "current: " << m_firstSeqCurrent << "   next: " << m_firstSeqNext << endl << std::flush;
      return; // modify this to make sure all lost packet got received!

   }
 
   total_packets += 1;
   if (total_packets%1000 == 0) {
      m_logFile << m_RecoveredTotal << endl;
      m_logFile.flush();
   }
     
   int *indices;

   indices = findIndexInsert(m_firstSeqCurrent, packet->m_iSeqNo);     

   if (indices[0] >= 0) {
      insert(packet, m_current[indices[0]], indices[1]);
      m_row_currentSize[indices[0]] ++;
      m_col_currentSize[indices[1]] ++;
      m_totalCurrentSize ++;
      if (m_totalNextSize > 0) 
         ;//cout << "current size: " << m_totalCurrentSize << "    next size: " << m_totalNextSize << endl << std::flush;
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate) {
         renewCurrentAndNext(false);
      }
      else //any recoverable packet?
         lossRecoveryInvestigation (indices[0], indices[1], true);
      return;
   }                  

   indices = findIndexInsert(m_firstSeqNext, packet->m_iSeqNo);
   if (indices[0] >= 0) {
      insert(packet, m_next[indices[0]], indices[1]);
      m_row_nextSize[indices[0]] ++;
      m_col_nextSize[indices[1]] ++;
      m_totalNextSize ++;
      if (m_totalNextSize == 1) {
         //cout << "current size: " << m_totalCurrentSize << endl << std::flush;
         //cout << "packet  "<< packet->m_iSeqNo << "  first current: " << m_firstSeqCurrent << "   first next: " << m_firstSeqNext << endl << std::flush;
      }
      if (m_totalNextSize == m_row_FECRate*m_col_FECRate) {
         cout << "next is full!   current size: " << m_totalCurrentSize << endl << std::flush;
         lossDetection();
      }
      else //any recoverable packet?
         lossRecoveryInvestigation (indices[0], indices[1], false);
      return;
   }
   
   //packet is out of m_current/m_next seq. range
   cout << " Packet receieved:" << packet->m_iSeqNo <<"  m_firstSeqCurrent: " << m_firstSeqCurrent << endl << std::flush;
   lossDetection(packet); 
   //cout << " Packet receieved: end! " << endl << std::flush;
}

void twoDXORFEC::lossRecoveryInvestigation (int row_index, int col_index, bool current) 
{
   if (current) {

      /*if (col_index == m_col_FECRate - 1 && row_index > 0 && m_row_currentSize[row_index-1] == m_col_FECRate && 
          m_current[row_index][col_index]->m_iSeqNo < 1300 && m_RcvXORCurrent[row_index-1] != NULL) { 
         for (int j = 1; j < m_col_FECRate; ++ j) {
            m_RcvXORCurrent[row_index-1]->XOR(m_current[row_index-1][j]);
         }

         m_RecoveredPkt.m_pcData = new char[m_RcvXORCurrent[row_index-1]->m_XORpayload_len];*/
         /* copy m_RcvXOR content */
         /*for (int i = 0; i < 4; ++ i) {
            m_RecoveredPkt.m_nHeader[i] = m_RcvXORCurrent[row_index-1]->m_XORHeader[i];
            //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
            m_XORFileWrite << m_RecoveredPkt.m_nHeader[i] << " ";
            m_XORFileWrite << m_current[row_index-1][0]->m_nHeader[i] << " ";
         }
         m_XORFileWrite << endl;
         m_RecoveredPkt.m_pcData = m_RcvXORCurrent[row_index-1]->m_payload;
         m_RecoveredPkt.setLength(m_RcvXORCurrent[row_index-1]->m_XORpayload_len);

         m_XORFileWrite.write(m_RecoveredPkt.m_pcData, m_RcvXORCurrent[row_index-1]->m_XORpayload_len);
         m_XORFileWrite << endl;
         m_XORFileWrite.write(m_current[row_index-1][0]->m_pcData, m_current[row_index-1][0]->getLength());
         m_XORFileWrite << endl << endl;

         delete [] m_RecoveredPkt.m_pcData;
      }*/

      if (row_index >0 && m_row_currentSize[row_index-1] == m_col_FECRate - 1 && m_RcvXORCurrent[row_index-1] != NULL) {
         //cout << " Current row recovery! " << row_index-1 << endl << std::flush;
         lossRecovery(row_index-1, true);
      }
      for (int i = 0; i < col_index+1; ++ i) {
         if (row_index == m_row_FECRate - 1 && m_col_currentSize[i] == m_row_FECRate - 1 && m_RcvXORCurrent[m_row_FECRate+i] != NULL) 
            lossRecovery(i, false);
      }
   } 
   else {
      if (row_index == 0) {
         if (m_row_currentSize[m_row_FECRate-1] == m_col_FECRate - 1 && m_RcvXORCurrent[m_row_FECRate-1] != NULL) {
            //cout << " Current row recovery! " << m_row_FECRate-1 << endl << std::flush;
            lossRecovery(m_row_FECRate-1, true);
         }         

         for (int i = 0; i < m_col_FECRate; ++ i) {
            if (m_col_currentSize[i] == m_row_FECRate - 1 &&  m_RcvXORCurrent[m_row_FECRate+i] != NULL) {
               lossRecovery(i, false);
            }
         }
      }
      else if (row_index >0 && m_row_nextSize[row_index-1] == m_col_FECRate - 1 && m_RcvXORNext[row_index-1] != NULL) {
         //cout << " Recovery of next! row " << row_index-1 << endl << std::flush;
         lossRecoveryNext(row_index-1);
      }
   }
}

void twoDXORFEC::processCustomMsg(const CPacket* XOR) //updating m_RcvXOR
{  
   int* indices;
  
   //cout << " XOR seq:  "<< XOR->m_iSeqNo << endl << std::flush;
 
   if (XOR->m_iSeqNo == -65536) { // header
      //build a XORPacket from XOR
      m_RcvXORTemp = new twoDXORPacket(m_UDT->m_iPayloadSize);
      memcpy(m_RcvXORTemp->m_XORHeader, XOR->m_pcData, CPacket::m_iPktHdrSize);
      memcpy(&(m_RcvXORTemp->m_XORpayload_len), XOR->m_pcData+CPacket::m_iPktHdrSize, sizeof(uint32_t));
      m_RcvXORTemp->m_XORpayload_len = (uint32_t)m_RcvXORTemp->m_XORpayload_len;
      memcpy(&(m_RcvXORTemp->m_firstSeq), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t), sizeof(int32_t));
      m_RcvXORTemp->m_firstSeq = (int32_t)m_RcvXORTemp->m_firstSeq;
      memcpy(&(m_RcvXORTemp->m_row), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t), sizeof(bool));
      m_RcvXORTemp->m_row = (bool)m_RcvXORTemp->m_row;
      return;
   }
   else { //payload
      if (m_RcvXORTemp == NULL)
         return;
      m_RcvXORTemp->m_payload_len = XOR->getLength();
      memcpy(m_RcvXORTemp->m_payload, XOR->m_pcData, XOR->getLength());
   }
  
   //cout << " rec XOR len: "<< XOR->getLength() << endl << std::flush;

   //if (newXOR->m_firstSeq < 10000)
   //   cout << " XOR: "<< newXOR->m_firstSeq << endl << std::flush;

   //cout << "row: "<< newXOR->m_row << "  XOR: " << newXOR->m_firstSeq << "  current: " << m_firstSeqCurrent << "  next: " << m_firstSeqNext << endl << std::flush;
   //sleep (1);

   if (CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, m_firstSeqCurrent) < 0) {
      //cout << "row: "<< newXOR->m_row << "   " << newXOR->m_firstSeq << " < " << m_firstSeqCurrent << endl << std::flush;
      //delete [] newXOR->m_payload;
      //delete newXOR;
      // delete [] XOR.m_pcData; ???
      m_RcvXORTemp = NULL;
      return;
   }

   //cout << " XOR receieved 1 "<<newXOR->m_firstSeq << endl << std::flush;
   
   if (CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, CSeqNo::decseq(m_firstSeqNext)) > 0 && 
       CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, CSeqNo::incseq(m_firstSeqNext, m_row_FECRate*m_col_FECRate)) < 0 &&
       m_RcvXORTemp->m_row == false) {
       lossDetection(m_RcvXORTemp);
       m_RcvXORTemp = NULL;
   }
   else if (CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, CSeqNo::decseq(m_firstSeqNext)) > 0 && 
       CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, CSeqNo::incseq(m_firstSeqNext, m_row_FECRate*m_col_FECRate)) < 0 &&
       m_RcvXORTemp->m_row == true) {
      //cout << "3  row: "<< newXOR->m_row << "   newXOR->m_firstSeq: " << newXOR->m_firstSeq << "   firstSeqNext: " << m_firstSeqNext << endl << std::flush;
      indices = findIndexInsert(m_firstSeqNext, m_RcvXORTemp->m_firstSeq);
      if (indices[0] >= 0) {
         m_RcvXORNext[indices[0]] = m_RcvXORTemp;
         m_RcvXORTemp = NULL;
         //lossRecoveryInvestigation (indices[0], -1, false);
      }

   }
   else if (CSeqNo::seqcmp(m_RcvXORTemp->m_firstSeq, m_firstSeqNext) < 0) {
      //cout << "4  row: "<< newXOR->m_row << "   newXOR->m_firstSeq: " << newXOR->m_firstSeq << "<   firstSeqNext: " << m_firstSeqNext << endl << std::flush;
      indices = findIndexInsert(m_firstSeqCurrent, m_RcvXORTemp->m_firstSeq);
      if (indices[0] >= 0) {
         if (m_RcvXORTemp->m_row) {
            m_RcvXORCurrent[indices[0]] = m_RcvXORTemp;
            m_RcvXORTemp = NULL;
            if (m_row_currentSize[indices[0]] == m_col_FECRate - 1 && 
                ((indices[0] < m_col_FECRate - 1 && m_row_currentSize[indices[0]+1] > 0) || 
                    (indices[0] == m_col_FECRate - 1 && m_row_nextSize[0] > 0))) {
               //cout << " Current row recovery! " << indices[0] << endl << std::flush;
               lossRecovery(indices[0], true);
            }
         }
         else { 
            m_RcvXORCurrent[m_row_FECRate + indices[1]] = m_RcvXORTemp;
            m_RcvXORTemp = NULL;
            //lossRecoveryInvestigation (-1, indices[1], true);
         }
      }
   }
   //cout << " XOR receieved 2 "<<newXOR->m_firstSeq << endl << std::flush;                     
}

void twoDXORFEC::lossRecoveryNext(int index)
{
   int lostPktIndex = -1;
   for (int j = 0; j < m_col_FECRate; ++ j) {
      if (m_next[index][j]->getLength() > 0)
         m_RcvXORNext[index]->XOR(m_next[index][j]);
      else
         lostPktIndex = j;
   }

   m_RecoveredPkt.m_pcData = new char[m_RcvXORNext[index]->m_XORpayload_len];
   /* copy m_RcvXOR content */
   for (int i = 0; i < 4; ++ i) {
      m_RecoveredPkt.m_nHeader[i] = m_RcvXORNext[index]->m_XORHeader[i];
      /* cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush; */
   }
   m_RecoveredPkt.m_pcData = m_RcvXORNext[index]->m_payload;    
   m_RecoveredPkt.setLength(m_RcvXORNext[index]->m_XORpayload_len);

   m_RcvXORNext[index] = NULL;
   insert(&m_RecoveredPkt, m_next[index], lostPktIndex);
   m_row_nextSize[index] ++;
   m_col_nextSize[lostPktIndex] ++;
   m_totalNextSize ++;

   m_UDT->receiveRecoveredData();

   if (m_totalNextSize == m_row_FECRate*m_col_FECRate)
      lossDetection();

   m_RecoveredTotal ++;
   cout << "recovered! " << endl << std::flush;
}

void twoDXORFEC::lossRecovery(int index, bool row)  
{  
   //cout << "1   " << index << "   " << row << endl << std::flush;
   int counter = 0;
   int lostPktIndex = -1;
   if (row) {
      for (int j = 0; j < m_col_FECRate; ++ j) {
         if (m_current[index][j]->getLength() > 0) {
            //counter += 1;
            //cout << "XOR len: " << m_RcvXORCurrent[index]->m_payload_len << endl << std::flush;
            m_RcvXORCurrent[index]->XOR(m_current[index][j]);
         }
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXORCurrent[index]->m_XORpayload_len];
      /* copy m_RcvXOR content */
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXORCurrent[index]->m_XORHeader[i];
         //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
      }
      //cout << counter << "   packet length: " << m_RcvXORCurrent[index]->m_XORpayload_len << endl << std::flush;
      m_RecoveredPkt.m_pcData = m_RcvXORCurrent[index]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXORCurrent[index]->m_XORpayload_len); 
    
      //m_XORFileWrite.write(m_RecoveredPkt.m_pcData, m_RcvXORCurrent[index]->m_XORpayload_len);
      //m_XORFileWrite << endl;

      m_RcvXORCurrent[index] = NULL; 
      insert(&m_RecoveredPkt, m_current[index], lostPktIndex);
      m_row_currentSize[index] ++;
      m_col_currentSize[lostPktIndex] ++;
      m_totalCurrentSize ++;
   
      m_UDT->receiveRecoveredData();
      
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate)
         renewCurrentAndNext(false);
      else if (m_col_currentSize[lostPktIndex] == m_row_FECRate - 1 &&  m_RcvXORCurrent[m_row_FECRate+lostPktIndex] != NULL) {
         //cout << " Current col recovery 2nd! " << lostPktIndex << endl << std::flush;
         lossRecovery(lostPktIndex, false);
      }
   }
   else {
      for (int j = 0; j < m_row_FECRate; ++ j) {
         if (m_current[j][index]->getLength() > 0)
            m_RcvXORCurrent[m_row_FECRate+index]->XOR(m_current[j][index]);
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXORCurrent[m_row_FECRate+index]->m_XORpayload_len];
      /* copy m_RcvXOR content */
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXORCurrent[m_row_FECRate+index]->m_XORHeader[i];
         //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
      }
      //cout << endl << std::flush;
      m_RecoveredPkt.m_pcData = m_RcvXORCurrent[m_row_FECRate+index]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXORCurrent[m_row_FECRate+index]->m_XORpayload_len); 
    
      m_RcvXORCurrent[m_row_FECRate+index] = NULL; 
      
      insert(&m_RecoveredPkt, m_current[lostPktIndex], index);
      m_row_currentSize[lostPktIndex] ++;
      m_col_currentSize[index] ++;
      m_totalCurrentSize ++;
   
      m_UDT->receiveRecoveredData();
      
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate) 
         renewCurrentAndNext(false);
      else if (m_row_currentSize[lostPktIndex] == m_col_FECRate - 1 && m_RcvXORCurrent[lostPktIndex] != NULL) {
         //cout << " Current row recovery 2nd! " << lostPktIndex << endl << std::flush;
         lossRecovery(lostPktIndex, true);
      }
   }
   m_RecoveredTotal ++;
   //cout << "recovered! " << endl << std::flush;
}

void twoDXORFEC::lossReport(CPacket*** list, int32_t firstSeq, bool current)  //sending NAK
{
   int32_t first_loss_seq, second_loss_seq = -1;
   int* size; 
   if (current)
      size = m_row_currentSize;
   else
      size = m_row_nextSize;
   
   for (int i= 0; i < m_row_FECRate; ++ i) {
      if (size[i] < m_col_FECRate) {
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (list[i][j]->getLength() == 0) {
               if (current) {
                  cout << "Current: loss at (" << i << ", " << j << ")"  << endl << std::flush;
                  if (m_RcvXORCurrent[i] == NULL)
                     cout << "No XOR for row " << i << endl << std::flush;
                  if (m_RcvXORCurrent[m_row_FECRate+j] == NULL)
                     cout << "No XOR for col " << j << endl << std::flush;
               }
               else {
                  cout << "Next: loss at (" << i << ", " << j << ")"  << endl << std::flush;
                  if (m_RcvXORNext[i] == NULL)
                     cout << "No XOR for Next row " << i << endl << std::flush;
               }
               first_loss_seq = CSeqNo::incseq(firstSeq, i*m_col_FECRate+j);
               second_loss_seq = first_loss_seq;
               while (j+1 < m_col_FECRate && list[i][j+1]->getLength() == 0) {
                  cout << "loss at (" << i << ", " << j+1 << ")"  << endl << std::flush;
                  j = j+1;
                  second_loss_seq = CSeqNo::incseq(firstSeq, i*m_col_FECRate+j);
               }
               m_UDT->sendNAK(first_loss_seq, second_loss_seq);
               //cout << "FEC Loss Detection: " << first_loss_seq << " " << second_loss_seq << endl << std::flush;
            }
         }
      }
   }
}

void twoDXORFEC::sndRenewCurrentAndNext() {
   if (m_sndNextSize == 0) 
      m_sndCurrentSize = 0;
   else {
      for (int i= 0; i < m_row_FECRate; ++ i) {
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (m_sndNext[i][j]->getLength() > 0) {
                  insert(m_sndNext[i][j], m_sndCurrent[i], j);
                  m_sndCurrent[i][j]->setLength(m_sndNext[i][j]->getLength());
                  m_next[i][j]->setLength(0);
                  m_sndNextSize --;
                  /*if (m_sndNextSize == 0)
                     break;*/
            }
         }
         /*if (m_sndNextSize == 0)
            break;*/
      }
      m_sndCurrentSize = m_sndNextSize;
      m_sndNextSize = 0;
   }
}


void twoDXORFEC::renewCurrentAndNext(bool empty_next) {
   if (m_totalCurrentSize < m_row_FECRate*m_col_FECRate) {
      cout << "m_totalCurrentSize < m_row_FECRate*m_col_FECRate" << m_totalCurrentSize << endl << std::flush;
      m_notRecoveredTotal += m_row_FECRate*m_col_FECRate - m_totalCurrentSize;
   }
   if (empty_next) {
      
      for (int i= 0; i < m_row_FECRate; ++ i) {
         m_row_currentSize[i] = 0;
         m_row_nextSize[i] = 0;
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (i == 0) {
               m_col_currentSize[j] = 0;
               m_col_nextSize[j] = 0;
            }
            m_current[i][j]->setLength(0);
            m_next[i][j]->setLength(0);
         }
      }
      m_totalCurrentSize = 0;
      m_totalNextSize = 0;
         
      m_firstSeqCurrent = CSeqNo::incseq(m_firstSeqCurrent, 2*m_row_FECRate*m_col_FECRate);   
   }
   else {
      for (int i= 0; i < m_row_FECRate; ++ i) {
         m_row_currentSize[i] = m_row_nextSize[i];
         m_row_nextSize[i] = 0;
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (i == 0) {
               m_col_currentSize[j] = m_col_nextSize[j];
               m_col_nextSize[j] = 0;
            }
            if (m_next[i][j]->getLength() > 0) {
                  insert(m_next[i][j], m_current[i], j);
                  m_current[i][j]->setLength(m_next[i][j]->getLength());
                  m_next[i][j]->setLength(0);
            }
            else
               m_current[i][j]->setLength(0);
         }
      }
      m_totalCurrentSize = m_totalNextSize;
      m_firstSeqCurrent = m_firstSeqNext; 
      m_totalNextSize = 0;
   }
   
   m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_row_FECRate*m_col_FECRate);
   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i) {
      if (i<m_row_FECRate) {
         m_RcvXORCurrent[i] = m_RcvXORNext[i];
         m_RcvXORNext[i] = NULL;
      }
      else
         m_RcvXORCurrent[i] = NULL;
   }
}

void twoDXORFEC::lossDetection() 
{  
   cout << "next is full! Loss report on m_current!" << m_totalCurrentSize << endl << std::flush;
   //m_logFile << "next is full! Loss report on m_current!" << endl;
   //m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   renewCurrentAndNext(true); 
}

void twoDXORFEC::lossDetection(twoDXORPacket* XOR) 
{
   cout << "receiving col XOR for next!" << endl << std::flush;
   //m_logFile << "receiving XOR for after next! Loss report on both!" << endl;
   //m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   renewCurrentAndNext(false);
   
   int* indices = findIndexInsert(m_firstSeqCurrent, XOR->m_firstSeq);
   if (indices[0] >= 0) {
      if (XOR->m_row) {
         m_RcvXORCurrent[indices[0]] = XOR;
         //lossRecoveryInvestigation (indices[0], -1, true); 
      }
      else { 
         m_RcvXORCurrent[m_row_FECRate + indices[1]] = XOR;
         //lossRecoveryInvestigation (-1, indices[1], true);
      }
   }
}

void twoDXORFEC::lossDetection(const CPacket* packet)
{ // just recieved a packet out of m_current/m_next range
   cout << "receiving a packet out of current and next! Loss report on both!" << packet->m_iSeqNo << "  "  << m_firstSeqNext << endl << std::flush;
   //m_logFile << "receiving a packet out of current and next! Loss report on both!" << endl;
   //m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   lossReport(m_next, m_firstSeqNext, false);
   renewCurrentAndNext(true);
   onPktReceived(packet);
}

//
XORPacket::XORPacket(int maxPayloadSize):
m_firstSeq(),
m_XORHeader(),
m_payload_len(0),
m_payload(new char[maxPayloadSize])
{
}

void XORPacket::init(const CPacket* packet)
{   
   m_firstSeq = packet->m_iSeqNo;
   
   for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = packet->m_nHeader[i];
         
   m_payload_len = (uint32_t)packet->getLength();
   memcpy(m_payload, packet->m_pcData, packet->getLength());
   m_XORpayload_len = (uint32_t)packet->getLength();
}

void XORPacket::XOR(const CPacket* packet)
{  
   for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = m_XORHeader[i]^packet->m_nHeader[i];
         
   int XOR_len = std::min(packet->getLength(), (int)m_payload_len);

   if (packet->getLength() > m_payload_len){
      memcpy(m_payload+m_payload_len, packet->m_pcData+m_payload_len, packet->getLength()-m_payload_len);
      m_payload_len = (uint32_t)packet->getLength();  
   }
      
   for (int i = 0; i < XOR_len; ++ i)
      m_payload[i] = m_payload[i]^(packet->m_pcData[i]);
   
   m_XORpayload_len = m_XORpayload_len^((uint32_t)packet->getLength());
}

//
twoDXORPacket::twoDXORPacket(int maxPayloadSize):
m_firstSeq(),
m_XORHeader(),
m_payload_len(0),
m_row(true),
m_payload(new char[maxPayloadSize])
{
}

twoDXORPacket::~twoDXORPacket()
{
   delete [] m_payload;
}

void twoDXORPacket::headerXOR(const CPacket* packet, bool init)
{
   if (init) { 
      for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = packet->m_nHeader[i];
   }
   else { 
      for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = m_XORHeader[i]^packet->m_nHeader[i];
   }
}

void twoDXORPacket::init(const CPacket* packet, bool row)
{   
   m_firstSeq = packet->m_iSeqNo;
   m_row = row;
   
   for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = packet->m_nHeader[i];
   
   m_payload_len = (uint32_t)packet->getLength();
   memcpy(m_payload, packet->m_pcData, packet->getLength());
   m_XORpayload_len = (uint32_t)packet->getLength();
}

void twoDXORPacket::XOR(const CPacket* packet)
{
   for (int i = 0; i < 4; ++ i)
         m_XORHeader[i] = m_XORHeader[i]^packet->m_nHeader[i];

   int XOR_len = packet->getLength();

   if (packet->getLength() > m_payload_len) {
      memcpy(m_payload+m_payload_len, packet->m_pcData+m_payload_len, packet->getLength()-m_payload_len);
      XOR_len = m_payload_len;
      m_payload_len = (uint32_t)packet->getLength();
   }

   for (int i = 0; i < XOR_len; ++ i)
      m_payload[i] = m_payload[i]^(packet->m_pcData[i]);

   m_XORpayload_len = m_XORpayload_len^((uint32_t)packet->getLength());
}

// Nooshin Done.
