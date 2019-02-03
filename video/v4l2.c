#include <config.h>
#include <video_manager.h>
#include <disp_manager.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>

static int g_aiSupportedFormats[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_RGB565};

static int V4l2GetFrameForReadWrite(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf);
static int V4l2PutFrameForReadWrite(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf);
static T_VideoOpr g_tV4l2VideoOpr;

static int isSupportThisFormat(int iPixelFormat)
{
    int i;
    for (i = 0; i < sizeof(g_aiSupportedFormats)/sizeof(g_aiSupportedFormats[0]); i++)
    {
        if (g_aiSupportedFormats[i] == iPixelFormat)
            return 1;
    }
    return 0;
}


/* 参考 luvcview */

/* open
 * VIDIOC_QUERYCAP 确定它是否视频捕捉设备,支持哪种接口(streaming/read,write)
 * VIDIOC_ENUM_FMT 查询支持哪种格式
 * VIDIOC_S_FMT    设置摄像头使用哪种格式
 * VIDIOC_REQBUFS  申请buffer
 对于 streaming:
 * VIDIOC_QUERYBUF 确定每一个buffer的信息 并且 mmap
 * VIDIOC_QBUF     放入队列
 * VIDIOC_STREAMON 启动设备
 * poll            等待有数据
 * VIDIOC_DQBUF    从队列中取出
 * 处理....
 * VIDIOC_QBUF     放入队列
 * ....
 对于read,write:
    read
    处理....
    read
 * VIDIOC_STREAMOFF 停止设备
 *
 */


