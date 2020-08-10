
.
..........#include "sys.h"

//UCOSIII中以下优先级用户程序不能使用，ALIENTEK
//将这些优先级分配给了UCOSIII的5个系统内部任务
//优先级0：中断服务服务管理任务 OS_IntQTask()
//优先级1：时钟节拍任务 OS_TickTask()
//优先级2：定时任务 OS_TmrTask()
//优先级OS_CFG_PRIO_MAX-2：统计任务 OS_StatTask()
//优先级OS_CFG_PRIO_MAX-1：空闲任务 OS_IdleTask()

//任务优先级
#define START_TASK_PRIO		3
//任务堆栈大小	
#define START_STK_SIZE 		128
//任务控制块
OS_TCB StartTaskTCB;
//任务堆栈	
CPU_STK START_TASK_STK[START_STK_SIZE];
//任务函数
void start_task(void *p_arg);

/* 系统主任务 */
#define MAIN_TASK_PRIO									6
//任务堆栈大小32bit
#define CPU_STK_MAIN_SIZE								1000
//时间片长度
#define MAIN_TICK_LEN									0
static  OS_TCB											MainTaskTCB;
static	CPU_STK											MainTaskStk[CPU_STK_MAIN_SIZE];
static void  MainTask(void* p_arg);



/* 接收MQTT指令 */
#define MQTTRECEIVE_PRIO								7
//任务堆栈大小32bit
#define CPU_STK_MQTTRECEIVE_SIZE						1000
//时间片长度
#define MQTTRECEIVE_TICK_LEN							0
static  OS_TCB											MQTTRECEIVEtcb;
static	CPU_STK											MQTTRECEIVEstk[CPU_STK_MQTTRECEIVE_SIZE];
static void  MQTTRECEIVEtask(void* p_arg);

/* 发送MQTT指令 */
#define SIM7100_PRIO									8
//任务堆栈大小32bit
#define CPU_STK_SIM7100_SIZE							1000
//时间片长度
#define SIM7100_TICK_LEN								0
static  OS_TCB											SIM7100tcb;
static	CPU_STK											SIM7100stk[CPU_STK_SIM7100_SIZE];
static void  SIM7100task(void* p_arg);

/* Watch task */
#define HELLO_LED_PRIO									10
#define CPU_STK_HELLO_LED_SIZE							2000
#define HELLO_LED_TICK_LEN								0
static  OS_TCB											HELLO_LEDtcb;
static	CPU_STK											HELLO_LEDstk[CPU_STK_HELLO_LED_SIZE];
static void  SysWatchTask(void* p_arg);

#define  SystemDatasBroadcast_PRIO                      11 // 统计任务优先级最低，我这里是12，已经低于其他任务的优先级了
#define  SystemDatasBroadcast_STK_SIZE                  200 // 任务的堆栈大小，做统计一般够了，统计结果出来后不够再加..
#define  SystemDatasBroadcast_LED_TICK_LEN              0
static   OS_TCB                                         SystemDatasBroadcast_TCB;		                // 定义统计任务的TCB
static   CPU_STK                                        SystemDatasBroadcast_STK [SystemDatasBroadcast_STK_SIZE];// 开辟数组作为任务栈给任务使用
static void  SystemDatasBroadcast (void *p_arg);

void SoftReset(void)
{
    __set_FAULTMASK(1); // 关闭所有中断
    NVIC_SystemReset(); // 复位
}

//bit0:表示电脑正在向SD卡写入数据
//bit1:表示电脑正从SD卡读出数据
//bit2:SD卡写数据错误标志位
//bit3:SD卡读数据错误标志位
//bit4:1,启动ping任务
//bit5:保留.
//bit6:保留.
//bit7:保留.

struct cycle_package cycle;
struct flash_package eerom;
#define FLASH_WRITE_MODE 0
// vu16 watchdog_f;
vu16 function_f;
vu16 function_f2;
vu16 ec25_on_flag;
vu16 key_on_flag;
vu16 led_on_flag;
#include "queue.h" 
#include "rng.h"

