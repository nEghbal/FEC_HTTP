// Added by Nooshin

#include <iostream>
#include "fec.h"
#include "core.h"
#include <cstring>

using namespace std;
int current_xor_row = 0;
int current_xor_col = 0;
int buffer_factor = 10;
//
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

   //m_logFile.open ("/home/eghbal/udt4_fec/logFile.txt");
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
   //cout << " Packet sent:" << packet->m_iSeqNo << endl << std::flush;
  
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
               //m_logFile << "Gap:" << j + m_firstSeqCurrent - m_prevLostSeq << endl;
               //m_logFile.flush();
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
               //m_logFile << "Gap:" << j - m_prevLostSeq << endl;
               //m_logFile.flush();
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
      //m_logFile << "next is full! Loss report on m_current!" << endl;
      //m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      renewCurrentAndNext(false);
      m_RcvXOR = NULL; 
   }
   else if (m_RcvXOR->m_firstSeq == m_firstSeqNext) {
      //m_logFile << "receiving next XOR! Loss report on m_current!" << endl;
      //m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      renewCurrentAndNext(false);
   }
   else if (CSeqNo::seqcmp(m_RcvXOR->m_firstSeq, m_firstSeqNext) > 0) {
      //m_logFile << "receiving XOR for after next! Loss report on both!" << endl;
      //m_logFile.flush();
      lossReport(m_current, m_firstSeqCurrent);
      lossReport(m_next, m_firstSeqNext);
      renewCurrentAndNext(true);
   }
}

