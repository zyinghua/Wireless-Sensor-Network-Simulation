/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/
#ifndef SENSOR_NODES_H_INCLUDED
#define SENSOR_NODES_H_INCLUDED
#include "WSN_Simulation_Main.h"
#include "Helper_Functions.h"

int slave_io(MPI_Comm master_comm, MPI_Comm comm, int nrows, int ncols, int threshold);

#endif