/////////////////systerm parameters//////////
vu16 sensor_frequency = CYCLE_TIME;
vu16 camera_frequency = TASK_T_P_CNT;
vu16 upload_frequency = TASK_S_D_CNT;
vu16 transfer_photo_frequency = TASK_S_P_CNT;
vu16 voltage_fuse_threshold = TD_B_V_VAL;
vu16 current_fuse_threshold = TD_C_C_VAL;
vu16 hardwork_min = TD_C_H_S;
vu16 hardwork_max = TD_C_H_E;
vu16 max_work_length = MAX_RUN_TIME;
////////////////////////////////////////////
void system_init(void)
{
	u8 res;
	// local  carious
	u32 now_time;
	int time_delta;
	// global various
	//watchdog_f=0;
	function_f = 0;  // 任务执行标志清零
	function_f2 = 0;
	ec25_on_flag = 0;
	key_on_flag = 0; // 任务执行标志清零
	led_on_flag = 0;
	u8 m_buf[100];
	u16 m_value[9];
	//u16 *m_value;
	// systerm initial
	delay_init(168);  	// 时钟初始化
	KEY_Init();	  		// key init
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);// 中断分组配置
	uart_init(921600); 	// 串口初始化
	printf("\r\n\r\n\r\n>>>>>>>>>>>>>>systerm start>>>>>>>>>>>>>>\r\n");
	InitQueue(&Q_stage);  // 初始化队列 
	InitQueue(&Q_resent);  // 初始化队列 	
	IWDG_Init(IWDG_Prescaler_256,4000); //4,000*256/32,000=32s
	My_RTC_Init();  //初始化RTC
	calendar_get_time(&calendar);
	calendar_get_date(&calendar);
	printf("*DATA:%d-%d-%d	Time:%d:%d:%d\r\n",calendar.w_year,calendar.w_month,calendar.w_date,calendar.hour,calendar.min,calendar.sec);
    local_time_cnt = calendar.sec;  // 用于看门狗统计
	// updata sys parameters
	// 打开SD开
	// 读取数据
	// 解析数据buf,len
	// flash
	mf_config_data_read_flash(m_buf);
	res = analyze_config_para((char *)m_buf,m_value);
	if(res==0) // 有意义
	{
		sensor_frequency = m_value[0];
		camera_frequency = m_value[1]/sensor_frequency;
		upload_frequency = m_value[2]/sensor_frequency;
		transfer_photo_frequency = m_value[3]/sensor_frequency;
		voltage_fuse_threshold = m_value[4];
		current_fuse_threshold = m_value[5];
		hardwork_min = m_value[6];
		hardwork_max = m_value[7];
		max_work_length = m_value[8];
		printf("*analyze_config_para update finish^^^^^\r\n");
	}
	else
	{
		sensor_frequency = CYCLE_TIME;
		camera_frequency = TASK_T_P_CNT / sensor_frequency;
		upload_frequency = TASK_S_D_CNT / sensor_frequency;
		transfer_photo_frequency = TASK_S_P_CNT/sensor_frequency;
		voltage_fuse_threshold = TD_B_V_VAL;
		current_fuse_threshold = TD_C_C_VAL;
		hardwork_min = TD_C_H_S;
		hardwork_max = TD_C_H_E;
		max_work_length = MAX_RUN_TIME;
		printf("!analyze_config_para error\r\n");
	}
	//SD卡
	/*
	res = mf_config_data_read(m_buf);
	// updata 最新版
	if(!res)
	{
		m_value = analyze_config_para((char *)m_buf);
		if(m_value) // 有意义
		{
			sensor_frequency = m_value[0];
			camera_frequency = m_value[1]/sensor_frequency;
			upload_frequency = m_value[2]/sensor_frequency;
			transfer_photo_frequency = m_value[3]/sensor_frequency;
			voltage_fuse_threshold = m_value[4];
			current_fuse_threshold = m_value[5];
			hardwork_min = m_value[6];
			hardwork_max = m_value[7];
			max_work_length = m_value[8];
			printf("*analyze_config_para\r\n");
		}
		else
		{
			printf("!analyze_config_para error\r\n");
		}
	}
	*/	
	// sleep mode
	#if FLASH_WRITE_MODE
	cycle.time_stamp=get_time_cnt();
	cycle.task_cnt =0;
	cycle.function=0;
	cycle.picture_id=2717;
	cycle.watch_cnt=30;
	STMFLASH_Write(FLASH_SAVE_ADDRC1,(u32 *)&cycle,sizeof(cycle)/4);	// 将初始化参数写入寄存器
	while(1);

	//	now_time = get_time_cnt();
	//	if(now_time<cycle.time_stamp)
	//		now_time+=3600;
	//	time_delta = now_time - cycle.time_stamp;
	//	printf("cycle2:%d,%d,%d,%d,%d,%d,%x\r\n",time_delta,now_time,cycle.time_stamp,cycle.picture_id,cycle.task_cnt,cycle.watch_cnt,cycle.function);

	//	cycle.time_stamp=get_time_cnt(); // 更新时间戳
	//	cycle.task_cnt ++;
	//	cycle.function=0;
	//	printf("cycle3:%d,%d,%d,%d,%d,%d,%x\r\n",time_delta,now_time,cycle.time_stamp,cycle.picture_id,cycle.task_cnt,cycle.watch_cnt,cycle.function);
	//	STMFLASH_Write(FLASH_SAVE_ADDRC1,(u32 *)&cycle,sizeof(cycle)/4);
	#else
	// printf("*cycle data:%d,%d,%d,%d,%x\r\n",cycle.time_stamp,cycle.picture_id,cycle.task_cnt,cycle.watch_cnt,cycle.function);
	#endif
	// 处理休眠机制
	#if SLEEP_MODE
	key_scan_fun();
	res = KEY_Scan(20);
	switch(res)
	{
		case KEY1_PRES:
			key_on_flag = 1;
			printf("$!!!force to execute the task!!!\r\n");
			break;
		case KEY2_PRES:
			function_f2=1;
			ec25_on_flag=1;
			printf("$!!!force not to sleep!!!\r\n");
			break;
		default:
			printf("$normal start\r\n");
			break;
	}
	// 有效数据
	STMFLASH_Read(FLASH_SAVE_ADDRC1,(u32 *)&cycle,sizeof(cycle)/4);
	printf("*info:STMFLASH_Read|time_stamp:%d,task_cnt:%d\r\n",cycle.time_stamp,cycle.task_cnt);
	now_time = get_time_cnt();
	time_delta = now_time - cycle.time_stamp;  // 正常时>0,或者now_time+3600- cycle.time_stamp>0 ，否则异常，更新时间戳时间
	printf("*info:count down|next statr time:{(T:%d) %d}\r\n",sensor_frequency,sensor_frequency-time_delta);
	if(!key_on_flag && !function_f2)  // 休眠 
	{
		if(time_delta <0)
			time_delta+=3600;
		if(time_delta> sensor_frequency || time_delta<0)  // 符合条件,加上小于零的判断，方便过滤错误
		{
			cycle.time_stamp=now_time;  // 更新时间戳
			cycle.task_cnt ++;		
			STMFLASH_Write(FLASH_SAVE_ADDRC1,(u32 *)&cycle,sizeof(cycle)/4);
			printf("*info:STMFLASH_Write|time_stamp:%d,task_cnt:%d\r\n",cycle.time_stamp,cycle.task_cnt);
		}
		else 
		{
			printf("*watchdog sleep\r\n");
			if((sensor_frequency-time_delta)>STANDBY_TIME)
				Sys_Enter_Standby(STANDBY_TIME);
			else
				Sys_Enter_Standby(sensor_frequency-time_delta);
		}
	}
	else
	{
		cycle.time_stamp=now_time;  // 更新时间戳
		cycle.task_cnt ++;		
		STMFLASH_Write(FLASH_SAVE_ADDRC1,(u32 *)&cycle,sizeof(cycle)/4);
		printf("*info:STMFLASH_Write|time_stamp:%d,task_cnt:%d\r\n",cycle.time_stamp,cycle.task_cnt);
		printf("$!!!force to execute the task,can't sleep!!!\r\n");
	}
	
	#endif

	#if USB_MODE
	usbapp_init();
	USBH_Init(&USB_OTG_Core,USB_OTG_FS_CORE_ID,&USB_Host,&USBH_MSC_cb,&USR_Callbacks);
	delay_ms(1000);
	#endif
    if (!SD_Init())
	{
		printf("*SD_Init ok\r\n"); //判断SD卡是否存在
	}
	else
        printf("*SD_Init Error\r\n");
	mymem_init(SRAMIN);      //初始化内部内存池
	exfuns_init();           //为fatfs相关变量申请内存
	f_mount(fs[0], "0:", 1); //挂载SD卡
	#if EN_log_sd
	mf_log_init();			 //初始化日志
	#endif
	#if USB_MODE
	f_mount(fs[1], "1:", 1); //挂载U盘
	#endif	
	printf("*****************************************\r\n");
	printf("**************systerm enter**************\r\n");
	printf("*****************************************\r\n");
 	printf("--------sys value--------\r\n");
	printf("sensor_frequency        :%d\r\n",sensor_frequency);
	printf("camera_frequency        :%d\r\n",camera_frequency);
	printf("upload_frequency        :%d\r\n",upload_frequency);
	printf("transfer_photo_frequency:%d\r\n",transfer_photo_frequency);
	printf("voltage_fuse_threshold  :%d\r\n",voltage_fuse_threshold);
	printf("current_fuse_threshold  :%d\r\n",current_fuse_threshold);
	printf("hardwork_min            :%d\r\n",hardwork_min);
	printf("hardwork_max            :%d\r\n",hardwork_max);
	printf("max_work_length         :%d\r\n",max_work_length);
	printf("-------------------------\r\n");
	
	Power_Ctrl_Init(); // 电源初始化	
	Relay_Init(); // 继电器初始化
	// sys hardware
	rng_Init();	
	LED_Init();   // LED init
	mqtt_UID_set();
	#if SENSOR_MODE
	SHT2x_Init();  //SHT20初始化
	max44009_initialize();  //MAX44009初始化
	MS5611_Init();  //MS5611初始化
	USART2_init(9600); // 电池数据端口初始化
	Cam_Crtl_Init();   // 相机控制引脚初始化
	#endif

	// 任务解析
	printf("-------------------------\r\n");
	printf("#Task analysis...........\r\n");
	printf("$task count:%d\r\n", cycle.task_cnt);
	
	function_f|=(0x01);  // 获取数据
	printf("$ins:get data\r\n");

	if(cycle.task_cnt%camera_frequency==0 || key_on_flag) 
	{
		function_f|=(0x02);  // 拍照
		printf("$ins:take photo\r\n");		
	}
	if(cycle.task_cnt%transfer_photo_frequency==0 || key_on_flag) 
	{
		function_f|=(0x04);  // 转存照片	
		printf("$ins:store photo\r\n");
	}
				
	if(cycle.task_cnt%upload_frequency==0 || key_on_flag)  // 发送数据
	{
		u8 i=0;
		printf("$ins:try to send data\r\n");
		while(i++<5 && battery_data_get()==0)
		{
			printf("*info:try to get battery data, cnt:{(Max:5) %d}\r\n",i);
		}
//		if(battery.total_voltage >= voltage_fuse_threshold || battery.total_voltage == -9999)
//		{
//			openOutputLoad();
//		}
//		else
//		{
//			closeOutputload();
//			//closeReLoad();
//		}
		if((battery.charge_current)*10 >=current_fuse_threshold  || (battery.total_voltage)*2 >=voltage_fuse_threshold)
		{
			ec25_on_flag=1;
			printf("*info:battery ok|current:%d|total_voltage:%d|send data\r\n",(int)battery.charge_current*10,(int)battery.total_voltage*2);
		}
		else
		{
			printf("*info:battery error,charge_current:{(T:%d)>%d},total_voltage:{(T:%d)>%d}\r\n",\
			(int)current_fuse_threshold,(int)battery.charge_current*10,(int)voltage_fuse_threshold,(int)battery.total_voltage*2);
		}
		calendar_get_time(&calendar);
		if(calendar.hour>=hardwork_min && calendar.hour<=hardwork_max)
		{
			ec25_on_flag=1;
			printf("*info:time(%d->%d) ok,calendar.hour:%d send data\r\n",hardwork_min,hardwork_max,calendar.hour);
		}
		else
		{
			printf("*info:time(%d->%d) error,calendar.hour:%d\r\n",hardwork_min,hardwork_max,calendar.hour);
		}
		if(key_on_flag)
		{
			ec25_on_flag=1;
			printf("*info:force to execute the task|ec25_on_flag=1");
		}
		if(ec25_on_flag)
		{
			function_f|=(0x10);  // 发送数据	
			printf("$ins:send data\r\n");
			function_f|=(0x20);  // 发送图片
			printf("$ins:send photo\r\n");	
		}			
	}
	printf("$function=%x\r\n",function_f);
	printf("-------------------------\r\n\r\n");
	IWDG_Feed();//喂狗
}

