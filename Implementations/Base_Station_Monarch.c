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

extern struct sensorNodeInfo *sensorNodesInfo;
extern struct simulationData *simulationArray;
extern struct lastReportedAlert *lastReportedAlerts;

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
    CreateAFile(fileName, size - 1, nrows, ncols, threshold, num_of_iterations);
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
                    if(simulationArray[i].year != CURRENT_YEAR) // If an empty cell is found
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
