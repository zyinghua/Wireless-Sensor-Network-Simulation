/***************************************************** ***/
/* PLEASE RUN AS mpicc WSN_Simulation.c -o wsn -fopenmp */
/*******************************************************/
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

#define MAX_RANDOM_READING 6500
#define MIN_RANDOM_READING 5500
#define THRESHOLD_TOLERANCE 100 // (m)
#define SIMULATION_ARRAY_SIZE 32
#define TIME_TOLERANCE 15 // seconds
#define SMA_PERIOD 5
#define TERMINATION_TAG 0
#define REQUEST_READING_TAG 1
#define ALERT_TAG 2
#define ALIVE_MSG_TAG 3
#define NEIGHBORS_IP_TAG 4
#define SENSOR_BASIC_INFO_TAG 5
#define ALIVE_RESPOND_INTERVAL 3 // Minimum 1
#define BS_SLEEP 2
#define SA_SLEEP 2
#define SENSOR_SLEEP 5
#define ALIVE_CHECK_THREAD_SLEEP 1
#define FAULT_TOLERANCE_TIME_RANGE_FACTOR 1.5 //APPLIED AS: "FAULT_TOLERANCE_TIME_RANGE_FACTOR * (size - 1)" (seconds) Meaning after this value of seconds it's considered as a fault (Time-Out)
#define CURRENT_YEAR 2021 // For data existance checking
#define SMA_STARTING_VALUE 5800.0

struct sensorNodeInfo {
    int coord[2];
    int neighbors[4];
    char IPaddress[32];
    char neighborsIP[4][32];
};

struct simulationData {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    int coord[2];
    float height;
};

struct lastReportedAlert {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    float height;
    int match;
};

struct sensorNodeInfo *sensorNodesInfo = NULL;
struct simulationData *simulationArray = NULL;
struct lastReportedAlert *lastReportedAlerts = NULL;

void CreateAFile(char *pFilename, int num_of_sensor_nodes, int nrows, int ncols);
void AppendToFile(char *pFilename, int match, int iteration, int alert_year, int alert_month, int alert_day, int alert_hour, int alert_min, int alert_sec, int sensor_rank, int alert_source_coord[], float SMA_Received, int ncols, int neighbors[], float neighbor_readings[], struct simulationData matched_or_latest_data, int num_of_msg_matched, int total_alert_msg_with_BS, char SN_IPaddress[32], char SN_neighborsIP[4][32], double comm_time);
float Generate_Random_Reading(int min, int max);
int calculate_Time_Difference(int day1, int hour1, int min1, int sec1, int day2, int hour2, int min2, int sec2);
int master_io(MPI_Comm master_comm, MPI_Comm comm, int threshold, int nrows, int ncols, int num_of_bs_iterations);
int slave_io(MPI_Comm master_comm, MPI_Comm comm, int nrows, int ncols, int threshold);
void AppendSummaryToFile(char *pFilename, double time_taken_bs, int num_of_true_alerts, int num_of_false_alerts, double total_comm_time, int terminated_normally);
void LogFaultToFile(char *pFilename, int ncols, int detected_node_rank, struct sensorNodeInfo sensorNodeInfo, struct lastReportedAlert lastReportedAlert, double elapsed_time, int source);
void checkHostName(int hostname);
void checkHostEntry(struct hostent * hostentry);
void checkIPbuffer(char *IPbuffer);

