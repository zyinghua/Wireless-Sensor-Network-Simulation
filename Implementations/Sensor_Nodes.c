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
#include "Sensor_Nodes.h"

extern struct sensorNodeInfo *sensorNodesInfo;
extern struct simulationData *simulationArray;
extern struct lastReportedAlert *lastReportedAlerts;

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