// 分析参数
// return:
// 0:分析成功
// 100：不需要更新
// 其它：数据异常
u8 analyze_config_para(char *buf, u16 * val)
{
	u8 res;
	// 中间变量
	u8 offset;
	//static u16 val[9];
	
	// 判断是否为更新变量
	if(buf[0]=='0')
	{
		//config_flag = 1;
		res=100;
		printf("*info:analyze_config_para|the data is latest, no need to updata!\r\n");
		goto an_end;
	}
	// 分析控制参数
	printf("#analyze_config_para .......\r\n");
	offset=0;
	val[0] = stringtoNum(buf);
	printf("%02dsensor_frequency        :%d\r\n",offset,val[0]);
	if(val[0]>10800|| val[0]<60)
	{
		printf("*!analyze_config_para error:val[0]=%d\r\n",val[0]);
		res=1;
		goto an_end;
	}	
	offset += locate_character(buf+offset, '|');
	val[1] = stringtoNum(buf+offset);
	printf("%02dcamera_frequency        :%d\r\n",offset,val[1]);
	
	if(val[1]<val[0] || val[1]>43200)
	{
		printf("*!analyze_config_para error:val[1]=%d\r\n",val[1]);
		res=2;
		goto an_end;
	}
	offset += locate_character(buf+offset, '|');
	val[2] = stringtoNum(buf+offset);
	printf("%02dupload_frequency        :%d\r\n",offset,val[2]);
	if(val[2]<val[0])
	{
		printf("*!analyze_config_para error:val[2]=%d\r\n",val[2]);
		res=3;
		goto an_end;
	}
	offset += locate_character(buf+offset, '|');
	val[3] = stringtoNum(buf+offset);
	printf("%02dtransfer_photo_frequency:%d\r\n",offset,val[3]);
	if(val[3]<val[0])
	{
		printf("*!analyze_config_para error:val[3]=%d\r\n",val[3]);
		res=4;
		goto an_end;
	}
	offset += locate_character(buf+offset, '|');
	val[4] = stringtoNum(buf+offset);
	printf("%02dvoltage_fuse_threshold  :%d\r\n",offset,val[4]);
	offset += locate_character(buf+offset, '|');
	val[5] = stringtoNum(buf+offset);
	printf("%02dcurrent_fuse_threshold  :%d\r\n",offset,val[5]);
	offset += locate_character(buf+offset, '|');
	val[6] = stringtoNum(buf+offset);
	printf("%02dhardwork_min            :%d\r\n",offset,val[6]);
	if(val[6]>24)
	{
		printf("*!analyze_config_para error:val[6]=%d\r\n",val[6]);
		res=7;
		goto an_end;
	}
	offset += locate_character(buf+offset, '|');
	val[7] = stringtoNum(buf+offset);
	printf("%02dhardwork_max            :%d\r\n",offset,val[7]);
	if(val[7]<=val[6])
	{
		printf("*!analyze_config_para error:val[7]=%d\r\n",val[7]);
		res=8;
		goto an_end;
	}
	offset += locate_character(buf+offset, '|');
	val[8] = stringtoNum(buf+offset);	
	printf("%02dmax_work_length         :%d\r\n",offset,val[8]);
	if(val[8]>2400 || val[8]<180)  // 最大40min,最小3min
	{
		printf("*!analyze_config_para error:val[8]=%d\r\n",val[8]);
		res=9;
		goto an_end;
	}	
	res=0;
	an_end:
	return res;
}

