/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Helper_Functions.h"
#include "WSN_Simulation_Main.h"

int calculate_Time_Difference(int day1, int hour1, int min1, int sec1, int day2, int hour2, int min2, int sec2)
{
    int time_diff_in_sec = (day2 - day1) * 86400 + (hour2 - hour1) * 3600 + (min2 - min1) * 60 + (sec2 - sec1);
    
    return abs(time_diff_in_sec);
}

float Generate_Random_Reading(int min, int max)
{
    float scale = rand() / (float) RAND_MAX;
    return min + scale * (max - min);
}

void CreateAFile(char *pFilename, int num_of_sensor_nodes, int nrows, int ncols, int threshold, int num_of_iter)
{
    FILE *pFile = fopen(pFilename, "w");
	fprintf(pFile, "******************************************************\nBase Station Log File:\nNumber of sensor nodes: %d. Grid Dimension = [%d x %d]\nThreshold: %d\nNumber of Base Station Iterations: %d\n******************************************************\n", num_of_sensor_nodes, nrows, ncols, threshold, num_of_iter);
	fclose(pFile);
}

void AppendToFile(char *pFilename, int match, int iteration, int alert_year, int alert_month, int alert_day, int alert_hour, int alert_min, int alert_sec, int sensor_rank, int alert_source_coord[], float SMA_Received, int ncols, int neighbors[], float neighbor_readings[], struct simulationData matched_or_latest_data, int num_of_msg_matched, int total_alert_msg_with_BS, char SN_IPaddress[32], char SN_neighborsIP[4][32], double comm_time)
{
    FILE *pFile = fopen(pFilename, "a");
	time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    int curr_year = tm.tm_year + 1900;
    int curr_month = tm.tm_mon + 1;
    int curr_day = tm.tm_mday;
    int curr_hour = tm.tm_hour;
    int curr_min = tm.tm_min;
    int curr_sec = tm.tm_sec;
    
    fprintf(pFile, "-----------------------------------------------------------------------\nIteration: %d\nLogged time: %d-%02d-%02d %02d:%02d:%02d\nAlert reported time: %d-%02d-%02d %02d:%02d:%02d\n", iteration, curr_year, curr_month, curr_day, curr_hour, curr_min, curr_sec, alert_year, alert_month, alert_day, alert_hour, alert_min, alert_sec);
    
    if(match) // True alert
    {
        fprintf(pFile,"Alert type: Match\n\n");
    }
    else // False alert
    {
        fprintf(pFile,"Alert type: Mismatch\n\n");
    }
    
    fprintf(pFile, "Reporting Node     	Coord		Height(m)	IPv4\n%d	        		(%d,%d)		%.3f	%s\n\nAdjacent Nodes		Coord		Height (m)	IPv4\n", sensor_rank, alert_source_coord[0],alert_source_coord[1], SMA_Received, SN_IPaddress);
    
    for(int i = 0; i < 4; i++)
    {
        if(neighbors[i] >= 0) // If neighbors[i] is not empty, which means the corresponding neighbor exists
            fprintf(pFile, "%d	        		(%d,%d)		%.3f	%s\n", neighbors[i], neighbors[i]/ncols, neighbors[i]%ncols, neighbor_readings[i], SN_neighborsIP[i]);
    }
    
    if(matched_or_latest_data.year == CURRENT_YEAR)
    {
        fprintf(pFile, "\nSatellite altimeter reporting time: %d-%02d-%02d %02d:%02d:%02d\nSatellite altimeter reporting height (m): %.3f\nSatellite altimeter reporting Coord : (%d,%d)\n\n", matched_or_latest_data.year, matched_or_latest_data.month, matched_or_latest_data.day, matched_or_latest_data.hour, matched_or_latest_data.min, matched_or_latest_data.sec, matched_or_latest_data.height, matched_or_latest_data.coord[0], matched_or_latest_data.coord[1]);
    }
    else // No latest data regarding the current sensor node in the shared global array
    {
        fprintf(pFile, "\nNo latest data from the satellite altimeter regarding this sensor node...\n\n");
    }
    
    fprintf(pFile, "Communication Time (seconds): %.5f\nTotal Messages send between reporting node and base station: %d\nNumber of adjacent matches to reporting node: %d\nMax. tolerance range between nodes readings (m): %d\nMax. tolerance range between satellite altimeter and reporting node readings (m): %d\nTime tolerance range between satellite altimeter and reporting node (seconds): %d\n-----------------------------------------------------------------------\n", comm_time, total_alert_msg_with_BS, num_of_msg_matched, THRESHOLD_TOLERANCE, THRESHOLD_TOLERANCE, TIME_TOLERANCE);
    
	fclose(pFile);
}

