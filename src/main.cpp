#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include "oscc.h"
 #include <unistd.h>
#include "compile.h"

#ifdef COMMANDER
#include "commander.h"
#include "can_protocols/steering_can_protocol.h"
#endif
#ifdef ROS
#include "ros/ros.h"
#include "std_msgs/Float64.h"
#include "std_msgs/Bool.h"
#endif
#ifdef JOYSTICK
#include "joystick.h"
#endif
#define STEERING_RANGE_PERCENTAGE (0.36)
void smooth(double *v, int e);

#define COMMANDER_UPDATE_INTERVAL_MICRO (5000)
#define SLEEP_TICK_INTERVAL_MICRO (1000)
state car_state;

static int error_thrown = OSCC_OK;
#if JOYSTICK
int steer_e;
int brake_e;
int throt_e;
double steer_factor;
double brake_factor;
double throt_factor;
#endif

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
#if JOYSTICK
void print_smooth(const char *c, int e, double d)
{
    char *tmp;
    char m[50];
    if (e == 0)
        sprintf(m, "y=1-sqrt(1-x^2)*sign(x)*%lf", d);
    if (e == 1)
        sprintf(m, "y=x*%lf", d);
    if (e >= 2)
    {
        if (e % 2 == 0)
            sprintf(m, "y=x^%d*%lf", e, d);
        else
            sprintf(m, "y=x^%d*sign(x)*%lf", e, d);
    }
    tmp = m;
    printf("Smoothing for %s is set to %s\n", c, tmp);
}
void print_smooths()
{
    print_smooth("braking", brake_e, brake_factor);
    print_smooth("throttle", throt_e, throt_factor);
    print_smooth("steering", steer_e, steer_factor);
}
#endif

