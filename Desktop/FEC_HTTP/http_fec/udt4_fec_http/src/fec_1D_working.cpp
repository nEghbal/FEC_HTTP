// Added by Nooshin

#include <iostream>
#include "fec.h"
#include "core.h"
#include <cstring>

using namespace std;
int current_xor_row = 0;
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
m_row_currentSize(NULL),
m_col_currentSize(NULL),
m_totalCurrentSize(0),
m_nextSize(0),
m_firstSeqCurrent(0),
m_rowSndXOR(NULL),
m_colSndXOR(NULL),
m_RcvXOR(NULL),
m_sndIndices(NULL),
m_RecoveredPkt()
{
}

twoDXORFEC::~twoDXORFEC()
{
   for (int i = 0; i < m_row_FECRate; ++ i) {
      for (int j = 0; j < m_col_FECRate; ++ j){
         if (i == 0) {
            if (m_next[j] !=NULL) {
               delete [] m_next[j]->m_pcData;
               delete m_next[j];
            }
         }
         if (m_current[i][j] !=NULL) {
            delete [] m_current[i][j]->m_pcData;
            delete m_current[i][j];
         }
      }
      if (m_current[i] !=NULL) 
      delete [] m_current[i];
   }
   
   delete [] m_current;
   delete [] m_next;
   
   delete [] m_row_currentSize;
   delete [] m_col_currentSize;
   delete [] m_RcvXOR;
   delete [] m_rowSndXOR;
   delete [] m_colSndXOR;
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
   m_next = new CPacket* [m_col_FECRate];
   m_colSndXOR = new twoDXORPacket*[m_col_FECRate];
   m_rowSndXOR = new twoDXORPacket*[m_row_FECRate];
   m_busyThreads = new bool[m_row_FECRate+m_col_FECRate];
   filethread = new pthread_t[m_row_FECRate+m_col_FECRate];
   thread_args = new XOR_thread_arg*[m_row_FECRate+m_col_FECRate];
   for (int i = 0; i < m_row_FECRate; ++ i) {
      m_row_currentSize[i] = 0;
      if (m_UDT != NULL)
         m_rowSndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);
      m_current[i] = new CPacket* [m_col_FECRate];
      for (int j = 0; j < m_col_FECRate; ++ j) {
         if (i == 0) {
            m_col_currentSize[j] = 0;
            m_colSndXOR[j] = NULL;

            m_next[j] = new CPacket();
            if (m_UDT != NULL) {
               m_colSndXOR[j] = new twoDXORPacket(m_UDT->m_iPayloadSize);
               m_next[j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            }
            m_next[j]->setLength(0);
         }
         m_current[i][j] = new CPacket();
         if (m_UDT != NULL)
            m_current[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
         m_current[i][j]->setLength(0);
      }
   }

   m_RcvXOR = new twoDXORPacket*[m_row_FECRate + m_col_FECRate];

   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i) {
      m_busyThreads[i] = false;
      m_RcvXOR[i] = NULL;
      thread_args[i] = new XOR_thread_arg();
   }

   m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_row_FECRate*m_col_FECRate);

   m_sndIndices = new int[2];
   m_sndIndices[0] = 0;
   m_sndIndices[1] = 0;

   m_logFile.open ("/home/eghbal/udt4_fec/logFile.txt");
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
         m_rowSndXOR[i] = new twoDXORPacket(m_UDT->m_iPayloadSize);

         for (int j = 0; j < m_col_FECRate; ++ j) { 
            if (i == 0) {
               m_colSndXOR[j] = new twoDXORPacket(m_UDT->m_iPayloadSize);
               m_next[j]->m_pcData = new char[m_UDT->m_iPayloadSize];
            }
            m_current[i][j]->m_pcData = new char[m_UDT->m_iPayloadSize];
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

   return indices;
}

