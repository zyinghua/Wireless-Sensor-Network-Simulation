/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/
#ifndef BASE_STATION_H_INCLUDED
#define BASE_STATION_H_INCLUDED
#include "WSN_Simulation_Main.h"
#include "Helper_Functions.h"

int master_io(MPI_Comm master_comm, MPI_Comm comm, int threshold, int nrows, int ncols, int num_of_bs_iterations);

#endif