void LogFaultToFile(char *pFilename, int ncols, int detected_node_rank, struct sensorNodeInfo sensorNodeInfo, struct lastReportedAlert lastReportedAlert, double elapsed_time, int source)
{
    FILE *pFile = fopen(pFilename, "a");
    
    fprintf(pFile, "\n^^^^^^^^^^^^^^^^^^^^^^^^^^FAULT DETECTED^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nDetected Node     	Coord		IPv4\n%d	        		(%d,%d)		%s\n\n", detected_node_rank, sensorNodeInfo.coord[0], sensorNodeInfo.coord[1], sensorNodeInfo.IPaddress);
    
    if(lastReportedAlert.year == CURRENT_YEAR) // Last reported alert exists
    {
        fprintf(pFile, "Last Reported Alert:\nTime        	            Height		   Match/Mismatch\n%d-%02d-%02d %02d:%02d:%02d         %.3f       ", lastReportedAlert.year,lastReportedAlert.month,lastReportedAlert.day,lastReportedAlert.hour,lastReportedAlert.min,lastReportedAlert.sec, lastReportedAlert.height);    
        
        if(lastReportedAlert.match)
        {
            fprintf(pFile, "Match\n\n");
        }
        else
        {
            fprintf(pFile, "Mismatch\n\n");
        }
    }
    else
    {
       fprintf(pFile, "No alert has been received from this sensor node...\n\n"); 
    }
    
    fprintf(pFile, "Estimated time of failure: %.3f\n\nNeighborhood Sensor Nodes:\nNode      	        Coord		IPv4\n", elapsed_time);
    
    for(int i = 0; i < 4; i++)
    {
        if(sensorNodeInfo.neighbors[i] >= 0)
            fprintf(pFile,"%d	        		(%d,%d)		%s\n",sensorNodeInfo.neighbors[i], sensorNodeInfo.neighbors[i]/ncols, sensorNodeInfo.neighbors[i]%ncols,sensorNodeInfo.neighborsIP[i]);
    }
    
    if(source == 0)
    {
        fprintf(pFile, "\nSource: Fault detected by the 'Alive' messages checking thread\n");
    }
    else
    {
        fprintf(pFile, "\nSource: Fault detected when trying to receive a message from this node\n");
    }
    
    fprintf(pFile, "\n^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");
    
    fclose(pFile);
}

void AppendSummaryToFile(char *pFilename, double time_taken_bs, int num_of_true_alerts, int num_of_false_alerts, double total_comm_time, int terminated_normally)
{
    FILE *pFile = fopen(pFilename, "a");
    
    fprintf(pFile, "\n**********************************Summary**************************************\n");
    
    if(terminated_normally == 1)
    {
        fprintf(pFile, "Termination Result: Terminated normally\n\n");
    }
    else if(terminated_normally == 0)
    {
        fprintf(pFile, "Termination Result: Terminated due to fault(s)\n\n");
    }
    else
    {
        fprintf(pFile, "Termination Result: *ERROR!* with this information\n\n");
    }
    
    fprintf(pFile, "Time taken for the base station (seconds): %.3f\nTotal communication time (seconds): %.3f\nAverage communication time (seconds): %.3f\n\nTotal number of alerts     	Number of TRUE alerts		Number of FALSE alertS\n%d	                		%d	                    	%d	\n*******************************************************************************\n", time_taken_bs, total_comm_time, total_comm_time/(num_of_true_alerts+num_of_false_alerts), num_of_true_alerts+num_of_false_alerts, num_of_true_alerts, num_of_false_alerts);
    
    fclose(pFile);
}

/*Section below is from GeeksforGeeks*/
/*Reference link: https://www.geeksforgeeks.org/c-program-display-hostname-ip-address/*/

// Returns hostname for the local computer
void checkHostName(int hostname)
{
    if (hostname == -1)
    {
        perror("gethostname");
        exit(1);
    }
}
  
// Returns host information corresponding to host name
void checkHostEntry(struct hostent * hostentry)
{
    if (hostentry == NULL)
    {
        perror("gethostbyname");
        exit(1);
    }
}
  
// Converts space-delimited IPv4 addresses
// to dotted-decimal format
void checkIPbuffer(char *IPbuffer)
{
    if (NULL == IPbuffer)
    {
        perror("inet_ntoa");
        exit(1);
    }
}