static int V4l2InitDevice(char *strDevName, PT_VideoDevice ptVideoDevice)
{
    int iFd;
    struct v4l2_capability tV4l2Cap;
    int iError;
    struct v4l2_fmtdesc tV4l2FmtDesc;
    struct v4l2_format tV4l2Fmt;
    struct v4l2_requestbuffers tV4l2RstBuf;
    struct v4l2_buffer tV4l2Buf;
    int i;

    int iLcdWidth;
    int iLcdHeigt;
    int iLcdBpp;   
    /* 1 open 设备 获取文件句柄 */
    if ((iFd = open(strDevName, O_RDWR)) == -1) 
    {
        DBG_PRINTF("cannot open  %s\n",strDevName);
        goto err_exit;
    }
    ptVideoDevice->iFd = iFd;

    /* 2 确定是否视频设备，如果是视频设备，支持stream接口还是read接口 */
    memset(&tV4l2Cap, 0, sizeof(struct v4l2_capability));
    iError = ioctl(iFd, VIDIOC_QUERYCAP, &tV4l2Cap);
    if (iError) {
        DBG_PRINTF("Error opening device %s: unable to query device.\n", strDevName);
        goto err_exit;
    }
    if ((tV4l2Cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) 
    {
        DBG_PRINTF("%s is not a video capture device\n",strDevName);
        goto err_exit;
    }
    if (tV4l2Cap.capabilities & V4L2_CAP_STREAMING) {
        DBG_PRINTF("%s supports streaming i/o\n", strDevName);
    }
    if (tV4l2Cap.capabilities & V4L2_CAP_READWRITE) {
        DBG_PRINTF("%s supports read i/o\n", strDevName);
    }

    /* 3 枚举支持的格式,这里区别于luvcview源码的处理机制，
    * 这里找到一种支持的格式即退出，并标记这种格式
    */
    memset(&tV4l2FmtDesc, 0, sizeof(tV4l2FmtDesc));
    tV4l2FmtDesc.index = 0;
    tV4l2FmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while ((iError = ioctl(iFd, VIDIOC_ENUM_FMT, &tV4l2FmtDesc)) == 0) 
    {
        if(isSupportThisFormat(tV4l2FmtDesc.pixelformat))
        {
            ptVideoDevice->iPixelFormat = tV4l2FmtDesc.pixelformat;
            break;
        }
        tV4l2FmtDesc.index++;
    }
    
    if(ptVideoDevice->iPixelFormat == 0)
    {
        DBG_PRINTF("can not support the format of this device \n");
        goto err_exit;
    }

    /* 4 设置摄像头格式 */
     GetDispResolution(&iLcdWidth, &iLcdHeigt, &iLcdBpp);
    memset(&tV4l2Fmt, 0, sizeof(struct v4l2_format));
    tV4l2Fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2Fmt.fmt.pix.width = iLcdWidth;
    tV4l2Fmt.fmt.pix.height = iLcdHeigt;
    tV4l2Fmt.fmt.pix.pixelformat = ptVideoDevice->iPixelFormat;
    tV4l2Fmt.fmt.pix.field = V4L2_FIELD_ANY;
    /* 如果驱动程序发现无法某些参数(比如分辨率),
     * 它会调整这些参数, 并且返回给应用程序
     * ----这里驱动好像没有实现
     */
    iError = ioctl(iFd, VIDIOC_S_FMT, &tV4l2Fmt);
    if (iError) 
    {
        DBG_PRINTF("Unable to set format.\n");
        goto err_exit;
    }
    ptVideoDevice->iWidth   = tV4l2Fmt.fmt.pix.width;
    ptVideoDevice->iHeight  = tV4l2Fmt.fmt.pix.height;

    /* 5 申请buffer */
    memset(&tV4l2RstBuf, 0, sizeof(struct v4l2_requestbuffers));
    tV4l2RstBuf.count = NB_BUFFER;
    tV4l2RstBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2RstBuf.memory = V4L2_MEMORY_MMAP;

    iError = ioctl(iFd, VIDIOC_REQBUFS, &tV4l2RstBuf);
    if (iError) {
        DBG_PRINTF("Unable to allocate buffers.\n" );
        goto err_exit;
    }

    /* 6 根据不同的接口，处理方式不同，上面注释 */
    ptVideoDevice->iVideoBufCnt = tV4l2RstBuf.count;
    if(tV4l2Cap.capabilities & V4L2_CAP_STREAMING)
    {
        /* 6.1  向驱动查询申请到的内存 */
        for (i = 0; i < ptVideoDevice->iVideoBufCnt; i++) 
        {
            memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
            tV4l2Buf.index = i;
            tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            tV4l2Buf.memory = V4L2_MEMORY_MMAP;
            iError = ioctl(iFd, VIDIOC_QUERYBUF, &tV4l2Buf);
            if (iError) 
            {
                DBG_PRINTF("Unable to query buffer.\n");
                goto err_exit;
            }
            ptVideoDevice->iVideoBufMaxLen = tV4l2Buf.length;
            ptVideoDevice->pucVideBuf[i] = mmap(0 /* start anywhere */ ,
                    tV4l2Buf.length, PROT_READ, MAP_SHARED, iFd,
                      tV4l2Buf.m.offset);
            if (ptVideoDevice->pucVideBuf[i] == MAP_FAILED) 
            {
                DBG_PRINTF("Unable to map buffer \n");
                goto err_exit;
            }
        }
        /* 6.2 将空闲的内存加入可捕获视频的队列  */
        for (i = 0; i < ptVideoDevice->iVideoBufCnt; i++)
        {
            memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
            tV4l2Buf.index = i;
            tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            tV4l2Buf.memory = V4L2_MEMORY_MMAP;
            iError = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
            if (iError) 
            {
                DBG_PRINTF("Unable to queue buffer.\n");
                goto err_exit;
            }
        }
    }
    else if (tV4l2Cap.capabilities & V4L2_CAP_READWRITE)
    {
        g_tV4l2VideoOpr.GetFrame        = V4l2GetFrameForReadWrite;
        g_tV4l2VideoOpr.PutFrame        = V4l2PutFrameForReadWrite;

        ptVideoDevice->iVideoBufCnt     = 1;
        ptVideoDevice->iVideoBufMaxLen  = ptVideoDevice->iWidth * ptVideoDevice->iHeight * 4;
        ptVideoDevice->pucVideBuf[0]    = malloc(ptVideoDevice->iVideoBufMaxLen);
    }

    ptVideoDevice->ptOPr = &g_tV4l2VideoOpr;

    return 0;

err_exit:
    close(iFd);
    return -1;  
}

static int V4l2ExitDevice(PT_VideoDevice ptVideoDevice)
{
    int i;
    for (i = 0; i < ptVideoDevice->iVideoBufCnt; i++)
    {
        if (ptVideoDevice->pucVideBuf[i])
        {
            munmap(ptVideoDevice->pucVideBuf[i], ptVideoDevice->iVideoBufMaxLen);
            ptVideoDevice->pucVideBuf[i] = NULL;
        }
    }
        
    close(ptVideoDevice->iFd);
    return 0;
}

static int V4l2GetFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    int ret;
    
    struct pollfd tFds[1];
    int iRet;
    struct v4l2_buffer tV4l2Buf;
            
    /* poll */
    tFds[0].fd     = ptVideoDevice->iFd;
    tFds[0].events = POLLIN;

    iRet = poll(tFds, 1, -1);
    if (iRet <= 0)
    {
        DBG_PRINTF("poll error!\n");
        return -1;
    }

    memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
    tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2Buf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(ptVideoDevice->iFd, VIDIOC_DQBUF, &tV4l2Buf);
    if (ret < 0) 
    {
        DBG_PRINTF("Unable to dequeue buffer.\n");
        return -1;
    }

    /* 设置ptVideoDevice相关参数 */
    ptVideoDevice->iVideoBufCurIndex    = tV4l2Buf.index;
    ptVideoBuf->iPixelFormat            = ptVideoDevice->iPixelFormat;
    ptVideoBuf->tPixelDatas.iWidth      = ptVideoDevice->iWidth;
    ptVideoBuf->tPixelDatas.iHeight = ptVideoDevice->iHeight;
    ptVideoBuf->tPixelDatas.iBpp    = (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_YUYV) ? 16 : \
                                        (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_MJPEG) ? 0 :  \
                                        (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_RGB565) ? 16 :  \
                                        0;
    ptVideoBuf->tPixelDatas.iLineBytes    = ptVideoDevice->iWidth * ptVideoBuf->tPixelDatas.iBpp / 8;
    ptVideoBuf->tPixelDatas.iTotalBytes   = tV4l2Buf.bytesused;
    ptVideoBuf->tPixelDatas.aucPixelDatas = ptVideoDevice->pucVideBuf[tV4l2Buf.index];    

    return 0;
}

