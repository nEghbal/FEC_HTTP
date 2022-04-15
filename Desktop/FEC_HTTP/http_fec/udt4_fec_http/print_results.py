import sys
import math

file_obj = open (str(sys.argv[1]))

lines = file_obj.readlines()

real_times = []
for i in range(5):
    #print lines[int(sys.argv[2+i])].split()[1]
    real_times.append(lines[int(sys.argv[2+i])].split()[1])

avg = 0
throughputs = []
for j in range(5):
    #print str(real_times[i])
    avg += round(40960.00/float(real_times[j]), 2)
    throughputs.append(round(40960.00/float(real_times[j]), 2))

avg = round(avg/5.00, 2)
SD = 0

for k in range(5):
    #print str(throughputs[k])
    SD += (avg-throughputs[k])*(avg-throughputs[k])/5.00
            
SD = round(math.sqrt(SD), 2)
    
print str(avg)
print str(SD)