void XORFEC::lossDetection(const CPacket* packet)
{ // just recieved a packet out of m_current/m_next range
   //m_logFile << "receiving a packet out of current and next! Loss report on both!" << endl;
   //m_logFile.flush();
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
m_buffer(NULL),
m_sndCurrent(NULL),
m_sndNext(NULL),
m_bufferSize(0),
m_sndNextSize(0),
m_sndCurrentSize(0),
m_SndXOR(NULL),
m_RcvXOR(NULL),
m_sndIndices(NULL),
m_RecoveredPkt(),
m_firstSeq(NULL),
m_current_buffer(0)
{
}

twoDXORFEC::~twoDXORFEC()
{
   for (int i = 0; i < m_row_FECRate; ++ i) {
      for (int j = 0; j < m_col_FECRate; ++ j){
         if (m_sndCurrent[i][j] !=NULL) {
            delete [] m_sndCurrent[i][j]->m_pcData;
            delete m_sndCurrent[i][j];
         }
         if (m_sndNext[i][j] !=NULL) {
            delete [] m_sndNext[i][j]->m_pcData;
            delete m_sndNext[i][j];
         }
      }
      if (m_sndCurrent[i] !=NULL)
         delete [] m_sndCurrent[i];
      if (m_sndNext[i] !=NULL)
         delete [] m_sndNext[i];
   }

   for (int i = 0; i < buffer_factor*m_row_FECRate; ++ i) {
      for (int j = 0; j < m_col_FECRate; ++ j)
         if (m_buffer[i][j] !=NULL) {
            delete [] m_buffer[i][j]->m_pcData;
            delete m_buffer[i][j];
         }
      if (m_buffer[i] !=NULL)
         delete [] m_buffer[i];
   }
   
   delete [] m_buffer;
   delete [] m_sndCurrent;
   delete [] m_sndNext;
   
   delete [] m_RcvXOR;
   delete [] m_SndXOR;
   delete [] m_sndIndices;
   delete [] thread_args;
   delete [] m_busyThreads;
}

void twoDXORFEC::init2()
{
   m_bufferRowNum = buffer_factor*m_row_FECRate;
   m_buffer = new CPacket** [m_bufferRowNum];
   m_firstSeq = new int32_t [buffer_factor];
   m_sndCurrent = new CPacket** [m_row_FECRate];
   m_sndNext = new CPacket** [m_row_FECRate];
   m_SndXOR = new twoDXORPacket*[m_col_FECRate+m_row_FECRate];
   m_busyThreads = new bool[m_row_FECRate+m_col_FECRate];
   filethread = new pthread_t[m_row_FECRate+m_col_FECRate];
   thread_args = new XOR_thread_arg*[m_row_FECRate+m_col_FECRate];
   
   if (m_UDT != NULL) {
      m_firstSeq[0] = m_UDT->m_iPeerISN;
      for (int i = 1; i < buffer_factor; ++ i)
         m_firstSeq[i] = CSeqNo::incseq(m_firstSeq[i-1], m_row_FECRate*m_col_FECRate);
   }
   else
      m_firstSeq[0] = NULL;

   for (int i = 0; i < m_bufferRowNum; ++ i) {
      m_buffer[i] = new CPacket* [m_col_FECRate];
      for (int j = 0; j < m_col_FECRate; ++ j) {
         m_buffer[i][j] = new CPacket();
         if (m_UDT != NULL) 
            m_buffer[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         m_buffer[i][j]->setLength(0);
      }
   }

   for (int i = 0; i < m_row_FECRate; ++ i) {
      m_sndCurrent[i] = new CPacket* [m_col_FECRate];
      m_sndNext[i] = new CPacket* [m_col_FECRate];
      for (int j = 0; j < m_col_FECRate; ++ j) {
         m_sndCurrent[i][j] = new CPacket();
         m_sndNext[i][j] = new CPacket();
         if (m_UDT != NULL) {
            m_sndCurrent[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndNext[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         }
         m_sndCurrent[i][j]->setLength(0);
         m_sndNext[i][j]->setLength(0);
      }
   }

   m_RcvXOR = new twoDXORPacket**[buffer_factor];
   for (int i = 0; i < buffer_factor; ++ i) {
      m_RcvXOR[i] = new twoDXORPacket*[m_row_FECRate + m_col_FECRate];
      for (int j = 0; j < m_row_FECRate + m_col_FECRate; ++ j)
         m_RcvXOR[i][j] = NULL;
   }

   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i) {
      m_busyThreads[i] = false;
      thread_args[i] = new XOR_thread_arg();
      if (m_UDT != NULL)
         m_SndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);
   }

   m_sndIndices = new int[2];
   m_sndIndices[0] = 0;
   m_sndIndices[1] = 0;

   m_bufferSize = 0;
   m_current_buffer = 0;
   m_sndCurrentSize = 0;
   m_sndNextSize = 0;

   //m_logFile.open ("/home/eghbal/udt4_fec/logFile.txt");
   m_XORFile.open ("/dev/shm/eghbal/XOR.txt");
}

void twoDXORFEC::setRowRate(int row_FECRate)
{     
   m_row_FECRate = row_FECRate;

   if (m_col_FECRate > 0)
      init2();
}

void twoDXORFEC::setColRate(int col_FECRate)
{
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
   
   if (m_row_FECRate != -1) {
      if (m_firstSeq[0] == NULL) {
         m_firstSeq[0] = m_UDT->m_iPeerISN;
         for (int i = 1; i < buffer_factor; ++ i)
            m_firstSeq[i] = CSeqNo::incseq(m_firstSeq[i-1], m_row_FECRate*m_col_FECRate);
      }

      for (int i = 0; i < m_bufferRowNum; ++ i) 
         for (int j = 0; j < m_col_FECRate; ++ j) 
            m_buffer[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];

      for (int i = 0; i < m_row_FECRate; ++ i) {
         m_SndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);

         for (int j = 0; j < m_col_FECRate; ++ j) { 
            if (i == 0) 
               m_SndXOR[m_row_FECRate+j] = new twoDXORPacket(m_UDT->m_iPayloadSize);
            m_sndCurrent[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            m_sndNext[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         }
      }
   }
}

int* twoDXORFEC::findIndexInsert(int32_t seqNo)
{
   int *indices = new int [2];
   indices[0] = -1;
   indices[1] = -1;
   int firstSeq = -1;
   int i = m_current_buffer;

   //cout << " before while! " << endl << std::flush;
   while (firstSeq < 0 && i >= 0) {
      //cout << "i: " << i << "   " << "seq: " << seqNo << "  " << CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) << endl << std::flush;
      if (CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) < 0 && i == 0 &&
          (m_bufferSize < buffer_factor*m_row_FECRate*m_col_FECRate ||
             CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[buffer_factor-1], m_row_FECRate*m_col_FECRate)) >= 0)) {
         firstSeq = m_firstSeq[0];
         indices[0] = 0;
         indices[1] = 0;
      }

      else if (CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) < 0 &&
               i == 0 && CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[buffer_factor-1], m_row_FECRate*m_col_FECRate)) < 0) {
         i = buffer_factor-1;
      }

      else if (CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) < 0 &&
               CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i-1], m_row_FECRate*m_col_FECRate)) >= 0) {
         firstSeq = m_firstSeq[i];
         indices[0] = i*m_row_FECRate;
         indices[1] = 0;
      }

      else if (CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) < 0 &&
               CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i-1], m_row_FECRate*m_col_FECRate)) < 0) {
         i = i - 1;
      }
     
      else if (i == buffer_factor-1 && CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) >= 0) {
         i = 0;  
         m_current_buffer = i;
         renewCurrentAndNext();
      }
    
      else if (CSeqNo::seqcmp(seqNo, CSeqNo::incseq(m_firstSeq[i], m_row_FECRate*m_col_FECRate)) >= 0) {
         i = i + 1;
         m_current_buffer = i;
         renewCurrentAndNext();
      }
   }

   if (firstSeq + m_row_FECRate*m_col_FECRate - 1 <= CSeqNo::m_iMaxSeqNo &&
       seqNo >= firstSeq &&  
       seqNo <= firstSeq + m_row_FECRate*m_col_FECRate - 1) {
             
      indices[0] += (seqNo - firstSeq)/m_col_FECRate;
      indices[1] = (seqNo - firstSeq)%m_col_FECRate;
   }
   
   else if (firstSeq + m_row_FECRate*m_col_FECRate - 1 > CSeqNo::m_iMaxSeqNo) {
      if (seqNo >= firstSeq) { 
         indices[0] += (seqNo - firstSeq)/m_col_FECRate;
         indices[1] = (seqNo - firstSeq)%m_col_FECRate;  
      }
      else if (seqNo <= (m_row_FECRate*m_col_FECRate - 1 - (CSeqNo::m_iMaxSeqNo - firstSeq + 1))) {
         indices[0] += ((CSeqNo::m_iMaxSeqNo - firstSeq + 1) + seqNo)/m_col_FECRate;
         indices[1] = ((CSeqNo::m_iMaxSeqNo - firstSeq + 1) + seqNo)%m_col_FECRate;
      }
   }
   
   //cout << "i: " << i <<"   firstSeq: "<< firstSeq << " seq: " << seqNo << endl << std::flush; 
   //cout << indices[0] << "  " << indices[1]  << endl << std::flush;
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
   m_XORFile << XOR_len;
   cout << XOR_len << endl << std::flush;
   m_XORFile.write(XOR, XOR_len);
   m_XORFile.flush();
}

