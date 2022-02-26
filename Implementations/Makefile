WSN: WSN_Simulation_Main.c
	mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 

runWSN13:
	mpirun -oversubscribe -np 13 wsn

runWSN13C:
	mpirun -oversubscribe -np 13 wsn 3 4 6100 15
	
runWSN21:
	mpirun -oversubscribe -np 21 wsn

runWSN21C:
	mpirun -oversubscribe -np 21 wsn 4 5 6200 25

runWSN26:
	mpirun -oversubscribe -np 26 wsn

runWSN26C:
	mpirun -oversubscribe -np 26 wsn 5 5 6000 10

clean :
	/bin/rm -f wsn *.o
	/bin/rm -f WSN_Log_File.txt *.o