int twoDXORFEC::findIndexInsert2(int32_t firstSeq, const CPacket* packet)
{
   int index = -1;

   if (firstSeq + m_col_FECRate - 1 <= CSeqNo::m_iMaxSeqNo &&
       packet->m_iSeqNo >= firstSeq &&
       packet->m_iSeqNo <= firstSeq + m_col_FECRate - 1)
      index = packet->m_iSeqNo - firstSeq;

   else if (firstSeq + m_col_FECRate - 1 > CSeqNo::m_iMaxSeqNo) {
      if (packet->m_iSeqNo >= firstSeq && packet->m_iSeqNo <= CSeqNo::m_iMaxSeqNo)
         index = packet->m_iSeqNo - firstSeq;
      else if (packet->m_iSeqNo <= (m_col_FECRate - 1 - (CSeqNo::m_iMaxSeqNo - firstSeq + 1)))
         index = (CSeqNo::m_iMaxSeqNo - firstSeq + 1) + packet->m_iSeqNo;
   }

   return index;
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

void* XORProcessing(void *input) 
{
   XOR_thread_arg *arg = (XOR_thread_arg *)input;
   for (int i = 0; i < arg->size; ++ i) {
      if (i == 0) 
         arg->XOR->init(arg->list[i], arg->row);
      else 
         arg->XOR->XOR(arg->list[i]);
      delete [] arg->list[i]->m_pcData;
      delete arg->list[i];
   }
   delete [] arg->list;
   arg->end = true;
 }

void twoDXORFEC::onPktSent(CPacket* packet)
{
   if (m_row_current == NULL)
      m_row_current = new CPacket*[m_col_FECRate];
   m_row_current[m_sndIndices[1]] = new CPacket();
   m_row_current[m_sndIndices[1]]->m_pcData = new char[m_UDT->m_iPayloadSize];
   insert(packet, m_row_current, m_sndIndices[1]);
   //cout << " Packet sent: " << packet->m_iSeqNo << endl << std::flush;
   if (m_sndIndices[1] == m_col_FECRate - 1) {
      if (m_busyThreads[m_sndIndices[0]] == false) { 
         thread_args[m_sndIndices[0]]->list = m_row_current;
         thread_args[m_sndIndices[0]]->size = m_col_FECRate;
         thread_args[m_sndIndices[0]]->row = true;
         thread_args[m_sndIndices[0]]->end = false;
         thread_args[m_sndIndices[0]]->XOR = m_rowSndXOR[m_sndIndices[0]];
         //XORProcessing(thread_args[m_sndIndices[0]]);
         m_busyThreads[m_sndIndices[0]] = true;
         //cout << " Packet sent: 2 " << packet->m_iSeqNo << endl << std::flush;

         pthread_create(&(filethread[m_sndIndices[0]]), NULL, XORProcessing, thread_args[m_sndIndices[0]]);
         pthread_detach(filethread[m_sndIndices[0]]);
      }
      else {
         // ???
      }
      m_row_current = NULL;
   }

   if(m_busyThreads[current_xor_row] == true) {      
      //cout << i <<"   " << thread_args[i]->iteration << endl << std::flush;
      if (thread_args[current_xor_row]->end == true) {
         //cout << current_xor_row << endl << std::flush;
         m_UDT->sendRedundantPkt(m_rowSndXOR[current_xor_row]);
         m_busyThreads[current_xor_row] = false;
         current_xor_row ++;
         if (current_xor_row == m_row_FECRate)
            current_xor_row = 0;
      }
   }

   /*if (m_col_current == NULL)
      m_col_current = new CPacket* [m_row_FECRate];
   m_col_current[m_sndIndices[0]] = new CPacket();
   m_col_current[m_sndIndices[0]]->m_pcData = new char[packet->getLength()];
   insert(packet, m_col_current, m_sndIndices[0]);
   if (m_sndIndices[0] == m_row_FECRate - 1) {
      MyLauncher2 launcher(this, (void*)m_col_current);
      #ifndef WIN32
      pthread_t filethread;
      pthread_create(&filethread, NULL, LaunchMemberFunction2, &launcher);
      pthread_detach(filethread);
      #else
      CreateThread(NULL, 0, LaunchMemberFunction2, &launcher, 0, NULL);
      #endif
      m_col_current = NULL;
   }*/
 
   //cout << " Packet sent: 2 " << endl << std::flush;
  
   /*if (m_sndIndices[1] == m_col_FECRate - 1)
      m_sndIndices[1] = 0;
   else
      m_sndIndices[1] ++;*/

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
   //cout << " Packet sent: 4 " << packet->m_iSeqNo << endl << std::flush;
}

void* twoDXORFEC::rowPktProcessing(void* list) //updating m_SndXOR
{
   cout << " rowProcessing: 1" << endl << std::flush;
   CPacket** current = (CPacket**)list;
   twoDXORPacket* XOR = new twoDXORPacket(m_UDT->m_iPayloadSize);
   for (int i = 0; i < m_col_FECRate; ++ i) {
      if (i == 0) 
         XOR->init(current[i], true);
      else 
         XOR->XOR(current[i]);
      delete [] current[i]->m_pcData;
      delete current[i];
   }
   delete [] current;  
   //m_UDT->sendRedundantPkt(XOR);
   //cout << " rowProcessing: 2" << endl << std::flush;
   return NULL;
}

void* twoDXORFEC::colPktProcessing(void* list) //updating m_SndXOR
{
   CPacket** current = (CPacket**)list;
   twoDXORPacket* XOR = new twoDXORPacket(m_UDT->m_iPayloadSize);
   for (int i = 0; i < m_row_FECRate; ++ i) {
      if (i == 0)
         XOR->init(current[i], false);
      else
         XOR->XOR(current[i]);
      delete [] current[i]->m_pcData;
      delete current[i];
   }
   delete [] current;
   m_UDT->sendRedundantPkt(XOR);
   return NULL;
}

void twoDXORFEC::onPktReceived(const CPacket* packet) //adding the packet to m_current/m_next and updating m_currentSize/m_nextSize
{
   //cout << " Packet receieved:" << packet->m_iSeqNo <<" " << m_UDT->m_iPayloadSize << endl << std::flush; 
   if (CSeqNo::seqcmp(packet->m_iSeqNo, m_firstSeqCurrent) < 0)
      return; // modify this to make sure all lost packet got received!
      
   int *indices;

   indices = findIndexInsert(m_firstSeqCurrent, packet->m_iSeqNo);
      
   if (indices[0] >= 0) {
      insert(packet, m_current[indices[0]], indices[1]);
      m_row_currentSize[indices[0]] ++;
      m_col_currentSize[indices[1]] ++;
      m_totalCurrentSize ++;
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate)
         renewCurrentAndNext(false);
      else //any recoverable packet?
         lossRecoveryInvestigation (indices[0], indices[1]);
      return;
   }                  

   int index = findIndexInsert2(m_firstSeqNext, packet);
   if (index >= 0) {
      insert(packet, m_next, index);
      m_nextSize ++;
      if (m_nextSize == m_col_FECRate)
         lossDetection(); 
      return;
   }
   
   //packet is out of m_current/m_next seq. range
   lossDetection(packet); 
   //cout << " Packet receieved: end! " << endl << std::flush;
}