void twoDXORFEC::onPktSent(CPacket* packet)
{
   //cout << packet->m_iSeqNo << endl << std::flush;
   if (m_sndCurrentSize < m_col_FECRate*m_row_FECRate) {
      insert(packet, m_sndCurrent[m_sndIndices[0]], m_sndIndices[1]);
      m_sndCurrentSize ++;
      if (m_sndIndices[1] == m_col_FECRate - 1) 
         rowXORProcessing(m_sndIndices[0]); // send an XOR
   }
   else if (m_sndNextSize < m_col_FECRate*m_row_FECRate){
      insert(packet, m_sndNext[m_sndIndices[0]], m_sndIndices[1]);
      m_sndNextSize ++;
      if (m_sndNextSize == m_col_FECRate*m_row_FECRate) {
         cout << "Next is full!    "<< current_xor_row << "   " <<current_xor_col << endl << std::flush;
         exit(0);
      }
   }

   if (m_busyThreads[current_xor_row] == true && thread_args[current_xor_row]->end == true) {
      //m_UDT->sendRedundantPkt(m_SndXOR[current_xor_row]);
      //cout << "row: " << current_xor_row << endl << std::flush;
      writeRedundantPktFile(m_SndXOR[current_xor_row]);
      m_busyThreads[current_xor_row] = false;
      current_xor_row ++;
      if (current_xor_row == m_row_FECRate) {
         current_xor_row = 0;
         for (int i = 0; i < m_col_FECRate; ++ i) 
            colXORProcessing(i); // send an XOR
      }
   }

   while (m_busyThreads[m_row_FECRate+current_xor_col] == true && thread_args[m_row_FECRate+current_xor_col]->end == true) {
      //m_UDT->sendRedundantPkt(m_SndXOR[m_row_FECRate+current_xor_col]);
      //cout << "col: " << current_xor_col << endl << std::flush;
      writeRedundantPktFile(m_SndXOR[m_row_FECRate+current_xor_col]);
      m_busyThreads[m_row_FECRate+current_xor_col] = false;
      current_xor_col ++;
      if (current_xor_col == m_col_FECRate) {
         current_xor_col = 0;
         if (current_xor_row == 0) {
            cout << "sndRenewCurrentAndNext(). Next size:  "<< m_sndNextSize << endl << std::flush;
            sndRenewCurrentAndNext();
            if (m_sndCurrentSize > 0) {
               for (int i = 0; i < m_sndIndices[0] ; ++ i)
                  rowXORProcessing(i);
               if (m_sndIndices[1] == m_col_FECRate - 1) 
                  rowXORProcessing(m_sndIndices[0]);
            }
         }
      }
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
   int *indices;

   indices = findIndexInsert(packet->m_iSeqNo);
      
   if (indices[0] >= 0) {
      insert(packet, m_buffer[indices[0]], indices[1]);
      m_bufferSize ++;
      //lossRecoveryInvestigation (indices[0], indices[1], true);
   }                  
}

void twoDXORFEC::lossRecoveryInvestigation (int row_index, int col_index, bool row) 
{
   int row1_size = 0;
   int col1_size = 0;
   int row2_size = 0;
   int col2_size = 0;
  //cout << "lossRecoveryInvestigation 1   " << row_index << "   " << col_index << endl << std::flush; 

   if (row) {
      for (int i = 0; i < m_col_FECRate; ++ i) {
         if (m_buffer[row_index][i]->getLength() > 0)
            row1_size ++;
         /*if (row_index < m_row_FECRate*buffer_factor - 1) {
            if (m_buffer[row_index+1][i]->getLength() > 0)
               row2_size ++;
         }
         else if (row_index == m_row_FECRate*buffer_factor - 1 && m_current_buffer != buffer_factor - 1) {
            if (m_buffer[0][i]->getLength() > 0)
               row2_size ++;
         }*/
      }
      if (row1_size == m_col_FECRate - 1 && m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate] != NULL/* && row2_size > 0*/) {
         cout << "row recovery" << endl << std::flush;
         lossRecovery(row_index, col_index, true);
         //cout << "after recovery" << endl << std::flush;
      }
   }

   //cout << "lossRecoveryInvestigation 2" << endl << std::flush;

   if (col_index > -1) {
      for (int i = (row_index/m_row_FECRate)*m_row_FECRate; i < (row_index/m_row_FECRate + 1)*m_row_FECRate; ++ i) {
         if (m_buffer[i][col_index]->getLength() > 0)
            col1_size ++;
      }
      if (col1_size == m_row_FECRate - 1 && m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index] != NULL) {
         cout << "col recovery" << endl << std::flush;
         //lossRecovery(row_index, col_index, false);
      }
   }

   //cout << "lossRecoveryInvestigation 3" << endl << std::flush;
}

