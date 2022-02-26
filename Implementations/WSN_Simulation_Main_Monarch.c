/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include <unistd.h>
#include <omp.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Helper_Functions.h"
#include "WSN_Simulation_Main.h"
#include "Base_Station.h"
#include "Sensor_Nodes.h"

struct sensorNodeInfo *sensorNodesInfo = NULL;
struct simulationData *simulationArray = NULL;
struct lastReportedAlert *lastReportedAlerts = NULL;

int main (int argc, char *argv[])
{
    int rank, size, provided, nrows, ncols, threshold, position, num_of_bs_iterations;
    char outbuf[256];
    MPI_Comm new_comm;
    // Initiate the MPI environment with indicating the level of multithreading support needed.
    MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    /* process command line arguments*/
	if (argc == 5) {
		nrows = atoi (argv[1]);
		ncols = atoi (argv[2]);
		threshold = atoi (argv[3]);
		num_of_bs_iterations = atoi (argv[4]);
		if((nrows*ncols) != (size - 1)) {
			if(rank == 0) printf("ERROR: nrows*ncols = %d * %d = %d != %d\n", nrows, ncols, nrows*ncols, (size - 1));
			MPI_Finalize(); 
			return 0;
		}
		
		if(threshold > MAX_RANDOM_READING || threshold < MIN_RANDOM_READING) {
			if(rank == 0) printf("ERROR: threshold %d is not within the range of (%d, %d)\n", threshold, MIN_RANDOM_READING, MAX_RANDOM_READING);
			MPI_Finalize(); 
			return 0;
		}
		
		if(num_of_bs_iterations <= 0) {
			if(rank == 0) printf("ERROR: Number of iterations %d for the base station is not a proper value (>0)!\n", num_of_bs_iterations);
			MPI_Finalize(); 
			return 0;
		}
		
	} 
	else{
	    // Default values
		nrows = ncols = 0;
		threshold = 6000;
		num_of_bs_iterations = 20;
	}

    MPI_Comm_split( MPI_COMM_WORLD, rank == 0, 0, &new_comm);
    
    if (rank == 0)
    {
	    master_io( MPI_COMM_WORLD, new_comm, threshold, nrows, ncols, num_of_bs_iterations);
	}
    else
    {
	    slave_io( MPI_COMM_WORLD, new_comm, nrows, ncols, threshold);
	}
	
	MPI_Comm_free(&new_comm);
    MPI_Finalize();
    
    return 0;
}