//主函数
int main(void)
{
	OS_ERR err;
	CPU_SR_ALLOC();	
	system_init();		//系统初始化 
	OSInit(&err);		//初始化UCOSIII
	OS_CRITICAL_ENTER();//进入临界区			 
	//创建开始任务
	OSTaskCreate((OS_TCB 	* )&StartTaskTCB,		//任务控制块
				 (CPU_CHAR	* )"start task", 		//任务名字
                 (OS_TASK_PTR )start_task, 			//任务函数
                 (void		* )0,					//传递给任务函数的参数
                 (OS_PRIO	  )START_TASK_PRIO,     //任务优先级
                 (CPU_STK   * )&START_TASK_STK[0],	//任务堆栈基地址
                 (CPU_STK_SIZE)START_STK_SIZE/10,	//任务堆栈深度限位
                 (CPU_STK_SIZE)START_STK_SIZE,		//任务堆栈大小
                 (OS_MSG_QTY  )0,					//任务内部消息队列能够接收的最大消息数目,为0时禁止接收消息
                 (OS_TICK	  )0,					//当使能时间片轮转时的时间片长度，为0时为默认长度，
                 (void   	* )0,					//用户补充的存储区
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR, //任务选项
                 (OS_ERR 	* )&err);				//存放该函数错误时的返回值
	OS_CRITICAL_EXIT();	// 退出临界区	 
	OSStart(&err);      // 开启UCOSIII
}


//开始任务函数
void start_task(void *p_arg)
{
	OS_ERR err;
	CPU_SR_ALLOC();
	p_arg = p_arg;
	
	CPU_Init();
#if OS_CFG_STAT_TASK_EN > 0u
   OSStatTaskCPUUsageInit(&err);  	//统计任务                
#endif
	
#ifdef CPU_CFG_INT_DIS_MEAS_EN		//如果使能了测量中断关闭时间
    CPU_IntDisMeasMaxCurReset();	
#endif
	
#if	OS_CFG_SCHED_ROUND_ROBIN_EN  //当使用时间片轮转的时候
	 //使能时间片轮转调度功能,时间片长度为1个系统时钟节拍，既1*5=5ms
	OSSchedRoundRobinCfg(DEF_ENABLED,1,&err);  
#endif		
	
	OS_CRITICAL_ENTER();	//进入临界区
    // stackMonitoring
    OSTaskCreate((OS_TCB *)&SystemDatasBroadcast_TCB,
                 (CPU_CHAR *)"SystemDatasBroadcast",
                 (OS_TASK_PTR)SystemDatasBroadcast,
                 (void *)0,
                 (OS_PRIO)SystemDatasBroadcast_PRIO,
                 (CPU_STK *)&SystemDatasBroadcast_STK[0],
                 (CPU_STK_SIZE)SystemDatasBroadcast_STK_SIZE / 10, /*栈溢出临界值我设置在栈大小的90%处*/
                 (CPU_STK_SIZE)SystemDatasBroadcast_STK_SIZE,
                 (OS_MSG_QTY)0,
                 (OS_TICK)0,
                 (void *)0,
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                 (OS_ERR *)&err);
	//
    OSTaskCreate((OS_TCB *)&MQTTRECEIVEtcb,
                 (CPU_CHAR *)"MQTTRECEIVE",
                 (OS_TASK_PTR)MQTTRECEIVEtask,
                 (void *)0,
                 (OS_PRIO)MQTTRECEIVE_PRIO,
                 (CPU_STK *)&MQTTRECEIVEstk[0],
                 (CPU_STK_SIZE)CPU_STK_MQTTRECEIVE_SIZE / 10,
                 (CPU_STK_SIZE)CPU_STK_MQTTRECEIVE_SIZE,
                 (OS_MSG_QTY)0,
                 (OS_TICK)MQTTRECEIVE_TICK_LEN,
                 (void *)0,
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                 (OS_ERR *)&err);
	if(ec25_on_flag==1)
	{
    //
    OSTaskCreate((OS_TCB *)&SIM7100tcb,
                 (CPU_CHAR *)"SIM7100",
                 (OS_TASK_PTR)SIM7100task,
                 (void *)0,
                 (OS_PRIO)SIM7100_PRIO,
                 (CPU_STK *)&SIM7100stk[0],
                 (CPU_STK_SIZE)CPU_STK_SIM7100_SIZE / 10,
                 (CPU_STK_SIZE)CPU_STK_SIM7100_SIZE,
                 (OS_MSG_QTY)0,
                 (OS_TICK)SIM7100_TICK_LEN,
                 (void *)0,
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                 (OS_ERR *)&err);
	 }
    // watchTask
    OSTaskCreate((OS_TCB *)&HELLO_LEDtcb,
                 (CPU_CHAR *)"HELLO_LED",
                 (OS_TASK_PTR)SysWatchTask,
                 (void *)0,
                 (OS_PRIO)HELLO_LED_PRIO,
                 (CPU_STK *)&HELLO_LEDstk[0],
                 (CPU_STK_SIZE)CPU_STK_HELLO_LED_SIZE / 10,
                 (CPU_STK_SIZE)CPU_STK_HELLO_LED_SIZE,
                 (OS_MSG_QTY)0,
                 (OS_TICK)HELLO_LED_TICK_LEN,
                 (void *)0,
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                 (OS_ERR *)&err);
				 
    OSTaskCreate((OS_TCB *)&MainTaskTCB,                              // 该任务堆栈的开始地址
                 (CPU_CHAR *)"MainTask",                              // 任务分配名字
                 (OS_TASK_PTR)MainTask,                               // 指向任务代码的指针
                 (void *)0,                                           // 指针，第一次执行任务时传递给，任务实体的指针参数*p_arg
                 (OS_PRIO)MAIN_TASK_PRIO,                             // 优先级设置	 参数值越小优先级越高
                 (CPU_STK *)&MainTaskStk[0],                          // 任务堆栈的基地址。基地址通常是分配给该任务的堆栈的最低内存位置
                 (CPU_STK_SIZE)CPU_STK_MAIN_SIZE / 10,                // 第七个参数是地址“水印” ，当堆栈生长到指定位置时就不再允许其生长
                 (CPU_STK_SIZE)CPU_STK_MAIN_SIZE,                     // 任务的堆栈大小
                 (OS_MSG_QTY)0,                                       //
                 (OS_TICK)MAIN_TICK_LEN,                              // 设置任务拥有多少个时间片，当采用时间片轮询调度任务时有效
                 (void *)0,                                           //
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR), //
                 (OS_ERR *)&err);      
				 
	OS_CRITICAL_EXIT();	//退出临界区
	OSTaskDel((OS_TCB*)0,&err);	//删除start_task任务自身
}