int main (int argc, char *argv[])
{
    int rank, size, provided, nrows, ncols, threshold, position, num_of_bs_iterations;
    char outbuf[256];
    MPI_Comm new_comm;
    // Initiate the MPI environment with indicating the level of multithreading support needed.
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
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

int master_io(MPI_Comm master_comm, MPI_Comm comm, int threshold, int nrows, int ncols, int num_of_iterations)
{
    /*************************************************/
    /*Initialization of relevant variables*/
    /*************************************************/
    int i, size, tid, alert_check_completed = 0, end_of_iter = 0;
    MPI_Comm_size( master_comm, &size );
    int terminate = 0;
    int terminated_normally = -1; // For logging purpose, whether terminated normally or by fault(s)
    double start, end;
    char buf[size - 1][256], alert_recv_completed[size - 1], fileName[256];
    
    for(i = 0; i < size - 1; i++)
    {
        alert_recv_completed[i] = 0;
    }
    
    start = MPI_Wtime();   
    int alert_coming[size - 1]; // whether there's an alert coming from ('i'+1)th sensor node, initially all 0(False)s. 
 
    omp_set_num_threads(4);
    
    sensorNodesInfo = (struct sensorNodeInfo*)malloc((size - 1) * sizeof(struct sensorNodeInfo));
    simulationArray = (struct simulationData*)malloc(SIMULATION_ARRAY_SIZE * sizeof(struct simulationData)); //SIMULATION_ARRAY_SIZE = 32
    lastReportedAlerts = (struct lastReportedAlert*)malloc((size - 1) * sizeof(struct lastReportedAlert));
    /*************************************************/
    
    /*********************************************************************/
    /*When the user does not specify the values, get from the sensor node*/
    /********************************************************************/
    if(nrows == 0 || ncols == 0)
    {
        MPI_Request request[2];
        MPI_Status status[2];
        
        MPI_Irecv(&nrows, 1, MPI_INT, 1, 101, master_comm, &request[0]);
        MPI_Irecv(&ncols, 1, MPI_INT, 1, 102, master_comm, &request[1]);
        MPI_Waitall(2, request, status);  
    }
    /*********************************************************************/
    
    /*********************************************************************************/
    /*get basic information of all the sensor nodes and store them in the heap array*/
    /*******************************************************************************/
    MPI_Request sensors_info_request[size - 1];
    MPI_Status sensors_info_status[size - 1];
    char bufs[size - 1][256];
    
    for(i = 0; i < size - 1; i++)
        MPI_Irecv(&bufs[i], 256, MPI_CHAR, i+1, SENSOR_BASIC_INFO_TAG, master_comm, &sensors_info_request[i]);
    MPI_Waitall(size - 1, sensors_info_request, sensors_info_status);
    
    for(i = 0; i < size - 1; i++)
    {
        int position = 0;
        
		MPI_Unpack(bufs[i], 256, &position, &sensorNodesInfo[i].coord, 2, MPI_INT, master_comm);
		MPI_Unpack(bufs[i], 256, &position, &sensorNodesInfo[i].neighbors, 4, MPI_INT, master_comm);
		MPI_Unpack(bufs[i], 256, &position, &sensorNodesInfo[i].IPaddress, 32, MPI_CHAR, master_comm);
		MPI_Unpack(bufs[i], 256, &position, &sensorNodesInfo[i].neighborsIP, 4*32, MPI_CHAR, master_comm);
    }
    /*******************************************************************************/
    
    /*********************************************/
    /*Create the log file*/
    /*********************************************/
    sprintf(fileName, "WSN_Log_File.txt");
    CreateAFile(fileName, size - 1, nrows, ncols);
    printf("Log File generated.\n");
    /*********************************************/
    
    #pragma omp parallel private(tid, i) shared(simulationArray, alert_coming, sensorNodesInfo, lastReportedAlerts, terminate, terminated_normally, size, start, end, fileName, alert_check_completed, buf, alert_recv_completed, end_of_iter)
    {
        tid = omp_get_thread_num();
        
        if (tid == 0) // Satellite Altimeter Simulation
        {
            printf("Satellite Altimeter: Started\n");
            
            while(!terminate)
            {
                srand((unsigned int)time(NULL));
                // Generate a random sea water column height reading which exceeds the threshold
                float simulated_reading = Generate_Random_Reading(threshold, threshold+500);
                //Generate random coordinates within the range of the cartesian grid layout
                int simulated_coord[2] = {rand() % nrows , rand() % ncols};
                
                /*****Store the data into the shared global array*****/
                for(i = 0; i < SIMULATION_ARRAY_SIZE; i++)
                {
                    if(simulationArray[i].year == 0) // If an empty cell is found
                    {
                        time_t t = time(NULL);
                        struct tm tm = *localtime(&t);
                        #pragma omp critical // Protect the shared global array from race condition
                        {
                            simulationArray[i].year = tm.tm_year + 1900;
                            simulationArray[i].month = tm.tm_mon + 1;
                            simulationArray[i].day = tm.tm_mday;
                            simulationArray[i].hour = tm.tm_hour;
                            simulationArray[i].min = tm.tm_min;
                            simulationArray[i].sec = tm.tm_sec;
                            simulationArray[i].coord[0] = simulated_coord[0];
                            simulationArray[i].coord[1] = simulated_coord[1];
                            simulationArray[i].height = simulated_reading;
                        }
                        break; 
                    }
                    
                    //Otherwise if 'i' is the last one and simulationArray[i] is not empty
                    if(i == SIMULATION_ARRAY_SIZE - 1)
                    {
                        time_t t = time(NULL);
                        struct tm tm = *localtime(&t);
                        
                        #pragma omp critical // Protect the shared global array from race condition
                        {
                            // FIFO: Replace the first one with the next one and so on...
                            for(int j = 0; j < SIMULATION_ARRAY_SIZE - 1; j++)
                            {
                                simulationArray[j] = simulationArray[j + 1];
                            }
                            // And then store the new data at the last index
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].year = tm.tm_year + 1900;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].month = tm.tm_mon + 1;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].day = tm.tm_mday;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].hour = tm.tm_hour;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].min = tm.tm_min;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].sec = tm.tm_sec;
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].coord[0] = simulated_coord[0];
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].coord[1] = simulated_coord[1];
                            simulationArray[SIMULATION_ARRAY_SIZE - 1].height = simulated_reading;
                       }
                    }
                }
                
                if(!terminate)
                    sleep(SA_SLEEP);
            }
            
            printf("Satellite Altimeter: Exited\n");
         }
         else if(tid == 1) // Handle the alive messages from the sensor nodes which are to detect faults if there's any
         {
	        MPI_Status status;
	        MPI_Status probe_status;
	        int flag = 0, alive_msg;
	        struct timespec clock_time; 
	        double elapsed_time; 
            int local_terminate = 0;
	        
	        printf("Safety Thread: Started\n");
	        
	        /********************************************************/
	        /*alive timestamp array stores for all the sensor nodes*/
	        /******************************************************/
	        clock_gettime(CLOCK_MONOTONIC, &clock_time); 

		    // Saves the current clock time for each sensor node
		    // The clock time is used to calculate the last active time of each sensor node
		    struct timespec *sensors_last_alive_timestamp_array = (struct timespec*)malloc((size - 1) * sizeof(struct timespec)); // dynamic or heap array
		    for(i = 0; i < (size - 1); i++)
		    {
			    sensors_last_alive_timestamp_array[i].tv_sec =  clock_time.tv_sec;
			    sensors_last_alive_timestamp_array[i].tv_nsec =  clock_time.tv_nsec;
		    }
	        
	        while (!local_terminate) 
	        {
		        MPI_Iprobe(MPI_ANY_SOURCE, ALIVE_MSG_TAG, master_comm, &flag, &probe_status);
		        
		        if(flag == 1)
		        {
			        flag = 0; // Set back to default for the next iteration
			        MPI_Recv(&alive_msg, 1, MPI_INT, probe_status.MPI_SOURCE, ALIVE_MSG_TAG, master_comm, &status);
			        
			        clock_gettime(CLOCK_MONOTONIC, &clock_time); // get the current timestamp
			        sensors_last_alive_timestamp_array[probe_status.MPI_SOURCE - 1].tv_sec =  clock_time.tv_sec;
			        sensors_last_alive_timestamp_array[probe_status.MPI_SOURCE - 1].tv_nsec =  clock_time.tv_nsec;
		        }
                
                // Check all the sensor nodes
                for(i = 0; i < size - 1; i++)
			    {
                    elapsed_time = (clock_time.tv_sec - sensors_last_alive_timestamp_array[i].tv_sec) * 1e9; 
                    elapsed_time = (elapsed_time + (clock_time.tv_nsec - sensors_last_alive_timestamp_array[i].tv_nsec)) * 1e-9; 
	                
                    if(elapsed_time > FAULT_TOLERANCE_TIME_RANGE_FACTOR * (size - 1))
                    {
                        LogFaultToFile(fileName, ncols, i, sensorNodesInfo[i], lastReportedAlerts[i], elapsed_time, 0);
                        #pragma omp critical
                        {
                            terminate = 1;
                            terminated_normally = 0;
                        }   
                    }
			    }
			    
			    #pragma omp critical
			        local_terminate = terminate;
			    
			    if(!local_terminate) 
		            sleep(ALIVE_CHECK_THREAD_SLEEP);
	        }

	        printf("Safety Thread: Exited\n");
	        fflush(stdout);
	        
	        free(sensors_last_alive_timestamp_array);
         }
         else if(tid == 2) // Handles the send/recv matters of the base station
         {
            printf("Base Station Send/Recv Thread: Started\n");
            while(!terminate)
            {
                /*********************************************************************/
                /*Ask if any of the sensor nodes are sending alert in this iteration*/
                /********************************************************************/  
                MPI_Request alert_request[size - 1];
                MPI_Status alert_status[size - 1];
                double recv_start_time[size - 1];
                int faulty_node = -1;
                     
                for(i = 0; i < size - 1; i++)
                {
                    #pragma omp critical
                        MPI_Irecv(&alert_coming[i], 1, MPI_INT, i+1, ALERT_TAG, master_comm, &alert_request[i]);
                    recv_start_time[i] = MPI_Wtime();
                }  
                    
                /*********************************************************************/
                /*Fault detection: Apply Time out approach on the receive operations
                as the other thread isn't able to cover the fault occurs during the
                recv.*/
                /********************************************************************/
                for(i = 0; i < size - 1; i++)
                {
                    int gotData = 0;
                    MPI_Test(&alert_request[i], &gotData, &alert_status[i]); // Tests for the completion of a request

                    //loop until received, otherwise time out
                    while (!gotData && (MPI_Wtime() - recv_start_time[i]) < FAULT_TOLERANCE_TIME_RANGE_FACTOR * (size - 1)) 
                    {
                        // While within a time range, keep testing for result
                        MPI_Test(&alert_request[i], &gotData, &alert_status[i]);
                    }
                        
                    // If we still haven't received the msg after the time out
                    // Clean up and prepare to gracefully shut down
                    if(!gotData) 
                    {
                        MPI_Cancel(&alert_request[i]);
                        MPI_Request_free(&alert_request[i]);
                        
                        #pragma omp critical
                        {
                          terminate = 1;
                          terminated_normally = 0;
                        }
                          
                        faulty_node = i;
                        break;
                    }
                }
                    
                if(faulty_node != -1) // There must be a faulty node detected
                {
                    LogFaultToFile(fileName, ncols, faulty_node, sensorNodesInfo[faulty_node], lastReportedAlerts[faulty_node], MPI_Wtime()-recv_start_time[faulty_node], 1);
                    break;
                }
                else
                {
                    #pragma omp critical
                        alert_check_completed = 1;
                }          
                /*****************************************************************/
                
                for(i = 0; i < size - 1; i++)
                {
                    // if value of 1 (true) is received, that means an alert is coming from sensor node (i+1)
                    if(alert_coming[i])
                    {
                        double recv_start_time;
                        /**************************************/
                        /*Listening for the incoming alert...*/
                        /*************************************/
                        #pragma omp critical
                            MPI_Irecv(buf[i], 256, MPI_PACKED, i+1, ALERT_TAG, master_comm, &alert_request[i]);
                        recv_start_time = MPI_Wtime();
                        /*********************************************************************/
                        /*Fault detection: Apply Time out approach on the receive operations
                        as the other thread isn't able to cover the fault occurs during the
                        recv.*/
                        /********************************************************************/
                        int gotData = 0;
                        MPI_Test(&alert_request[i], &gotData, &alert_status[i]); // Tests for the completion of a request

                        //loop until received, otherwise time out
                        while (!gotData && (MPI_Wtime() - recv_start_time) < FAULT_TOLERANCE_TIME_RANGE_FACTOR * (size - 1)) 
                        {
                          // While within a time range, keep testing for result
                          MPI_Test(&alert_request[i], &gotData, &alert_status[i]);
                        }
                        
                        // If we still haven't received the msg after the time out
                        // Clean up and prepare to gracefully shut down
                        if (!gotData) 
                        {
                            MPI_Cancel(&alert_request[i]);
                            MPI_Request_free(&alert_request[i]);
                              
                            #pragma omp critical
                            {
                               terminate = 1;
                               terminated_normally = 0;
                            }

                            LogFaultToFile(fileName, ncols, i, sensorNodesInfo[i], lastReportedAlerts[i], MPI_Wtime()-recv_start_time, 1);
                            break;
                        }
                        else
                        {
                            #pragma omp critical
                                alert_recv_completed[i] = 1;
                        }
                        /********************************************************************/
                    }
                }
                
                if(terminate)
                {
                    break;
                }
                else
                {
                    while(!end_of_iter)
                    {
                        // Wait for the other thread (acts as the Base Station) to finish data processing
                    }
                    
                    #pragma omp critical
                        end_of_iter = 0; // Set back to default for the next iteration  
                }
            }
            
            printf("Base Station Send/Recv Thread: Exited\n");
         }
         else // tid == 3, acts as the base station node
         {
            int num_of_true_alerts = 0, num_of_false_alerts = 0;
            int iteration;
            double total_communication_time = 0.0;
            int round = 0; // When user specifies a non-zero value to continue, round++
            int total_alert_msg[size - 1]; // Total number of messages sent between sensor node (i+1) and the base station, excluding all the confirmation messages sent in every iteration
            for(i = 0; i < size - 1; i++)
                total_alert_msg[i] = 0;

            for(iteration = 0; iteration < num_of_iterations; iteration++)
            {            
                printf("[Base station] Current iteration: %d.\n", iteration+round*num_of_iterations);
                
                while(!alert_check_completed && !terminate) // terminate will be set to 1 if fault(s) detected
                {
                    //Wait
                }
                
                if(terminate)
                {
                    MPI_Request terminate_request[size - 1];
                    MPI_Status terminate_status[size - 1];
                    
                    for(i = 0; i < size - 1; i++)
                       MPI_Isend(&terminate, 1, MPI_INT, i+1, TERMINATION_TAG, master_comm, &terminate_request[i]);
                    
                    break;
                }
                else
                {
                    #pragma omp critical
                        alert_check_completed = 0; // Set back to default for the next iteration
                }

                /**********************************************/
                /*For any sensor nodes sending an alert, do...*/
                /**********************************************/
                for(i = 0; i < size - 1; i++)
                {
                    // if value of 1 (true) is received, that means an alert is coming from sensor node (i+1)
                    if(alert_coming[i])
                    {   
                        int position = 0; // For MPI_Unpack
                        int alert_year, alert_month, alert_day, alert_hour, alert_min, alert_sec, num_of_msg_matched;
                        float SMA_Received;
                        float neighbor_readings[4];
                        double comm_start;
                        
                        while(!alert_recv_completed[i] && !terminate)
                        {
                            // Wait
                        }
                        double comm_end = MPI_Wtime();
                        
                        if(terminate)
                        {
                            break;
                        }
                        else
                        {
                            #pragma omp critical
                                alert_recv_completed[i] = 0; // Set back to default for the next iteration
                        }
                        
                        /**************** master_comm = MPI_COMM_WORLD ****************/
                        MPI_Unpack(buf[i], 256, &position, &alert_year, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &alert_month, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &alert_day, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &alert_hour, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &alert_min, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &alert_sec, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &SMA_Received, 1, MPI_FLOAT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &num_of_msg_matched, 1, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &neighbor_readings, 4, MPI_INT, master_comm);
		                MPI_Unpack(buf[i], 256, &position, &comm_start, 1, MPI_DOUBLE, master_comm);
                        total_alert_msg[i] += 1;
                        total_communication_time += comm_end - comm_start;

                        /*****************************************************************************/
                        /*Upon receiving the alert, compare with the data in the shared global array.*/
                        /*****************************************************************************/
                        int match = 0;
                        int latest_at = -1; // To store the index of the latest data regarding the current sensor node in the simulation array
                        
                        #pragma omp critical // We have to avoid race conditions when we 
                        {
                            for(int k = SIMULATION_ARRAY_SIZE - 1; k >= 0; k--)
                            {
                                //If coordinates match
                                if(simulationArray[k].coord[0] == sensorNodesInfo[i].coord[0] && simulationArray[k].coord[1] == sensorNodesInfo[i].coord[1] && simulationArray[k].year == CURRENT_YEAR) // Last condition is to avoid matching (0,0) with the empty data in the array
                                {
                                    //If readings and timelines match
                                    if(SMA_Received - simulationArray[k].height <= THRESHOLD_TOLERANCE && SMA_Received - simulationArray[k].height >= -THRESHOLD_TOLERANCE && calculate_Time_Difference(simulationArray[k].day, simulationArray[k].hour, simulationArray[k].min, simulationArray[k].sec, alert_day, alert_hour, alert_min, alert_sec) <= TIME_TOLERANCE)
                                    {
                                        match = 1;
                                        num_of_true_alerts +=1;
                                        // Add to the log file
                                        AppendToFile(fileName, match, iteration+round*num_of_iterations, alert_year, alert_month, alert_day, alert_hour, alert_min, alert_sec, i, sensorNodesInfo[i].coord, SMA_Received, ncols, sensorNodesInfo[i].neighbors, neighbor_readings, simulationArray[k], num_of_msg_matched, total_alert_msg[i], sensorNodesInfo[i].IPaddress, sensorNodesInfo[i].neighborsIP, comm_end-comm_start);
                                        // If there's a match, we will only use matched_data without worrying about latest_data
                                        break;
                                    }
                                    else if(latest_at == -1) // If the latest one isn't a match, we will store it anyway and refer to it if no match is found eventually
                                    {
                                        latest_at = k;
                                    }
                                }
                            }
                            
                            if(match == 0)
                            {
                                num_of_false_alerts +=1;
                                
                                // Add to the log file
                                if(latest_at != -1)
                                {
                                    AppendToFile(fileName, match, iteration+round*num_of_iterations, alert_year, alert_month, alert_day, alert_hour, alert_min, alert_sec, i, sensorNodesInfo[i].coord, SMA_Received, ncols, sensorNodesInfo[i].neighbors, neighbor_readings, simulationArray[latest_at], num_of_msg_matched, total_alert_msg[i], sensorNodesInfo[i].IPaddress, sensorNodesInfo[i].neighborsIP, comm_end-comm_start);
                                }
                                else
                                {
                                    struct simulationData empty;
                                    AppendToFile(fileName, match, iteration+round*num_of_iterations, alert_year, alert_month, alert_day, alert_hour, alert_min, alert_sec, i, sensorNodesInfo[i].coord, SMA_Received, ncols, sensorNodesInfo[i].neighbors, neighbor_readings, empty, num_of_msg_matched, total_alert_msg[i], sensorNodesInfo[i].IPaddress, sensorNodesInfo[i].neighborsIP, comm_end-comm_start);
                                } 
                            }
                            
                            // Update the last reported alert of the corresponding node
                            lastReportedAlerts[i].year = alert_year;
                            lastReportedAlerts[i].month = alert_month;
                            lastReportedAlerts[i].day = alert_day;
                            lastReportedAlerts[i].hour = alert_hour;
                            lastReportedAlerts[i].min = alert_min;
                            lastReportedAlerts[i].sec = alert_sec;
                            lastReportedAlerts[i].height = SMA_Received;
                            lastReportedAlerts[i].match = match;
                        } 
                    }    
                }
                
                // If the current iteration is the last one, and terminate isn't activated by any faults,
                // then the program is ready to terminate
                if(iteration == num_of_iterations - 1 && terminate != 1)
                {
                    int shutdown;
                    printf("Do you want to shut down the program? (Enter 0 to shut down, or any other numerical key to continue): ");
                    fflush(stdout);
                    scanf("%d", &shutdown);
                    
                    if(shutdown == 0)
                    {
                        #pragma omp critical
                        {
                            terminate = 1;
                            terminated_normally = 1;
                        }    
                    }
                    else
                    {
                        round++;
                        iteration = -1; // Will be 0 in the next iteration due to iteration++
                    }
                }   
                
                /***********************************************************************************/
                /*Pass the terminate signal to the sensor nodes, if 1 (true), they will terminate.*/
                /**********************************************************************************/
                MPI_Request terminate_request[size - 1];
                MPI_Status terminate_status[size - 1];
                
                for(i = 0; i < size - 1; i++)
                   MPI_Isend(&terminate, 1, MPI_INT, i+1, TERMINATION_TAG, master_comm, &terminate_request[i]);
                
                if(!terminate)
                {
                    sleep(BS_SLEEP);
                    #pragma omp critical
                        end_of_iter = 1;  // Notify the Send/Recv thread  
                }
                else 
                {
                    #pragma omp critical
                        end_of_iter = 1;  // Notify the Send/Recv thread  
                    break; // For in case of terminate signal received due to fault(s) detected
                }
            }
            
            end = MPI_Wtime(); // No need to "omp critical" it since it will never be used in the other thread
            AppendSummaryToFile(fileName, end-start, num_of_true_alerts, num_of_false_alerts, total_communication_time, terminated_normally);    
        }
    }
    printf("Base Station: Exited\n");
    fflush(stdout);
    
    free(sensorNodesInfo);
    free(simulationArray);
    free(lastReportedAlerts);
    return 0;
}

