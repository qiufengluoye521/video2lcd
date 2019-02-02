#include <config.h>
#include <video_manager.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>


static int V4l2InitDevice(char *strDevName, PT_VideoDevice ptVideoDevice)
{
    return 0;
}

static int V4l2ExitDevice(PT_VideoDevice ptVideoDevice)
{
    return 0;
}

static int V4l2GetFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    return 0;
}

static int V4l2PutFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    return 0;
}

static int V4l2StartDevice(PT_VideoDevice ptVideoDevice)
{
    return 0;
}
    
static int V4l2StopDevice(PT_VideoDevice ptVideoDevice)
{
    return 0;
}



/* 构造一个VideoOpr结构体 */
static T_VideoOpr g_tV4l2VideoOpr = {
    .name        = "v4l2",
    .InitDevice  = V4l2InitDevice,
    .ExitDevice  = V4l2ExitDevice,
    .GetFrame    = V4l2GetFrameForStreaming,
    .PutFrame    = V4l2PutFrameForStreaming,
    .StartDevice = V4l2StartDevice,
    .StopDevice  = V4l2StopDevice,
};

/* 注册这个结构体 */
int V4l2Init(void)
{
    return RegisterVideoOpr(&g_tV4l2VideoOpr);
}


