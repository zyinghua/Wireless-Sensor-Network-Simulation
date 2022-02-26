/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/
#ifndef HELPER_FUNCTIONS_H_INCLUDED
#define HELPER_FUNCTIONS_H_INCLUDED
#include "WSN_Simulation_Main.h"

int calculate_Time_Difference(int day1, int hour1, int min1, int sec1, int day2, int hour2, int min2, int sec2);
float Generate_Random_Reading(int min, int max);
void CreateAFile(char *pFilename, int num_of_sensor_nodes, int nrows, int ncols, int threshold, int num_of_iter);
void AppendToFile(char *pFilename, int match, int iteration, int alert_year, int alert_month, int alert_day, int alert_hour, int alert_min, int alert_sec, int sensor_rank, int alert_source_coord[], float SMA_Received, int ncols, int neighbors[], float neighbor_readings[], struct simulationData matched_or_latest_data, int num_of_msg_matched, int total_alert_msg_with_BS, char SN_IPaddress[32], char SN_neighborsIP[4][32], double comm_time);
void LogFaultToFile(char *pFilename, int ncols, int detected_node_rank, struct sensorNodeInfo sensorNodeInfo, struct lastReportedAlert lastReportedAlert, double elapsed_time, int source);
void AppendSummaryToFile(char *pFilename, double time_taken_bs, int num_of_true_alerts, int num_of_false_alerts, double total_comm_time, int terminated_normally);
void checkHostName(int hostname);
void checkHostEntry(struct hostent * hostentry);
void checkIPbuffer(char *IPbuffer);

#endif