void twoDXORFEC::lossRecoveryInvestigation (int row_index, int col_index) 
{
   if (row_index >=0 && m_row_currentSize[row_index] == m_col_FECRate - 1 && m_RcvXOR[row_index] != NULL &&
       ((row_index < m_row_FECRate - 1 && m_row_currentSize[row_index+1] > 0) || 
       (row_index == m_row_FECRate - 1 && m_nextSize > 0))) {
      cout << "row_index = " << row_index << endl << std::flush;
      lossRecovery(row_index, true);
   }
      
   if (col_index >=0 && m_col_currentSize[col_index] == m_row_FECRate - 1 &&  m_RcvXOR[m_row_FECRate+col_index] != NULL && m_nextSize > 0) {
         cout << "col_index = " << col_index << endl << std::flush;
         lossRecovery(col_index, false);
   }
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
   memcpy(newXOR->m_payload, XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t), 
          XOR->getLength()- (CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)));
   newXOR->m_payload_len = XOR->getLength()- (CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)+sizeof(bool));
   memcpy(&(newXOR->m_row), XOR->m_pcData+CPacket::m_iPktHdrSize+sizeof(uint32_t)+sizeof(int32_t)+newXOR->m_payload_len, sizeof(bool));

   if (CSeqNo::seqcmp(newXOR->m_firstSeq, m_firstSeqCurrent) < 0) {
     // cout << " :-O" << endl << std::flush;
      //delete [] newXOR->m_payload;
      //delete newXOR;
      // delete [] XOR.m_pcData; ???
      return;
   }

   //cout << " XOR receieved 1 "<<newXOR->m_firstSeq << endl << std::flush;
   
   if (CSeqNo::seqcmp(newXOR->m_firstSeq, CSeqNo::decseq(m_firstSeqNext)) > 0)
      lossDetection(newXOR);
   else if (CSeqNo::seqcmp(newXOR->m_firstSeq, m_firstSeqNext) < 0) {
      indices = findIndexInsert(m_firstSeqCurrent, newXOR->m_firstSeq);
      if (indices[0] >= 0) {
         if (newXOR->m_row) {
            m_RcvXOR[indices[0]] = newXOR;
            lossRecoveryInvestigation (indices[0], -1); 
         }
         else { 
            m_RcvXOR[m_row_FECRate + indices[1]] = newXOR;
            lossRecoveryInvestigation (-1, indices[1]);
         }
      }
   }
   //cout << " XOR receieved 2 "<<newXOR->m_firstSeq << endl << std::flush;                     
}