static u8 local_memdevflag=0;  
void  SysWatchTask(void *pdata)
{
	OS_ERR err;
	u16 t;
	printf("SysWatchTask running!!!!!\r\n");
	delay_ms(500);
	while(1)
	{
		t++;
		#if USB_MODE
		// USB开关打开后，方执行下属的USB扫描程序
		if(usbConnectSwitchGet())
		{
			if((t%60)==0)
			{
				if(usbx.hdevclass==1)
				{
					if(local_memdevflag==0)
					{
						fs[1]->drv=2;  // 暂时认为也可以不加,我认为可能是重命名
						f_mount(fs[1],"1:",1); 	// 重新挂载U盘
						usbapp_user_app();

						usbConnectStateSet(1);						
						local_memdevflag=1;
					}  
				}
				else 
					local_memdevflag=0; // U盘被拔出了
			}
			
			while((usbx.bDeviceState&0XC0)==0X40) // USB设备插入了,但是还没连接成功,猛查询.
			{
				usbapp_pulling();  // 轮询处理USB事务
				delay_ms(1);  // 不能像HID那么猛...,U盘比较慢
				#if (EN_log_print >= 2)
				printf(".");
				#endif // EN_log_print
			}
			usbapp_pulling();  // 检测USB
			#if (EN_log_print >= 2)
			printf("\\");
			#endif // EN_log_print
		}
		#endif
		UART_TCPbuff_Run(F407USART3_buffRead);  // 循环读取U3中缓存数据
		//key_scan_fun();
		OSTimeDly(5,OS_OPT_TIME_DLY,&err);
	}									 
}

// 扫描转存相机中的文件
void act_scan_camera(void)
{
	u8 res;
	// 初始化USB
	// 打开相机开关，对应于watch_task任务开始扫描USB
	LED_YELLOW_ON();
	printf("->\r\n$act:act_scan_camera...\r\n");
	delay_ms(1000);
	delay_ms(1000);
	LED_YELLOW_OFF();
	local_memdevflag=0;
	usbConnectSwitchSet(1);  
	usbapp_mode_set();  // 设置设置USB模式，复位USB
	usbConnectStateSet(0);  // 清空标志位
	openUSB();  // 打开相机的USB可电源
	// 等待连接成功
	IWDG_Feed();
	res = waitUsbConnectFlag(10000);
	if (res == 1) // 正常打开相机
	{
		printf("successful find usb，open camera!\r\n");
	}
	else
	{
		F407USART1_SendString("Fail WaitDistinguishflag...\r\n");
	}
	
	printf("*try to scan usb，open camera!\r\n");
	delay_ms(2000);  // 等待相机稳定
	mf_dcopy("1:DCIM/100IMAGE","0:INBOX",1);
	mf_scan_files("1:DCIM/100IMAGE");
	mf_scan_files("0:INBOX");
	closeUSB();  // close usb power
	IWDG_Feed();
	res = waitUsbDisonnectFlag(5000);
	if (res == 1) // 正常关闭相机
	{
		F407USART1_SendString("success closeUSB...\r\n");
	}
	else
	{
		F407USART1_SendString("closeUSB..fail\r\n");
	}
	IWDG_Feed();
	usbapp_mode_stop();
	usbConnectSwitchSet(0);
	delay_ms(1000);
}
// 拍照
void act_tale_photo(void)
{
	LED_YELLOW_ON();
	// 判断相机状态,如果相机处于连接状态，则跳过
	F407USART1_SendString("->\r\n$act:act_take_photo...\r\n");
	delay_ms(1000);
	delay_ms(1000);
	LED_YELLOW_OFF();
	if(usbConnectSwitchGet() == 0)
	{
		USB_Photograph();
	}
	else
	{
		printf("*Error, Camera is connected\r\n");
	}
	delay_ms(1000);
}




// 发送图片
u8 act_send_picture(void)
{
	F407USART1_SendString("->\r\n$act:act_send_picture...\r\n");
	delay_ms(1000);
	delay_ms(1000);
	delay_ms(1000);	
	printf("*mf_scan_files|befor \r\n");
	mf_scan_files("0:INBOX");  // 扫描文件夹
	mf_send_pics("0:INBOX","0:ARCH",1);  // 发送图片
	printf("*mf_scan_files|arter\r\n");
	mf_scan_files("0:INBOX");  // 扫描文件架
	return 1;
}

// <s>MainTask
// 返回0表示成功
u8 check_uart_commamd(u8*buf)
{
	//u8 command;
	u8 res=0;
	printf("$check_uart_commamd:");
	if(buf[0]=='{' && buf[2]==':' && buf[4]=='}')
	{
		switch(buf[1])
		{
			case 'C':
				switch(buf[3])	
				{
					case '0':
						function_f=0;  // 强制休眠
						printf("#$ins:force to sleep\r\n");	
						break;
					case '1':
						function_f|=(0x01);  // 获取数据
						printf("#$ins:get data\r\n");
						break;
					case '2':
						function_f|=(0x02);  // 拍照
						printf("#$ins:take photo\r\n");	
						break;
					case '3':
						function_f|=(0x04);  // 转存照片
						printf("#$ins:store photo\r\n");
						break;
					case '4':
						function_f|=(0x10);  // 发送数据	
						printf("#$ins:send data\r\n");
						break;
					case '5':
						function_f|=(0x20);  // 发送图片
						printf("#$ins:send photo\r\n");	
						break;
					default:
						res=1;
						printf("!!!check_uart_commamd error parameter\r\n");	
						break;
				}
				break;
				
			case 'K':
				switch(buf[3])	
				{
					case '0':
						function_f2=0;  // 启动休眠
						printf("#$ins:ready to sleep\r\n");	
						break;
						
					case '1':
						function_f2=1;  // 关闭休眠
						printf("#$ins:not sleep\r\n");	
						break;
					case '2':
						printf("#$ins:Request to get parameters\r\n");
						if (mqtt_state_get() == 1)
						{
							printf("$mysend_config-QUERRY CONFIG~~~~~\r\n");
							mysend_config("0");  // 获取参数
						}
						else
						{
							printf("!!!network error\r\n");
						}
						break;
						case '3':
						printf("#$ins:open indicator led\r\n");
						led_on_flag=1;
						break;
					case '4':
						printf("#$ins:close indicator led\r\n");
						led_on_flag=0;
						LED_BLUE_OFF();
						LED_YELLOW_OFF();
						LED_GREEN_OFF();
						break;
					default:
						res=2;
						printf("!!!check_uart_commamd error parameter\r\n");	
						break;
				}
				break;
			default:
				res=3;
				printf("!!!check_uart_commamd error command\r\n");	
				break;
		}
	}
	else
	{
		res=4;
		printf("!!!check_uart_commamd error format\r\n");	
	}
	return res;
}