void twoDXORFEC::processCustomMsg(const CPacket* XOR) //updating m_RcvXOR
{   
   //cout << " XOR receieved" << endl << std::flush;
   int* indices;
   
   //build a XORPacket from XOR
   twoDXORPacket* newXOR = new twoDXORPacket(m_UDT->m_iPayloadSize);
   memcpy(newXOR->m_XORHeader, XOR->m_pcData, CPacket::m_iPktHdrSize);
   //cout << " XOR after header" << endl << std::flush;
   memcpy(&(newXOR->m_XORpayload_len), XOR->m_pcData+CPacket::m_iPktHdrSize, sizeof(uint32_t));
   newXOR->m_XORpayload_len = (uint32_t)newXOR->m_XORpayload_len;
   memcpy(&(newXOR->m_firstSeq), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t), sizeof(int32_t));
   newXOR->m_firstSeq = (int32_t)newXOR->m_firstSeq;
   memcpy(&(newXOR->m_row), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t), sizeof(bool));
   newXOR->m_row = (bool)newXOR->m_row;   
   newXOR->m_payload_len = XOR->getLength()- (CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)+sizeof(bool));
   memcpy(newXOR->m_payload, XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)+sizeof(bool), newXOR->m_payload_len);

   //cout << "row: "<< newXOR->m_row << "  XOR: " << newXOR->m_firstSeq << "  current: " << m_firstSeqCurrent << "  next: " << m_firstSeqNext << endl << std::flush;

   indices = findIndexInsert(newXOR->m_firstSeq);
   
   if (indices[0] > -1) {
      if (newXOR->m_row) {
         m_RcvXOR[indices[0]/m_row_FECRate][indices[0]] = newXOR;
         lossRecoveryInvestigation (indices[0], -1, true); 
      }
      else { 
         m_RcvXOR[indices[0]/m_row_FECRate][m_row_FECRate + indices[1]] = newXOR;
         lossRecoveryInvestigation (indices[0], indices[1], false);
      }
   }              
}