static int V4l2PutFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    /* VIDIOC_QBUF */
    struct v4l2_buffer tV4l2Buf;
    int iError;
    
	memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
	tV4l2Buf.index  = ptVideoDevice->iVideoBufCurIndex;
	tV4l2Buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	tV4l2Buf.memory = V4L2_MEMORY_MMAP;
	iError = ioctl(ptVideoDevice->iFd, VIDIOC_QBUF, &tV4l2Buf);
	if (iError) 
    {
	    DBG_PRINTF("Unable to queue buffer.\n");
	    return -1;
	}

    return 0;
}

static int V4l2GetFrameForReadWrite(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    int iRet;

    iRet = read(ptVideoDevice->iFd, ptVideoDevice->pucVideBuf[0], ptVideoDevice->iVideoBufMaxLen);
    if (iRet <= 0)
    {
        return -1;
    }
    
    ptVideoBuf->iPixelFormat        = ptVideoDevice->iPixelFormat;
    ptVideoBuf->tPixelDatas.iWidth  = ptVideoDevice->iWidth;
    ptVideoBuf->tPixelDatas.iHeight = ptVideoDevice->iHeight;
    ptVideoBuf->tPixelDatas.iBpp    = (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_YUYV) ? 16 : \
                                        (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_MJPEG) ? 0 :  \
                                        (ptVideoDevice->iPixelFormat == V4L2_PIX_FMT_RGB565)? 16 : \
                                          0;
    ptVideoBuf->tPixelDatas.iLineBytes    = ptVideoDevice->iWidth * ptVideoBuf->tPixelDatas.iBpp / 8;
    ptVideoBuf->tPixelDatas.iTotalBytes   = iRet;
    ptVideoBuf->tPixelDatas.aucPixelDatas = ptVideoDevice->pucVideBuf[0];

    return 0;
}

static int V4l2PutFrameForReadWrite(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    return 0;
}


static int V4l2StartDevice(PT_VideoDevice ptVideoDevice)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(ptVideoDevice->iFd, VIDIOC_STREAMON, &type);
    if (ret < 0) 
    {
        DBG_PRINTF("Unable to start Device.\n");
        return ret;
    }

    return 0;
}
    
static int V4l2StopDevice(PT_VideoDevice ptVideoDevice)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(ptVideoDevice->iFd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        DBG_PRINTF("Unable to stop Device.\n");
        return ret;
    }

    return 0;
}

static int V4l2GetFormat(PT_VideoDevice ptVideoDevice)
{
    return ptVideoDevice->iPixelFormat;
}


/* 构造一个VideoOpr结构体 */
static T_VideoOpr g_tV4l2VideoOpr = {
    .name        = "v4l2",
    .InitDevice  = V4l2InitDevice,
    .ExitDevice  = V4l2ExitDevice,
    .GetFormat   = V4l2GetFormat,
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