#if ROS
void steering_callback(const std_msgs::Float64::ConstPtr &msg)
{
    double d = msg->data;
    car_state.steering_torque = d;
}
void throttle_callback(const std_msgs::Float64::ConstPtr &msg)
{
    double d = msg->data;
    car_state.throttle = d;
}
void brake_callback(const std_msgs::Float64::ConstPtr &msg)
{
    double d = msg->data;
    car_state.brakes = d;
}
void enabled_callback(const std_msgs::Bool::ConstPtr &msg)
{
    bool d = msg->data;
    car_state.enabled = d;
}
ros::Publisher *p_steer;
ros::Publisher *p_throt;
ros::Publisher *p_brake;
ros::Publisher *p_enabl;
void ros_send(state car_state)
{
    auto msgs = std_msgs::Float64();
    msgs.data = car_state.steering_torque;
    p_steer->publish(msgs);
    auto msgst = std_msgs::Float64();
    msgst.data = car_state.throttle;
    p_throt->publish(msgst);
    auto msgsb = std_msgs::Float64();
    msgsb.data = car_state.brakes;
    p_brake->publish(msgsb);
    auto bmsg = std_msgs::Bool();
    bmsg.data = car_state.enabled;
    p_enabl->publish(bmsg);
}
#endif
#if COMMANDER
int channel;
#endif
void decodeParameters(int argc, char *argv[]);
int main(int argc, char *argv[])
{
    printf("V%d.%d: COMPILED WITH [", MAJOR, MINOR);
#if ROS
    printf(" ROS ");
#endif
#if JOYSTICK
    printf(" JOYSTICK ");
#endif
#if COMMANDER
    printf(" COMMANDER ");
#endif
    printf("]\r\n");

    errno = 0;

    decodeParameters(argc, argv);
#if JOYSTICK
    print_smooths();
#endif
    oscc_result_t ret = OSCC_OK;

#if COMMANDER
    char m[150];
    const char *str = "ip link set can%d type can bitrate 500000\n";
    sprintf(m, str, channel);
    //printf("%s",m);
    system(m);

    sprintf(m, "ip link set up can%d\n", channel);
    //printf("%s",m);
    system(m);
    unsigned long long update_timestamp = get_timestamp_micro();
    unsigned long long elapsed_time = 0;
#endif
    struct sigaction sig;
    sig.sa_handler = signal_handler;

    sigaction(SIGINT, &sig, NULL);
#if ROS
    char hostnamePtr[80];
    gethostname(hostnamePtr,79);
#if COMMANDER
    ros::init(argc, argv, std::string(hostnamePtr)+"/drivekit");
    ros::NodeHandle n;
    ros::Subscriber sub_steer = n.subscribe("car/steering/torque", 1, steering_callback);
    ros::Subscriber sub_throt = n.subscribe("car/throttle", 1, throttle_callback);
    ros::Subscriber sub_brake = n.subscribe("car/brake", 1, brake_callback);
    ros::Subscriber sub_enabled = n.subscribe("car/enabled", 1, enabled_callback);
#endif
#if JOYSTICK
    ros::init(argc, argv, std::string(hostnamePtr)+"/joystick");
    ros::NodeHandle n;
    ros::Publisher pub_steep = n.advertise<std_msgs::Float64>("car/steering/torque", 1);
    ros::Publisher pub_throt = n.advertise<std_msgs::Float64>("car/throttle", 1);
    ros::Publisher pub_brake = n.advertise<std_msgs::Float64>("car/brake", 1);
    ros::Publisher pub_enabl = n.advertise<std_msgs::Bool>("car/enabled", 1);
    p_steer = &pub_steep;
    p_brake = &pub_brake;
    p_enabl = &pub_enabl;
    p_throt = &pub_throt;
#endif
#endif

#if COMMANDER
    ret = (oscc_result_t)commander_init(channel);
    if (ret == OSCC_OK)
    {
#endif

#if JOYSTICK
        printf("\nControl Ready:\n");
        printf("    START - Enable controls\n");
        printf("    BACK - Disable controls\n");
        printf("    LEFT TRIGGER - Brake\n");
        printf("    RIGHT TRIGGER - Throttle\n");
        printf("    LEFT STICK - Steering\n");
        joystick_init();
#endif

        while (ret == OSCC_OK && error_thrown == OSCC_OK
#if ROS
               && ros::ok()
#endif
        )
        {
#if COMMANDER
            elapsed_time = get_elapsed_time(update_timestamp);

            if (elapsed_time > COMMANDER_UPDATE_INTERVAL_MICRO)
            {
                update_timestamp = get_timestamp_micro();
#endif

#if JOYSTICK
                ret = (oscc_result_t)check_for_controller_update(car_state);
#endif
#if ROS && JOYSTICK
                ros_send(car_state);
#endif
#if COMMANDER
                commander_update(car_state);
            }
#endif
            // Delay 1 ms to avoid loading the CPU
            (void)usleep(SLEEP_TICK_INTERVAL_MICRO);
        }
#if COMMANDER
        commander_close(channel);
    }
#endif
#if JOYSTICK
    joystick_close();
#endif
    return 0;
}
bool isInteger(std::string line)
{
    if (line.empty())
        return false;
    char *p;
    strtol(line.c_str(), &p, 10);
    return *p == 0;
}
bool isDouble(std::string line)
{
    if (line.empty())
        return false;
    char *p;
    strtod(line.c_str(), &p);
    return *p == 0;
}
std::string getValueForParameter(std::string name, std::vector<std::string> unnamed, std::map<std::string, std::string> named, int &idx)
{
    if (!named[name].empty())
        return named.at(name);
    if (idx < unnamed.size())
    {
        auto ret = unnamed[idx];
        idx++;
        return ret;
    }
    return "";
}
int asInt(std::string name, std::string val, int def, bool mandatory)
{
    int ret;
    if (!isInteger(val.c_str()))
    {
        if (mandatory)
        {
            std::cout << "argument " + name + " is missing or invalid";
            exit(1);
        }
        else
        {
            ret = def;
        }
    }
    else
    {
        ret = atoi(val.c_str());
    }
}
double asDouble(std::string name, std::string val, double def, bool mandatory)
{
    double ret;
    if (!isDouble(val.c_str()))
    {
        if (mandatory)
        {
            std::cout << "argument " + name + " is missing or invalid";
            exit(1);
        }
        else
        {
            ret = def;
        }
    }
    else
    {
        ret = atof(val.c_str());
    }
}
void printHelp(std::string str)
{
    std::cout << "usage : "
#ifdef COMMANDER
              << "sudo "
#endif
              << str
#ifdef COMMANDER
              << "channel"
#endif
#ifdef JOYSTICK
              << "[steering=4] [brake=6] [throttle=5] [steering_max=1] [brake_max=1] [throttle_max=1]" << std::endl
#endif
#ifdef JOYSTICK
              << "Possible smoothing types:" << std::endl
              << "    0\t:y=1-(sqrt(a-x^2)) * sign(x)" << std::endl
              << "    1\t:y=x" << std::endl
              << "    2\t:y=x^2 *sign(x)" << std::endl
              << "    3\t:y=x^3 " << std::endl
              << "  e>3\t:y=x^e (*sign(x) if e is even) " << std::endl
#endif
              << std::endl;
    exit(0);
}
int getValueForParameterInt(std::string name, std::vector<std::string> unnamed, std::map<std::string, std::string> named, int &idx, int def, bool mandatory)
{
    return asInt(name, getValueForParameter(name, unnamed, named, idx), def, mandatory);
}
int getValueForParameterDouble(std::string name, std::vector<std::string> unnamed, std::map<std::string, std::string> named, int &idx, int def, bool mandatory)
{
    return asDouble(name, getValueForParameter(name, unnamed, named, idx), def, mandatory);
}
void decodeParameters(int argc, char *argv[])
{
    //check parameters validity
    bool foundNamed = false, previousIsName = false;
    std::string previous;
    std::map<std::string, std::string> parameters;
    std::vector<std::string> unnamed;
    for (auto i = 1; i < argc; i++)
    {
        auto isNumber = isDouble(argv[i]);
        if (!isNumber)
        {
            foundNamed = true;
            previousIsName = true;
            previous = argv[i];
            if (previous == "help")
                printHelp(argv[0]);
        }
        else
        {
            if (!previousIsName && foundNamed)
            {
                printf("named parameters can only be at the end\r\n");
                exit(1);
            }
            else if (foundNamed && previousIsName)
            {
                parameters[previous] = argv[i];
            }
            else
            {
                unnamed.push_back(argv[i]);
            }

            previousIsName = false;
        }
    }
    int idx = 0;
#if COMMANDER
    channel = getValueForParameterInt("-c", unnamed, parameters, idx, 0, false);
    channel = getValueForParameterInt("-channel", unnamed, parameters, idx, channel, false);
#endif
#if JOYSTICK
    steer_e = getValueForParameterInt("-s", unnamed, parameters, idx, 4, false);
    brake_e = getValueForParameterInt("-b", unnamed, parameters, idx, 6, false);
    throt_e = getValueForParameterInt("-t", unnamed, parameters, idx, 5, false);
    steer_factor = getValueForParameterDouble("-sf", unnamed, parameters, idx, 1, false);
    brake_factor = getValueForParameterDouble("-bf", unnamed, parameters, idx, 1, false);
    throt_factor = getValueForParameterDouble("-tf", unnamed, parameters, idx, 1, false);

    std::vector<std::string> empty;
    int i = 0;
    steer_e = getValueForParameterInt("-steering", empty, parameters, i, steer_e, false);
    brake_e = getValueForParameterInt("-braking", empty, parameters, i, brake_e, false);
    throt_e = getValueForParameterInt("-throttle", empty, parameters, i, throt_e, false);
    steer_factor = getValueForParameterDouble("-steering-factor", empty, parameters, i, steer_factor, false);
    brake_factor = getValueForParameterDouble("-braking-factor", empty, parameters, i, brake_factor, false);
    throt_factor = getValueForParameterDouble("-throttle-factor", empty, parameters, i, throt_factor, false);

#endif
}