void twoDXORFEC::lossRecovery(int row_index, int col_index, bool row)  
{  
   int lostPktIndex = -1;
   if (row) {
      for (int j = 0; j < m_col_FECRate; ++ j) {
         if (m_buffer[row_index][j]->getLength() > 0)
            m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate]->XOR(m_buffer[row_index][j]);
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate]->m_XORpayload_len];
      /* copy m_RcvXOR content */
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate]->m_XORHeader[i];
         /* cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush; */
      }
      m_RecoveredPkt.m_pcData = m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate]->m_XORpayload_len); 
    
      m_RcvXOR[row_index/m_row_FECRate][row_index%m_row_FECRate] = NULL; 
      insert(&m_RecoveredPkt, m_buffer[row_index], lostPktIndex);
      m_bufferSize ++;
   
      m_UDT->receiveRecoveredData();
      
      lossRecoveryInvestigation (row_index, lostPktIndex, false);
   }
   else {
      for (int j = (row_index/m_row_FECRate)*m_row_FECRate; j < (row_index/m_row_FECRate + 1)*m_row_FECRate; ++ j) {
         if (m_buffer[j][col_index]->getLength() > 0)
            m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index]->XOR(m_buffer[j][col_index]);
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index]->m_XORpayload_len];
      /* copy m_RcvXOR content */
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index]->m_XORHeader[i];
         /* cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush; */
      }
      m_RecoveredPkt.m_pcData = m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index]->m_XORpayload_len); 
    
      m_RcvXOR[row_index/m_row_FECRate][m_row_FECRate+col_index] = NULL; 
      
      insert(&m_RecoveredPkt, m_buffer[lostPktIndex], col_index);
      m_bufferSize ++;
   
      m_UDT->receiveRecoveredData();
      
      lossRecoveryInvestigation (lostPktIndex, -1, true);
   }
}