void openUSB(void);
void closeUSB(void);

static void MainTask(void *p_arg) // test fun
{
	
    OS_ERR err;
    //u8 res;
	F407USART1_SendString("MainTask run\r\n");
	delay_ms(500);  
	while (1)
    {
		////////////////check uart///////////////////////
		if(USART_RX_STA &= 0x8000)
		{
			check_uart_commamd(USART_RX_BUF);
			USART_RX_STA = 0;
		}
		// 不需要联网
		// MQTT联网状态执行的任务
		
		// 不需要联网 或者 联网成功
		if((ec25_on_flag==0) || (mqtt_state_get() == 1)) 
		{
			if(function_f&0x01 )  // 获取数据
			{
				IWDG_Feed();				
				act_get_data();
				function_f&=(~0x01);
				printf("*finish act_get_data , fun:%x~~~~~\r\n",function_f);
			}
			else if(function_f&0x02 )  // 拍照
			{
				IWDG_Feed();
				act_tale_photo();
				delay_ms(1000);
				function_f&=(~0x02);
				printf("*fun:%x\r\n",function_f);
			}
			else if(function_f&0x04 )  // 转存
			{
				IWDG_Feed();				
				act_scan_camera();
				delay_ms(1000);
				function_f&=(~0x04);
				printf("*finish act_scan_camera, fun:%x~~~~~\r\n",function_f);
			}			
		}
		// 联网成功方可执行
		if(mqtt_state_get() == 1 && !(function_f&0x0F))  // 先执行基础任务再执行发送任务
		{	
			if(function_f&0x10)  // 发送传感器
			{
				IWDG_Feed();
				act_send_data();
				printf("*finish act_send_data, fun:%x~~~~~\r\n",function_f);
				delay_ms(1000);
				mf_send_log();
				delay_ms(1000);
				function_f&=(~0x10); 
				printf("*finish act_send_log, fun:%x~~~~~\r\n",function_f);
			}
			
			else if(function_f&0x20)  // 发送图片
			{
				IWDG_Feed();
				act_send_picture();
				delay_ms(1000);
				function_f&=(~0x20); 
				printf("*finish act_send_picture, fun:%x~~~~~\r\n",function_f);
			}
		}
        OSTimeDly(10, OS_OPT_TIME_DLY, &err);
    }
}
// <e>MainTask
// 提取参数，
// 校验 参数
// instan:"600|10800|3600|3600|12500|800|8|15|1200"
//mysend_data("01");
u8 check_config(u8 *load, u16 len)
{
	u8 res=0;
	// 中间变量
	u16 crc_cal = 0;
	u16 crc_rcv = 0;
	u16 msg_len=0;
	u16 value1[9];
	
	msg_len = (load[6] << 8) + load[7];
	if(msg_len < len)
	{
		crc_cal = ModBusCRC((uint8_t *)load+10, msg_len);
		crc_rcv = (load[8]<<8) + load[9];				
		if(crc_cal == crc_rcv)
		{
			//u8 result;
			res = analyze_config_para((char*)load+10, value1);

			if(res==0)
			{
				// 存储参数信息，字符串格式
				// 并没有进行updata,下次启动有效
				mf_config_data_write_flash(load+10);
				printf("*info:mf_config_data_write data={%s}\r\n",load+10);
				res=0;
			}
			else if(res==100)
			{
				printf("*info:config_flag|the config is latest, don't need to upgrade\r\n");
			}
			else
			{
				printf("*!warming:check_config|analyze_config_para error!\r\n");
			}
		}
		else
		{
			printf("*!warming:check_config|crc_cal error!crc_cal=%d,crc_rcv=%d\r\n",crc_cal,crc_rcv);
			res=101;
		}
	}
	else
	{
		printf("*!warming:check_config|msg_len error,msg_len=%d,len=%d\r\n",msg_len,len);
		res=102;
	}	
	return res;
}

u8 check_sever_config(u8 *load, u16 len)
{
	u8 res;
	// 中间变量
	u16 crc_cal = 0;
	u16 crc_rcv = 0;
	u16 msg_len=0;
	
	msg_len = (load[6] << 8) + load[7];
	//#test{
	len=msg_len+1;
	//#test}
	if(msg_len < len)
	{
		crc_cal = ModBusCRC((uint8_t *)load+10, msg_len);
		crc_rcv = (load[8]<<8) + load[9];				
		//#test{
		crc_cal = crc_rcv;
		//#test}
		if(crc_cal == crc_rcv)
		{
			res=check_uart_commamd(load+10);
			if(res==0)
				printf("*info:check_sever_config|succeed receive command\r\n");
			else
				printf("*!warming:check_sever_config|parameter error\r\n");
		}
		else
		{
			printf("*!warming:check_sever_config|crc_cal error!crc_cal=%d,crc_rcv=%d\r\n",crc_cal,crc_rcv);
			res=101;
		}
	}
	else
	{
		printf("*!warming:check_sever_config|msg_len error,msg_len=%d,len=%d\r\n",msg_len,len);
		res=102;
	}
	return res;
}

