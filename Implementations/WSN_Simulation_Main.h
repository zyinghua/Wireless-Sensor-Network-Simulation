/****************************************************************************/
//PLEASE RUN AS:
//mpicc WSN_Simulation_Main.c Helper_Functions.c Base_Station.c Sensor_Nodes.c -o wsn -fopenmp 
/****************************************************************************/
#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

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

#endif
