#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>

#include "oscc.h"
#include "commander.h"
#include "can_protocols/steering_can_protocol.h"

#define COMMANDER_UPDATE_INTERVAL_MICRO (5000)
#define SLEEP_TICK_INTERVAL_MICRO (1000)

static int error_thrown = OSCC_OK;
int steer_e;
int brake_e;
int throt_e;
double steer_factor;
double brake_factor;
double throt_factor;

static unsigned long long get_timestamp_micro()
{
    struct timeval time;

    gettimeofday(&time, NULL);

    return (time.tv_usec);
}

static unsigned long long get_elapsed_time(unsigned long long timestamp)
{
    unsigned long long now = get_timestamp_micro();
    unsigned long long elapsed_time = now - timestamp;

    return elapsed_time;
}

void signal_handler(int signal_number)
{
    if (signal_number == SIGINT)
    {
        error_thrown = OSCC_ERROR;
    }
}
void print_smooth(char *c, int e,double d)
{
    char *tmp;
    char m[50];
    if (e == 0)
        sprintf(m,"y=1-sqrt(1-x^2)*sign(x)*%lf",d);
    if (e==1)
        sprintf(m,"y=x*%lf",d);
    if(e>=2)
    {
        if(e%2==0)
            sprintf(m,"y=x^%d*%lf",e,d);
        else
            sprintf(m,"y=x^%d*sign(x)*%lf",e,d);
        
    }
    tmp=m;
    printf("Smoothing for %s is set to %s\n",c,tmp);
}
void print_smooths()
{
    print_smooth("braking",brake_e,brake_factor);
    print_smooth("throttle",throt_e,throt_factor);
    print_smooth("steering",steer_e,steer_factor);
}
int main(int argc, char **argv)
{
    int channel;

    errno = 0;

    if (argc < 2 || (channel = atoi(argv[1]), errno) != 0)
    {
        printf("usage : sudo %s channel [steering=4] [brake=6] [throttle=5] [steering_max=1] [brake_max=1] [throttle_max=1]\n", argv[0]);
        printf("Possible smoothing types:\n");
        printf("    0\t:y=1-(sqrt(a-x^2)) * sign(x)\n");
        printf("    1\t:y=x\n");
        printf("    2\t:y=x^2 *sign(x)\n");
        printf("    3\t:y=x^3 \n");
        printf("  e>3\t:y=x^e (*sign(x) if e is even) \n");
        exit(1);
    }
    if (argc < 3 || (steer_e = atoi(argv[2]), errno) != 0)
    {
        printf("No valid smoothing has been specified for steering, using y=x^4*sign(x)\n");
        steer_e = 4;
    }
    if (argc < 4 || (brake_e = atoi(argv[3]), errno) != 0)
    {
        printf("No valid smoothing has been specified for braking, using y=x^6*sign(x)\n");
        brake_e = 6;
    }
    if (argc < 5 || (throt_e = atoi(argv[4]), errno) != 0)
    {
        printf("No valid smoothing has been specified for throttle, using y=x^5\n");
        throt_e = 5;
    }
    if (argc < 6 || (steer_factor = atof(argv[5]), errno) != 0)
    {
        printf("No valid factor has been specified for steering, using f=1\n");
        steer_factor = 1;
    }
    if (argc < 7 || (brake_factor = atof(argv[6]), errno) != 0)
    {
        printf("No valid factor has been specified for braking, using f=1\n");
        brake_factor = 1;
    }
    if (argc < 8 || (throt_factor = atof(argv[7]), errno) != 0)
    {
        printf("No valid factor has been specified for throttle, using f=1\n");
        throt_factor = 1;
    }
    print_smooths();
    char m[150];
    char *str = "ip link set can%d type can bitrate 500000\n";
    sprintf(m, str, channel);
    //printf("%s",m);
    system(m);

    sprintf(m, "ip link set up can%d\n", channel);
    //printf("%s",m);
    system(m);
    oscc_result_t ret = OSCC_OK;
    unsigned long long update_timestamp = get_timestamp_micro();
    unsigned long long elapsed_time = 0;

    struct sigaction sig;
    sig.sa_handler = signal_handler;
    sigaction(SIGINT, &sig, NULL);

    ret = commander_init(channel);

    if (ret == OSCC_OK)
    {
        printf("\nControl Ready:\n");
        printf("    START - Enable controls\n");
        printf("    BACK - Disable controls\n");
        printf("    LEFT TRIGGER - Brake\n");
        printf("    RIGHT TRIGGER - Throttle\n");
        printf("    LEFT STICK - Steering\n");

        while (ret == OSCC_OK && error_thrown == OSCC_OK)
        {
            elapsed_time = get_elapsed_time(update_timestamp);

            if (elapsed_time > COMMANDER_UPDATE_INTERVAL_MICRO)
            {
                update_timestamp = get_timestamp_micro();

                ret = check_for_controller_update();
            }

            // Delay 1 ms to avoid loading the CPU
            (void)usleep(SLEEP_TICK_INTERVAL_MICRO);
        }
        commander_close(channel);
    }

    return 0;
}
