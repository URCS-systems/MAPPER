g++ SAM_equi.cpp cgroup.c util.c cpuinfo.c budgets.c -o SAM_equi -lrt -lboost_filesystem -lboost_system
g++ launch_wrapper.cpp cgroup.c util.c -o launch_wrapper -lrt -lboost_filesystem -lboost_system

#If you expecting anything more sophisticated, you have to make it yourself :P
#Will add to existing makefile soon
#This is just a 30 sec fix

echo "If this is the first compilation after a modification, and you got errors, don't feel bad. It happens to everyone including Sandhya. Ofcourse does not happen to me but that's
OK"
