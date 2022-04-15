#!/bin/bash

FILE=$1
PEER=$2

# RTT: 10 ms 40 ms 70 ms 100 ms 130 ms

#for rtt in 5ms 20ms 35ms 50ms


#do

#sh /home/eghbal/PDS-master/code/netem.sh $rtt 0.1%
#ssh -t $PEER sh /home/eghbal/PDS-master/code/netem.sh $rtt 0.1%

#echo $rtt >> $FILE

#rm  "/dev/shm/eghbal/14G.blob"

#(time -p sh /home/eghbal/udt4/app/script_rec.sh $PEER /dev/shm/eghbal/14G.blob) 2>> $FILE

#for j in 8 16 32 64 128
#   do
      for i in 1
        do
    

#       ssh $PEER rm -rf "/dev/shm/eghbal/14G.blob"

#       (time -p rsync  --rsh="/home/eghbal/PDS-master/code/PDS_client.out 8 10000 TCP"  /dev/shm/eghbal/14G.blob  $PEER:/dev/shm/eghbal/14G.blob) 2>> $FILE

#        ssh $PEER rm -rf "/dev/shm/eghbal/14G.blob"

 #       (time -p rsync  --rsh="/home/eghbal/PDS-master/code/PDS_client.out 8 4000 SSH"  /dev/shm/eghbal/14G.blob  $PEER:/dev/shm/eghbal/14G.blob) 2>> $FILE

#       ssh $PEER rm -rf "/dev/shm/eghbal/14G.blob"

#       (time -p rsync /dev/shm/eghbal/14G.blob  $PEER:/dev/shm/eghbal/14G.blob) 2>> $FILE

       # (iperf -c 192.168.74.43 -n 14336M) >> $FILE

#       ssh $PEER rm -rf "/dev/shm/eghbal/14G.blob"

      # (time -p globus-url-copy -p $j -fast -g2 file:///dev/shm/eghbal/5G.blob ftp://eghbal:123456@$PEER:2811/dev/shm/eghbal/5G.blob) 2>> $FILE
      (time -p ./recvfile 192.168.154.31 9000 /dev/shm/eghbal/5G.blob /dev/shm/eghbal/5G.blob) 2>> $FILE
    done
#done