void twoDXORFEC::lossRecovery(int index, bool row)  
{  
   cout << "Recovery  "<< std::flush;
   int lostPktIndex = -1;
   if (row) {
      for (int j = 0; j < m_col_FECRate; ++ j) {
         if (m_current[index][j]->getLength() > 0)
            m_RcvXOR[index]->XOR(m_current[index][j]);
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXOR[index]->m_XORpayload_len];
      // copy m_RcvXOR content
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXOR[index]->m_XORHeader[i];
         //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
      }
      m_RecoveredPkt.m_pcData = m_RcvXOR[index]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXOR[index]->m_XORpayload_len); 
    
      m_RcvXOR[index] = NULL; 
      insert(&m_RecoveredPkt, m_current[index], lostPktIndex);
      m_row_currentSize[index] ++;
      m_col_currentSize[lostPktIndex] ++;
      m_totalCurrentSize ++;
   
      m_UDT->receiveRecoveredData();
      
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate)
         renewCurrentAndNext(false);
      else //any recoverable packet?
         lossRecoveryInvestigation (-1, lostPktIndex);
   }
   else {
      for (int j = 0; j < m_row_FECRate; ++ j) {
         if (m_current[j][index]->getLength() > 0)
            m_RcvXOR[m_row_FECRate+index]->XOR(m_current[j][index]);
         else 
            lostPktIndex = j;            
      }
    
      m_RecoveredPkt.m_pcData = new char[m_RcvXOR[m_row_FECRate+index]->m_XORpayload_len];
      // copy m_RcvXOR content
      for (int i = 0; i < 4; ++ i) {
         m_RecoveredPkt.m_nHeader[i] = m_RcvXOR[m_row_FECRate+index]->m_XORHeader[i];
         //cout << m_RecoveredPkt.m_nHeader[i] << " "<< std::flush;
      }
      m_RecoveredPkt.m_pcData = m_RcvXOR[m_row_FECRate+index]->m_payload;
      m_RecoveredPkt.setLength(m_RcvXOR[m_row_FECRate+index]->m_XORpayload_len); 
    
      m_RcvXOR[m_row_FECRate+index] = NULL; 
      
      insert(&m_RecoveredPkt, m_current[lostPktIndex], index);
      m_row_currentSize[lostPktIndex] ++;
      m_col_currentSize[index] ++;
      m_totalCurrentSize ++;
   
      m_UDT->receiveRecoveredData();
      
      if (m_totalCurrentSize == m_row_FECRate*m_col_FECRate) 
         renewCurrentAndNext(false);
      else //any recoverable packet?
         lossRecoveryInvestigation (lostPktIndex, -1);
   }
}