/*void twoDXORFEC::lossReport(CPacket*** list, int32_t firstSeq, bool current)  //sending NAK
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
               first_loss_seq = CSeqNo::incseq(firstSeq, i);
               second_loss_seq = first_loss_seq;
               while (i+1 < m_row_FECRate && list[i+1][j]->getLength() == 0) {
                  i = i+1;
                  second_loss_seq = CSeqNo::incseq(firstSeq, i);
               }
               m_UDT->sendNAK(first_loss_seq, second_loss_seq);
               //cout << "FEC Loss Detection: " << first_loss_seq << " " << second_loss_seq << endl << std::flush;
            }
         }
      }
   }
}*/

void twoDXORFEC::sndRenewCurrentAndNext() {
   if (m_sndNextSize == 0) 
      m_sndCurrentSize = 0;
   else {
      for (int i= 0; i < m_row_FECRate; ++ i) {
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (m_sndNext[i][j]->getLength() > 0) {
                  insert(m_sndNext[i][j], m_sndCurrent[i], j);
                  m_sndCurrent[i][j]->setLength(m_sndNext[i][j]->getLength());
                  m_sndNext[i][j]->setLength(0);
                 // m_sndNextSize --;
                 // if (m_sndNextSize == 0)
                 //    break;
            }
         }
         //if (m_sndNextSize == 0)
         //   break;
      }
      m_sndCurrentSize = m_sndNextSize;
      m_sndNextSize = 0;
   }
}

void twoDXORFEC::renewCurrentAndNext() {
   
   for (int i= m_current_buffer*m_row_FECRate; i < (m_current_buffer+1)*m_row_FECRate; ++ i) 
         for (int j= 0; j < m_col_FECRate; ++ j)
            if (m_buffer[i][j]->getLength() > 0) {
               m_buffer[i][j]->setLength(0);
               m_bufferSize --;
            }
         
   if (m_current_buffer > 0)
      m_firstSeq[m_current_buffer] = CSeqNo::incseq(m_firstSeq[m_current_buffer-1], m_row_FECRate*m_col_FECRate);
   else   
      m_firstSeq[0] = CSeqNo::incseq(m_firstSeq[buffer_factor-1], m_row_FECRate*m_col_FECRate);

   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i)
      m_RcvXOR[m_current_buffer][i] = NULL;
}

/*void twoDXORFEC::lossDetection() 
{  
   cout << "next is full! Loss report on m_current!" << endl << std::flush;
   m_logFile << "next is full! Loss report on m_current!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   renewCurrentAndNext(true); 
}

void twoDXORFEC::lossDetection(twoDXORPacket* XOR) 
{
   cout << "receiving XOR for after next! Loss report on both!" << endl << std::flush;
   m_logFile << "receiving XOR for after next! Loss report on both!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   renewCurrentAndNext(false);
   
   int* indices = findIndexInsert(m_firstSeqCurrent, XOR->m_firstSeq);
   if (indices[0] >= 0) {
      if (XOR->m_row) {
         m_RcvXORCurrent[indices[0]] = XOR;
         lossRecoveryInvestigation (indices[0], -1, true); 
      }
      else { 
         m_RcvXORCurrent[m_row_FECRate + indices[1]] = XOR;
         lossRecoveryInvestigation (-1, indices[1], true);
      }
   }
}

void twoDXORFEC::lossDetection(const CPacket* packet)
{ // just recieved a packet out of m_current/m_next range
   m_logFile << "receiving a packet out of current and next! Loss report on both!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent, true);
   lossReport(m_next, m_firstSeqNext, false);
   renewCurrentAndNext(true);
   onPktReceived(packet);
}*/

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

   int XOR_len = std::min(packet->getLength(), (int)m_payload_len);

   if (packet->getLength() > m_payload_len){
      memcpy(m_payload+m_payload_len, packet->m_pcData+m_payload_len, packet->getLength()-m_payload_len);
      m_payload_len = (uint32_t)packet->getLength();
   }

   for (int i = 0; i < XOR_len; ++ i)
      m_payload[i] = m_payload[i]^(packet->m_pcData[i]);

   m_XORpayload_len = m_XORpayload_len^((uint32_t)packet->getLength());
}

// Nooshin Done.