//	InitQueue(&Q_stage);  // 初始化队列 
//	InitQueue(&Q_resent);  // 初始化队列
void check_response(u8* load, int len)
{
	u8 res;
	
	u8 i,cnt;
	u32 uid=0;
	QElemType elem;
	if(load[0]==0xa5)
	{
		if(load[1]==0x96)
		{
			uid = (load[2]<<24) +  (load[3]<<16) + (load[4]<<8) +  load[5];
			// printf(">>>>>>>>>>>>>>>>>>>>>>>>R*UID:%0X\r\n",uid);
			printf(">>>>>RUID:[%3d,%3d,%3d],Q-UID:%08X,pid:%04X\r\n", 0, 0, 0, uid, 0XFFFF);
			// visit Q_stage squeue
			cnt=0;
			i=Q_stage.front;
			while(i!=Q_stage.rear && cnt++<90)
			{
				// printf("<VUID:%0X,%3d,pic=%3d\r\n",Q_stage.data[i].uid, i, Q_stage.data[i].pic_id);
				printf("<<<<<VUID:[%3d,%3d,%3d],Q-UID:%08X,pid:%04X\r\n", i,Q_stage.rear,(Q_stage.rear-i+MAXSIZE)%MAXSIZE,Q_stage.data[i].uid,Q_stage.data[i].pack_id);
				if(Q_stage.data[i].uid==uid)
				{
					while(--cnt)  //弹出所有的多多余的缓存
					{
						DeQueue(&Q_stage, &elem);
						printf("*-Q_stage:[%3d,%3d,%3d],Q-UID:%08X,pid:%04X\r\n",Q_stage.front,Q_stage.rear,QueueLength(Q_stage),elem.uid,elem.pack_id);
						EnQueue(&Q_resent,elem);
						//printf("*+Q_resnt:[%3d,%3d,%3d],Q+UID:%08X,pid:%04X\r\n", Q_resent.front, Q_resent.rear, QueueLength(Q_resent), elem.uid,elem.pack_id);	
						printf("* Q_resnt:[%3d,%3d,%3d],Q*UID:%08X,pid:%04X\r\n", Q_resent.front, Q_resent.rear, QueueLength(Q_resent), Q_resent.data[Q_resent.front].uid, Q_resent.data[Q_resent.front].pack_id);						
					}
					
					DeQueue(&Q_stage, &elem);
					printf("- Q_stage:[%3d,%3d,%3d],Q-UID:%08X,pid:%04X\r\n",Q_stage.front,Q_stage.rear,QueueLength(Q_stage),elem.uid,elem.pack_id);
					// printf("* Q_resnt:[%3d,%3d,%3d],Q+UID:%08X,pid:%04X\r\n",Q_resent.front,Q_resent.rear,QueueLength(Q_resent),elem.uid,elem.pack_id);
					printf("* Q_resnt:[%3d,%3d,%3d],Q*UID:%08X,pid:%04X\r\n", Q_resent.front, Q_resent.rear, QueueLength(Q_resent), Q_resent.data[Q_resent.front].uid, Q_resent.data[Q_resent.front].pack_id);
					break;		
				}
				i=(i+1)%MAXSIZE;			
			}

		}
		else if(load[1]==0x90)
		{
			u8 res;
			printf("\r\n*#check_config\r\n");
			res=check_config(load,len);
			if(res==0) // 成功存储
			{
				printf("*mysend_config--UPDATA~~~~~\r\n");
				mysend_config("1");
			}
			else if(res==100)
			{
				printf("*mysend_config--NO_CHANGE~~~~~\r\n");
				mysend_config("1");
			}
			else
			{
				printf("*!mysend_config--PARA_ERROR res:%d\r\n",res);
				mysend_config("2");  // 失败
			}
		}
		else if(load[1]==0x92)
		{	
			char buf[10];

			printf("\r\n*#check_sever_config\r\n");
			res=check_sever_config(load,len);
			if(res==0)
			{
				printf("*check_sever_config--UPDATA~~~~~\r\n");
			}
			else
			{
				printf("*!check_sever_config--ERROR\r\n");
			}
			sprintf(buf,"R%03d",res);
			mysend_config(buf);  // 获取参数	
		}
	}
}
// <e>MQTTRECEIVEtask
// 接收MQTT服务器的指令
static void MQTTRECEIVEtask(void *p_arg)
{
    OS_ERR err;
	int type; // 解析接收的数据值
    //===========================
    unsigned char dup;
    int qos;
    // 保留标志
    unsigned char retained;
    // 包id
    unsigned short packetid;
    // 主题名
    MQTTString topicName;
    // 数据
    unsigned char *payload;
    // 数据长度
    int payloadlen;
	//
	int test_cnt=0;
    //==============================
    u8 flag=1;  // 仅仅发送一次数据
	F407USART1_SendString("MQTTRECEIVEtask run\r\n");
	delay_ms(500);  

	while (1)
    {
        if (UART_TCP_buffLength() != 0)
        {
			#if (EN_log_print >= 3)
            F407USART1_SendString("+UART_TCP\r\n");
			#endif // EN_log_print	
            //处理接收到的MQTT消息，并根据不同的消息类型做不同的处理
            type = MQTTPacket_read(MQTT_Receivebuf, MQTT_RECEIVEBUFF_MAXLENGTH, transport_getdata);
            switch (type)
            {
				case CONNECT:
					break;
				case CONNACK:          //连接请求响应
					// mqtt_state_set(1); // 设置连接成功
					mqtt_subscribe(MY_TOPIC_MSGDOWN);
					printf("%s\r\n",MY_TOPIC_MSGDOWN);
					F407USART1_SendString("MQTT Connect CONNACK\r\n");
					break;
				case PUBLISH: //订阅的消息,由别人发布
					if (MQTTDeserialize_publish(&dup, &qos, &retained, &packetid, &topicName, &payload, &payloadlen,
												MQTT_Receivebuf, MQTT_RECEIVEBUFF_MAXLENGTH) == 1)
					{					
						#if (EN_log_print >= 3)
						int i;
						F407USART1_SendString("payload:");
						for (i = 0; i < payloadlen; i++)
						{
							printf("%0X",payload[i]);
							// 打印缓存区域的内容
						}
						F407USART1_SendString("\r\n");
						#endif // EN_log_print	
						// 理论上这里需要进行一定的检验
						check_response(payload,payloadlen);
					}
					break;
				case PUBACK: //发布消息响应，表示发布成功
					break;
				case PUBREC:
					break;
				case PUBREL:
					break;
				case PUBCOMP:
					break;
				case SUBSCRIBE:
					break;
				case SUBACK: //订阅消息ack	
					printf("MQTT subscrible SUBACK\r\n");
					mqtt_state_set(1); // 设置连接成功
					// querry data
					if(flag)
					{
						printf("$mysend_config-QUERRY CONFIG~~~~~\r\n");
						mysend_config("0");  // 获取参数
						flag=0;
					}
					break;
				case UNSUBSCRIBE:
					break;
				case UNSUBACK:
					break;
				case PINGREQ:
					break;
				case PINGRESP: //Ping返回·
					mqtt_ping_state_set(1);
					break;
				case DISCONNECT:
					break; //由客户端发送给服务器，表示正常断开连接
				default:
					break;
            }
        }
        OSTimeDly(5, OS_OPT_TIME_DLY, &err);
    }
}