void twoDXORFEC::lossReport(CPacket*** list, int32_t firstSeq)  //sending NAK
{
   int32_t first_loss_seq, second_loss_seq = -1;
   //CUDT* u = CUDT::getUDTHandle(m_UDT);
   
   for (int i= 0; i < m_row_FECRate; ++ i) {
      if (m_row_currentSize[i] < m_col_FECRate) {
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
}

void twoDXORFEC::renewCurrentAndNext(bool empty_next) {
   if (empty_next) {
      
      for (int i= 0; i < m_row_FECRate; ++ i) {
         m_row_currentSize[i] = 0;
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (i == 0) {
               m_col_currentSize[j] = 0;
               m_next[j]->setLength(0);
            }
            m_current[i][j]->setLength(0);
         }
      }
      m_totalCurrentSize = 0;
      m_nextSize = 0;
         
      m_firstSeqCurrent = CSeqNo::incseq(m_firstSeqCurrent, 2*m_row_FECRate*m_col_FECRate);   
   }
   else {
      for (int i= 0; i < m_row_FECRate; ++ i) {
         if (i == 0)
            m_row_currentSize[0] = m_nextSize;
         else
            m_row_currentSize[i] = 0;
         for (int j= 0; j < m_col_FECRate; ++ j) {
            if (i == 0) {
               if (m_next[j]->getLength() > 0) {
                  m_col_currentSize[j] = 1;
                  insert(m_next[j], m_current[i], j);
                  m_next[j]->setLength(0);
               }
               else {
                  m_current[i][j]->setLength(0);
                  m_col_currentSize[j] = 0;
               }
            }
            else 
               m_current[i][j]->setLength(0);
         }
      }
      m_totalCurrentSize = m_nextSize;
      m_firstSeqCurrent = m_firstSeqNext; 
      m_nextSize = 0;
   }
   
   m_firstSeqNext = CSeqNo::incseq(m_firstSeqCurrent, m_row_FECRate*m_col_FECRate);
   for (int i = 0; i < m_row_FECRate + m_col_FECRate; ++ i) 
      m_RcvXOR[i] = NULL;
}

void twoDXORFEC::lossDetection() 
{  
   m_logFile << "next is full! Loss report on m_current!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent);
   renewCurrentAndNext(true); 
}

void twoDXORFEC::lossDetection(twoDXORPacket* XOR) 
{
   m_logFile << "receiving XOR for after next! Loss report on both!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent);
   renewCurrentAndNext(false);
   
   int* indices = findIndexInsert(m_firstSeqCurrent, XOR->m_firstSeq);
   if (indices[0] >= 0) {
      if (XOR->m_row) {
         m_RcvXOR[indices[0]] = XOR;
         lossRecoveryInvestigation (indices[0], -1); 
      }
      else { 
         m_RcvXOR[m_row_FECRate + indices[1]] = XOR;
         lossRecoveryInvestigation (-1, indices[1]);
      }
   }
}

void twoDXORFEC::lossDetection(const CPacket* packet)
{ // just recieved a packet out of m_current/m_next range
   m_logFile << "receiving a packet out of current and next! Loss report on both!" << endl;
   m_logFile.flush();
   lossReport(m_current, m_firstSeqCurrent);
   renewCurrentAndNext(false);
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