int slave_io(MPI_Comm master_comm, MPI_Comm comm, int nrows, int ncols, int threshold)
{
    /***************************************************************************/
    /*Relevant variables initialization*/
    /*SMA_update_completed, end_of_iter, recv_neighbor_readings_completed,
    compare_reading_completed and termination_received are all treated and used 
    as booleans (0 means false, 1 means true), each represents:
    whether the SMA has been updated regarding the current iteration,
    whether the thread is ready to end the current iteration which is
    to make sure one thread is not entering the next iteration before 
    the other, whether it has completed the reading comparison in the 
    iteration and whether the termination message from the master 
    node (i.e., the base station) is received, respectively.*/
    /***************************************************************************/
    int ndims = 2, counter, tid, size, rank, reorder, my_cart_rank, ierr, i, alive_count = 0, SMA_update_completed = 0, end_of_iter = 0, recv_neighbor_readings_completed = 0, compare_reading_completed = 0, termination_received = 0;
	int neighbors[4] = {}; // [0] = up, [1] = down, [2] = left, [3] = right;
	char neighborsIP[4][32];
	float recvVal[4] = {-1.0, -1.0, -1.0, -1.0}; // [0] = up, [1] = down, [2] = left, [3] = right;
	MPI_Comm comm2D;
	int dims[ndims],coord[ndims];
	int wrap_around[ndims];
	
	omp_set_num_threads(3);
	
	/*****************************************/
	/*Variables for getting the IPv4 address*/
	/*Obtained from GeeksforGeeks*/
	/***************************************/
	char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
    
    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    checkHostName(hostname);
  
    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    checkHostEntry(host_entry);
  
    // To convert an Internet network
    // address into ASCII string
    IPbuffer = inet_ntoa(*((struct in_addr*)
                           host_entry->h_addr_list[0]));
    
    char IPaddress[32];
    strncpy(IPaddress, IPbuffer, sizeof(IPaddress) - 1);
    /*****************************************/ 

	// Simple Moving Average related variables
	// Assuming all the readings are 5500.0 initially
	int readingStream[SMA_PERIOD] = {};
	for (i = 0; i < SMA_PERIOD; i++)
	{
	    readingStream[i] = SMA_STARTING_VALUE;
	}
	float SMA = SMA_STARTING_VALUE;
	
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &rank);
	
	dims[0] = nrows;
	dims[1] = ncols;
	
	//create cartesian topology for the sensor nodes (slave processes)
	MPI_Dims_create(size, ndims, dims);
    
    /*********************************************************************/
    /*When the user does not specify the values, send to the base station*/
    /********************************************************************/
    if(nrows == 0 || ncols == 0)
    {
        if(rank == 0)
        {
            MPI_Request request[2];
            MPI_Status status[2];
            
            MPI_Isend(&dims[0], 1, MPI_INT, 0, 101, master_comm, &request[0]);
            MPI_Isend(&dims[1], 1, MPI_INT, 0, 102, master_comm, &request[1]);
            MPI_Waitall(2, request, status);  
        } 
    } 
	
	/* create cartesian mapping */
	wrap_around[0] = 0; // [0] for wrapping around top/bottom, 1 stands for true
	wrap_around[1] = 0; /* periodic shift is .false. */
	reorder = 1; // reorder is set to true
	ierr = 0;
	ierr = MPI_Cart_create(comm, ndims, dims, wrap_around, reorder, &comm2D);
	if(ierr != 0) printf("ERROR[%d] creating CART\n",ierr);
	
	/* find my coordinates in the cartesian communicator group */
	MPI_Cart_coords(comm2D, rank, ndims, coord); // coordinates are returned into the coord array
	/* use my cartesian coordinates to find my rank in cartesian group*/
	MPI_Cart_rank(comm2D, coord, &my_cart_rank);
	
	MPI_Cart_shift( comm2D, 0, 1, &neighbors[0], &neighbors[1] ); // 0 for rows
	MPI_Cart_shift( comm2D, 1, 1, &neighbors[2], &neighbors[3] ); // 1 for cols
	
	 /**************************************************/
    /*Sending and recving IP address to/from neighbors*/
    /*************************************************/
    MPI_Request send_IP_request[4];
    MPI_Request recv_IP_request[4];
    MPI_Status send_IP_status[4];
    MPI_Status recv_IP_status[4];
    
	for(i = 0; i < 4; i++)
	{                
        MPI_Isend(&IPaddress, 32, MPI_CHAR, neighbors[i], NEIGHBORS_IP_TAG, comm2D, &send_IP_request[i]);
    }
    
    for(i = 0; i < 4; i++)
	{   
        MPI_Irecv(&neighborsIP[i], 32, MPI_CHAR, neighbors[i], NEIGHBORS_IP_TAG, comm2D, &recv_IP_request[i]);
    }
    MPI_Waitall(4, send_IP_request, send_IP_status);
    MPI_Waitall(4, recv_IP_request, recv_IP_status);
    /*************************************************/
    
    /**************************************************/
    /*Sending basic info to the base station*/
    /*************************************************/
    MPI_Request sensors_info_request;
    char outbuf[256];
    int position = 0;
    
    MPI_Pack(&coord, 2, MPI_INT, outbuf, 256, &position, master_comm);
    MPI_Pack(&neighbors, 4, MPI_INT, outbuf, 256, &position, master_comm); 
    MPI_Pack(&IPaddress, 32, MPI_CHAR, outbuf, 256, &position, master_comm); 
	MPI_Pack(&neighborsIP, 4*32, MPI_CHAR, outbuf, 256, &position, master_comm);
    
    MPI_Isend(outbuf, position, MPI_PACKED, 0, SENSOR_BASIC_INFO_TAG, master_comm, &sensors_info_request);
    /**************************************************/
    
	#pragma omp parallel private(tid, i) shared(SMA, termination_received, recvVal, comm2D, counter, recv_neighbor_readings_completed, SMA_update_completed, end_of_iter, compare_reading_completed, threshold, master_comm, alive_count)
    {   
        tid = omp_get_thread_num();
        
        if (tid == 0)
        { 
            /*******************************************************************************/
            /*Sending an "ALIVE" message to the base station in respect of fault detection*/
            /*The fun reason of sending such a message in a different thread is that if a 
            sensor node fairs, its neighbors will wait for its reading if their readings are
            greater than the threshold, which will cause its neighbors unable to send the
            alive message as well if not sending it in a different thread due to the design 
            of the system, hence leading the base station to log the wrong failure node or
            will take more effort to find the right one, therefore sending in a different 
            thread is better in terms of accuracy and efficiency.*/
            /*******************************************************************************/
            while(!termination_received)
	        {
                if(alive_count == ALIVE_RESPOND_INTERVAL)
                {
                    int alive_msg = 1;
                    MPI_Request alive_request;
                    MPI_Isend(&alive_msg, 1, MPI_INT, 0, ALIVE_MSG_TAG, master_comm, &alive_request);
                    alive_count = 0;
                }
                
                alive_count++;
                sleep(ALIVE_CHECK_THREAD_SLEEP);
            }
            /*******************************************************************************/
        }
        else
        {
	        while(!termination_received)
	        {
                if (tid == 1) // A thread to handle send/recv
                {
                    int request_reading, sending_alert;
                    char outbuf[256];
                    
                    MPI_Request send_request[4];
                    MPI_Request receive_request[4];
                    MPI_Request send_reading_request[4];
                    MPI_Request alert_request;
                    MPI_Status send_status[4];
                    MPI_Status receive_status[4];
                    MPI_Status send_reading_status[4];
                    MPI_Status terminate_status;
                    
                    while(!SMA_update_completed)
                    {
                        //wait
                    }
                    
                    #pragma omp critical
                        SMA_update_completed = 0; // Set to default for next iteration
                    
                    // Exceeds the threshold, request and acquire from the neighbourhood nodes for comparison
                    if(SMA > threshold)
                    {
                        request_reading = 1;
                        for(i = 0; i < 4; i++)
	                    {
	                        // Tell its neighbors it does require the readings
	                        MPI_Isend(&request_reading, 1, MPI_INT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_request[i]);
                        }
                        /*********************************************/
                        /*Handling potential requests from neighbors*/
                        /********************************************/
                        int send_reading[4] = {}; // whether sending reading to the neighbor(s) is needed, initially all 0(False)s.
                        for(i = 0; i < 4; i++)
	                    {   
	                        MPI_Irecv(&send_reading[i], 1, MPI_INT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_reading_request[i]);
                        }
                        MPI_Waitall(4, send_request, send_status); // relateing to Isend
                        MPI_Waitall(4, send_reading_request, send_reading_status); // relating to Irecv
                        
                        for(i = 0; i < 4; i++)
	                    {   
	                        if(send_reading[i]) // for any of its neighbors requested for its SMA
	                            MPI_Isend(&SMA, 1, MPI_FLOAT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_reading_request[i]);
                        }
                        
                        #pragma omp critical
                        {
                            for(i = 0; i < 4; i++)
	                        {   
	                            // Recv from its neighbors
	                            MPI_Irecv(&recvVal[i], 1, MPI_FLOAT, neighbors[i], REQUEST_READING_TAG, comm2D, &receive_request[i]);
                            }
                        }
                        MPI_Waitall(4, send_reading_request, send_reading_status); // relating to Isend
	                    MPI_Waitall(4, receive_request, receive_status); // relating to Irecv
	                    
	                    #pragma omp critical
	                        recv_neighbor_readings_completed = 1;
	                    
	                    while(!compare_reading_completed)
	                    {
	                        //wait
	                    }
	                    
	                    #pragma omp critical
	                        compare_reading_completed = 0; // Set back to default for the next iteration
	                    
	                    if(counter >= 2)
                        {   
                            sending_alert = 1;
                            MPI_Isend(&sending_alert, 1, MPI_INT, 0, ALERT_TAG, master_comm, &alert_request);
                            
                            int position = 0; // For pack & unpack
	                        time_t t = time(NULL);
                            struct tm tm = *localtime(&t);
                            int year = tm.tm_year + 1900;
                            int month = tm.tm_mon + 1;
                            int day = tm.tm_mday;
                            int hour = tm.tm_hour;
                            int min = tm.tm_min;
                            int sec = tm.tm_sec;   
                            double comm_start = MPI_Wtime(); 
                            
                            /**************** master_comm = MPI_COMM_WORLD ****************/
                            MPI_Pack(&year, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&month, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&day, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&hour, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&min, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&sec, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&SMA, 1, MPI_FLOAT, outbuf, 256, &position, master_comm);  
		                    MPI_Pack(&counter, 1, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&recvVal, 4, MPI_INT, outbuf, 256, &position, master_comm); 
		                    MPI_Pack(&comm_start, 1, MPI_DOUBLE, outbuf, 256, &position, master_comm);
		                    //Only one send
                            MPI_Isend(outbuf, position, MPI_PACKED, 0, ALERT_TAG, master_comm, &alert_request);
                        }
                        else
                        {
                            sending_alert = 0;
                            MPI_Isend(&sending_alert, 1, MPI_INT, 0, ALERT_TAG, master_comm, &alert_request);
                        }
                        
                        // Reset back to default for the next iteration
                        for(i = 0; i < 4; i++)
                            recvVal[i] = -1.0;
                    }
                    else // Does not exceed the threshold
                    {
                        sending_alert = 0;
                        MPI_Isend(&sending_alert, 1, MPI_INT, 0, ALERT_TAG, master_comm, &alert_request);
                        
                        request_reading = 0;
	                    for(i = 0; i < 4; i++)
	                    {
	                        // Tell its neighbors it doesn't require the readings
	                        MPI_Isend(&request_reading, 1, MPI_INT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_request[i]);
                        }
                        
                        /*********************************************/
                        /*Handling potential requests from neighbors*/
                        /********************************************/
                        int send_reading[4] = {}; // whether sending reading to the neighbor(s) is needed, initially all 0(False)s.
                        for(i = 0; i < 4; i++)
	                    {   
	                        MPI_Irecv(&send_reading[i], 1, MPI_INT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_reading_request[i]);
                        } 
                        MPI_Waitall(4, send_request, send_status); // relateing to Isend
                        MPI_Waitall(4, send_reading_request, send_reading_status); // relating to Irecv

                        for(i = 0; i < 4; i++)
	                    {   
	                        if(send_reading[i])  // If any of its neighbors requested for its SMA
	                            MPI_Isend(&SMA, 1, MPI_FLOAT, neighbors[i], REQUEST_READING_TAG, comm2D, &send_reading_request[i]);
                        }
                        MPI_Waitall(4, send_reading_request, send_reading_status);
                    }

                    #pragma omp critical
                        MPI_Recv(&termination_received, 1, MPI_INT, 0, TERMINATION_TAG, master_comm, &terminate_status);
                    
                    #pragma omp critical
	                    end_of_iter = 1;
                }
                else // tid == 1
                {	
                    srand(getpid() * (unsigned int)time(NULL));
	                float newSensorReading = Generate_Random_Reading(MIN_RANDOM_READING, MAX_RANDOM_READING);
	                
	                // Drop out the first one
                    float prevFirst = readingStream[0];
                    // No need to omp critical'ise this section as the other thread will never access the readingStream array
                    for(i = 0; i < SMA_PERIOD - 1; i++)
                    {
                        readingStream[i] = readingStream[i+1];
                    }
                    // (Append) Replace the last one with the new sensor reading
                    readingStream[SMA_PERIOD - 1] = newSensorReading;
                    // Update SMA
                    #pragma omp critical
                        SMA = SMA + (newSensorReading - prevFirst)/SMA_PERIOD;
                        
                    #pragma omp critical
                        SMA_update_completed = 1;
                        
                    if(SMA > threshold)
                    { 
                        while(!recv_neighbor_readings_completed)
                        {
                            // Waiting to recv neighbor readings
                        }
                        
                        #pragma omp critical
                            recv_neighbor_readings_completed = 0; //Set back to default for the next iteration
                        
                        /******************************************************************/
                        /*Once the values from the neighbors are received*/
	                    /*Handle the received SMAs*/
	                    /******************************************************************/
	                    // Count the number of received readings that exceeds the threshold
	                    #pragma omp critical
                            counter = 0;
                        
                        for(i = 0; i < 4; i++)
	                    {   
	                        // e) compare the readings to its own reading
	                        if (recvVal[i] != -1 && (recvVal[i] - SMA <= THRESHOLD_TOLERANCE && SMA - recvVal[i] <= THRESHOLD_TOLERANCE))
	                        {
	                            #pragma omp critical
	                                counter++;
	                        }  
                        }
                    
	                    #pragma omp critical
	                        compare_reading_completed = 1;  
                    }
                    
                    while(!end_of_iter)
                    {
                        //wait
                    }
                    
                    #pragma omp critical
	                    end_of_iter = 0; // Reset back to default for the next iteration
                }
                
                if(!termination_received)
                sleep(SENSOR_SLEEP);
	        }
	    }   
	}
	printf("Sensor Node - %d: Exited\n", rank);
	
	MPI_Comm_free( &comm2D );
    return 0;
}

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

void CreateAFile(char *pFilename, int num_of_sensor_nodes, int nrows, int ncols)
{
    FILE *pFile = fopen(pFilename, "w");
	fprintf(pFile, "******************************************************\nBase Station Log File:\nNumber of sensor nodes: %d. Grid Dimension = [%d x %d]\n******************************************************\n", num_of_sensor_nodes, nrows, ncols);
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