static void SIM7100task(void *p_arg)
{
    OS_ERR err;
	EC25_ERR res;
    uint16_t time = 0;
	static u8 mqtt_connect_flag=0;
	static u8 f_reconnect=0; 
	
    F407USART1_SendString("SIM7100task run\r\n");
	delay_ms(500);
	
	res = ec25_Init();  // 初始化4G模组并联网
	if(res == EC25_ERR_NONE)
	{
		F407USART1_SendString("*EC25 ec25_Init succeed\r\n");
		ec25_SynLocalTime();
		ec25_QueeryCSQ();
		gpsx.gpssta = ec25_QueeryGPS();
	}
	else
	{
		F407USART1_SendString("*EC25 ec25_Init error\r\n");
	}
    while (1)
	{    // ? 这里的重连可能与发送时候的重连冲突    
        if (time % 5 == 0 && f_reconnect == 0)
        { //如果连接失败，每隔5秒会重新尝试连接一次
            if (mqtt_state_get() == 0 && ec25_on_flag == 1)  // 前提是4G联网成功
            {
				if(mqtt_connect_flag>=3)
				{
					printf("*!Try mqtt_connect f:%d, restart ec25\r\n",mqtt_connect_flag);
					ec25_Restart();
					
				}
				else if(mqtt_connect_flag>6)
				{
					ec25_on_flag = 0;
					function_f&=(~0x10); 
					function_f&=(~0x20); 
					printf("*forcus to close network\r\n");
				}
                mqtt_connect();
				printf("Try mqtt_connect f:%d\r\n",mqtt_connect_flag);
				
				mqtt_connect_flag++;  // 累计尝试连接次数				
            }
			else
			{

				mqtt_connect_flag=0;
			}
        }
		if (time % 10 == 1)  // mqtt ping
		{
			if (mqtt_state_get() == 1 && (function_f&0xf0) == 0)
            {
				u16 cnt=0;
				mqtt_Ping();
				printf("ping mqtt .....\r\n");
				while(!mqtt_ping_state_get())
				{
					cnt++;
					OSTimeDly(10,OS_OPT_TIME_DLY,&err);
					if(cnt>=500) // 5s
					{
						printf("none ping back .....\r\n");
						
						mqtt_state_set(0);

						break;
					}
				}
				printf("receive ping back .....\r\n");
				//mycheck_Queue();
				mqtt_ping_state_set(0);  // 清空标志位
            }
		}
        if (mqtt_state_get() == 1)
        {
			mqtt_connect_flag=0; // 一旦成功则清零

        }
        else
        {

        }
		time++;
        time %= 600;
        OSTimeDly(1000, OS_OPT_TIME_DLY, &err);
    }
}
// <e>SIM7100task
u16 time_cnt=1;
char up_down=1;
// <s>SystemDatasBroadcast
void  SystemDatasBroadcast (void *p_arg)
{
	u32 now_time;
	int time_delta;
	OS_ERR err;

	CPU_STK_SIZE free,used;
	(void)p_arg;

	F407USART1_SendString("SystemDatasBroadcast run\r\n");
	delay_ms(500);  


	while(DEF_TRUE)
	{
		u8 sta;
		IWDG_Feed();//喂狗
		#if DEBUG_MODE 
		#if USART1_DEBUG
		OSTaskStkChk (&SystemDatasBroadcast_TCB,&free,&used,&err);
		printf("SystemDatasBroadcast  used/free:%d/%d  usage:%%%d\r\n",used,free,(used*100)/(used+free));
		#endif
		OSTaskStkChk (&MainTaskTCB,&free,&used,&err);
		#if USART1_DEBUG
		printf("MainTaskTCB             used/free:%d/%d  usage:%%%d\r\n",used,free,(used*100)/(used+free));
		#endif	
		OSTaskStkChk (&MQTTRECEIVEtcb,&free,&used,&err);
		#if USART1_DEBUG
		printf("MQTTRECEIVEtcb             used/free:%d/%d  usage:%%%d\r\n",used,free,(used*100)/(used+free));
		#endif	
		OSTaskStkChk (&SIM7100tcb,&free,&used,&err);
		#if USART1_DEBUG
		printf("SIM7100tcb              used/free:%d/%d  usage:%%%d\r\n",used,free,(used*100)/(used+free));
		#endif	
		OSTaskStkChk (&HELLO_LEDtcb,&free,&used,&err);
		#if USART1_DEBUG
		printf("HELLO_LEDtcb             used/free:%d/%d  usage:%%%d\r\n",used,free,(used*100)/(used+free));
		#endif
		if(watchdog_f*10>7200) //1h,SoftReset
		{
			printf("\r\n\r\n\r\nsleep\r\n\r\n");
			SoftReset();
		}
		if((watchdog_f*10)%60 == 30)  // 3min过60s开始发送起数据
		{
			function_f|=(0x04);  // 发送传感器
		}

		if((watchdog_f*10)%1800 == 70) // 30min过70s 开始扫描相机
		{
			function_f|=(0x02);  // 拍照
			//key_wkup_down = 1;
		}
		if((watchdog_f*10)%1800 == 100) // 30min过70s 转存照片
		{
			function_f|=(0x01);  // 转存
			//key1_down = 1;
		}
		if((watchdog_f*10)%1800 == 150) // 30min过130s 开始上传图片
		{
			function_f|=(0x08);  // 发送图片
			//key2_down = 1;
		}
		#endif	
		now_time = get_time_cnt();
		time_delta = now_time - cycle.time_stamp;  // 正常时>0,或者now_time+3600- cycle.time_stamp>0 ，否则异常，更新时间戳时间
		if(time_delta <0)
			time_delta+=3600;
		sta = mqtt_state_get();
		printf("\r\n&heart|fun:%x|fun2:%x|netsta:%d|running time:{(max %d) %d}&\r\n\r\n", function_f,function_f2, sta, max_work_length, time_delta);
		#if EN_log_sd
		if(sd_ready_flag ==0xAA)
			mf_sync();
		#endif
		if(function_f==0 && function_f2==0)  // 如果超过420s，则自动休眠
		{
			#if SLEEP_MODE
			printf("$Well done, task accomplished!!!!!!");
			Sys_Enter_Standby(30);
			#endif
		}
		if(time_delta> max_work_length || time_delta<0)
		{
			#if SLEEP_MODE
			printf("$!Timeout, forced standby!!!!!!");
			Sys_Enter_Standby(30);
			#endif		
		}
		OSTimeDlyHMSM(0,0,10,0,(OS_OPT)OS_OPT_TIME_DLY,(OS_ERR*)&err);  
	}
}
// <e>SystemDatasBroadcast

