// Added by Nooshin
#ifndef fec
#define fec

#include "udt.h"
#include "packet.h"
#include "common.h"
//#include "core.h"
#include <math.h>
#include <iostream>
#include <fstream>
using std::ofstream;
using std::ifstream;

class CUDT;

class XORPacket
{

public:
   XORPacket(int maxPayloadSize);
   void init(const CPacket* packet);
   void XOR(const CPacket* packet);

public:
   int32_t m_firstSeq;
   uint32_t m_XORHeader[4];               // The 128-bit header field
   uint32_t m_XORpayload_len;
   char* m_payload;
   uint32_t m_payload_len;
};

class twoDXORPacket
{

public:
   twoDXORPacket(int maxPayloadSize);
   ~twoDXORPacket();
   void init(const CPacket* packet, bool row);
   void XOR(const CPacket* packet);
   void headerXOR(const CPacket* packet, bool init);

public:
   int32_t m_firstSeq;
   uint32_t m_XORHeader[4];               // The 128-bit header field
   uint32_t m_XORpayload_len;
   char* m_payload;
   uint32_t m_payload_len;
   bool m_row;         // true if the XOR is for a row, false if it is for a column
};

class CFEC
{
friend class CUDT;
public:
   CFEC();

public:
   virtual void onPktSent(const CPacket*) {}
   virtual void onPktReceived(const CPacket*) {}
   virtual void processCustomMsg(const CPacket*) {}
   virtual int lossRecovery() {}
   virtual void lossDetection() {}
   virtual void lossDetection(CPacket*) {}
   virtual void lossReport(CPacket**, int32_t) {}
   virtual void renewCurrentAndNext(bool) {}
   void setPayloadSize(int);
   ofstream m_logFile;
   
protected:
   void init() {}
   void writeToLogFile(const CPacket*);

protected:
   //UDTSOCKET m_UDT;                   // The UDT entity that this FEC algorithm is bound to
   CUDT* m_UDT;
   int m_iPayloadSize;
   int32_t m_prevLostSeq;
};

class XORFEC: public CFEC
{

public:
   XORFEC();
   virtual ~XORFEC();

public:

   XORPacket* m_SndXOR;                 // the XOR packet from m_FECRate sent packets
   XORPacket* m_RcvXOR;                 // the XOR packet that has been recieved for loss recovery
   CPacket m_RecoveredPkt;             // the recovered packet

public:
   void init();
   void insert(const CPacket* packet, CPacket** list, int index);
   int findIndexInsert(int32_t firstSeq, const CPacket* packet);
   void setRate(int);
   int getRate();
   virtual void onPktSent(const CPacket* packet);
   virtual void onPktReceived(const CPacket* packet);
   virtual void processCustomMsg(const CPacket* packet);
   virtual int lossRecovery();
   virtual void lossDetection();
   virtual void lossDetection(const CPacket* packet);
   virtual void lossReport(CPacket** list, int32_t firstSeq);
   virtual void renewCurrentAndNext(bool empty_next);

protected:

   int m_FECRate;                     // The rate of sending XOR packets
   CPacket** m_current;               // Array of current FEC window
   int m_currentSize;                 // The current size of m_current
   CPacket** m_next;                  // Array of next FEC window
   int m_nextSize;                    // The current size of m_next
   int m_FECIteration;                // FEC iteration at sender
   int32_t m_firstSeqCurrent;         // First Seq. number of m_current at reciever
   int32_t m_firstSeqNext;            // First Seq. number of m_next at reciever
};

class XOR_thread_arg {

public:
   CPacket*** list;
   int size;
   twoDXORPacket* XOR;
   int index;
   bool row;
   bool end;
};

class twoDXORFEC: public CFEC
{

public:
   twoDXORFEC();
   virtual ~twoDXORFEC();

public:

   twoDXORPacket** m_SndXOR;                 // the XOR packet from m_FECRate sent packets
   XOR_thread_arg** thread_args;
   twoDXORPacket** m_RcvXORCurrent;                 // the list of XOR packets that has been recieved for loss recovery
   twoDXORPacket* m_RcvXORTemp;
   twoDXORPacket** m_RcvXORNext;
   CPacket m_RecoveredPkt;             // the recovered packet
   bool* m_busyThreads;
   pthread_t* filethread;
   uint64_t m_RecoveredTotal;
   uint64_t m_notRecoveredTotal;  

public:
   void init();
   void init2();
   void setRowRate(int);
   void setColRate(int);
   int getRate(bool row);
   void insert(const CPacket* packet, CPacket** list, int index);
   void rowXORProcessing(int index);
   void colXORProcessing(int index);
   int* findIndexInsert(int32_t firstSeq, int32_t seqNo);
   virtual void onPktSent(CPacket* packet);
   void onPktSent_write(CPacket* packet);
   void onPktSent_read(CPacket* packet);
   virtual void onPktReceived(const CPacket* packet);
   virtual void processCustomMsg(const CPacket* packet);
   virtual void lossRecovery(int index, bool row);
   void lossRecoveryNext(int index);
   virtual void lossDetection();
   virtual void lossDetection(twoDXORPacket* XOR);
   virtual void lossDetection(const CPacket* packet);
   virtual void lossReport(CPacket*** list, int32_t firstSeq, bool current);
   virtual void lossRecoveryInvestigation (int row_index, int col_index, bool current);
   virtual void renewCurrentAndNext(bool empty_next);
   void sndRenewCurrentAndNext();
   void writeRedundantPktFile(twoDXORPacket* m_SndXOR);

protected:

   int m_row_FECRate;                     // The rate of sending XOR packets
   int m_col_FECRate;                     // The rate of sending XOR packets
   int *m_sndIndices;
   CPacket*** m_current;               // Array of current FEC window
   int* m_row_currentSize;                 // The current size of m_current
   int* m_col_currentSize;
   int m_totalCurrentSize;
   CPacket*** m_sndCurrent;
   int m_sndCurrentSize;
   CPacket*** m_next;                 // Array of next FEC window
   CPacket*** m_sndNext;
   int m_sndNextSize;
   int* m_row_nextSize;
   int* m_col_nextSize;
   int m_totalNextSize;                    // The current size of m_next
   int32_t m_firstSeqCurrent;         // First Seq. number of m_current at reciever
   int32_t m_firstSeqNext;            // First Seq. number of m_next at reciever
   ofstream m_XORFileWrite;
   ifstream m_XORFileRead;
};
#endif
// Nooshin done.
