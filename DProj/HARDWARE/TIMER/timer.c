#include "timer.h"
#include "usbh_usr.h" 
#include "delay.h"
#include "led.h"
extern USBH_HOST  USB_Host;
extern USB_OTG_CORE_HANDLE  USB_OTG_Core;
//通用定时器5中断初始化
//arr：自动重装值。
//psc：时钟预分频数
//定时器溢出时间计算方法:Tout=((arr+1)*(psc+1))/Ft us.
//Ft=定时器工作频率,单位:Mhz
//这里使用的是定时器5!
void TIM5_Int_Init(u16 arr,u16 psc)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5,ENABLE);  ///使能TIM5时钟
	
	TIM_TimeBaseInitStructure.TIM_Period = arr; 	//自动重装载值
	TIM_TimeBaseInitStructure.TIM_Prescaler=psc;  //定时器分频
	TIM_TimeBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up; //向上计数模式
	TIM_TimeBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1; 
	
	TIM_TimeBaseInit(TIM5,&TIM_TimeBaseInitStructure);//初始化TIM5
	
	TIM_ITConfig(TIM5,TIM_IT_Update,ENABLE); //允许定时器5更新中断
	//TIM_Cmd(TIM5,ENABLE); //使能定时器3
	
	NVIC_InitStructure.NVIC_IRQChannel=TIM5_IRQn; //定时器3中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=0x01; //抢占优先级1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=0x03; //子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
}

////定时器5中断服务函数
//void TIM5_IRQHandler(void)
//{
//	static unsigned char t;
//	if(TIM_GetITStatus(TIM5,TIM_IT_Update)==SET) //溢出中断
//	{
//		USBH_Process(&USB_OTG_Core, &USB_Host);
//		t++;
//		if(t==200){ HELLO_LED2_NOT(); t=0;}
//		
//	}
//	TIM_ClearITPendingBit(TIM5,TIM_IT_Update);  //清除中断标志位
//}

//定时器5中断服务程序	 
void TIM5_IRQHandler(void)
{ 		
	OSIntEnter();   
	if(TIM_GetITStatus(TIM5,TIM_IT_Update)==SET)//溢出中断 
	{
		if(OSRunning!=TRUE)//OS还没运行,借TIM3的中断,10ms一次,来扫描USB
		{
			usbapp_pulling();
		}
	}				   
	TIM_ClearITPendingBit(TIM5,TIM_IT_Update);  //清除中断标志位  
	OSIntExit(); 	    		  			    
}
