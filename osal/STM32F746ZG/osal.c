#include "osal.h"

#include "stm32f7xx_hal.h"

extern volatile uint32_t sec;
extern TIM_HandleTypeDef htim2;

void osal_timer_start(osal_timert *self, uint32 timeout_us)
{
	ec_timet current = osal_current_time();
	self->stop_time.usec = current.usec + (timeout_us % 1000000);
	self->stop_time.sec = current.sec + (timeout_us / 1000000);
	if (self->stop_time.usec >= 1000000)
	{
		self->stop_time.usec -= 1000000;
		self->stop_time.sec++;
	}
}

boolean osal_timer_is_expired(osal_timert *self)
{
	boolean expired = FALSE;

	ec_timet current = osal_current_time();
	if (current.sec >= self->stop_time.sec && current.usec >= self->stop_time.usec)
		expired = TRUE;
	else if (current.sec > self->stop_time.sec)
		expired = TRUE;

	return expired;
}

int osal_usleep(uint32 usec)
{
	osal_timert timer;

	osal_timer_start(&timer, usec);
	while(!osal_timer_is_expired(&timer));

	return 0;
}

ec_timet osal_current_time(void)
{
	ec_timet rval;
	rval.sec = sec;
	rval.usec = htim2.Instance->CNT;
	if (sec != rval.sec) //timer overflow has occured since polling the seconds
	{
		/* since there is no way to determine whether the usec was changed before or after reading, we have to check both values again. */
		rval.sec = sec;
		rval.usec = htim2.Instance->CNT;
	}

	return rval;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
	if (end->usec < start->usec)
	{
		diff->sec = end->sec - start->sec - 1;
		diff->usec = end->usec + 1000000 - start->usec;
	}
	else
	{
		diff->sec = end->sec - start->sec;
		diff->usec = end->usec - start->usec;
	}
}

int osal_thread_create(void *thandle, int stacksize, void *func, void *param)
{
	return 1;												//TODO not supported
}

int osal_thread_create_rt(void *thandle, int stacksize, void *func, void *param)
{
	return 1;												//TODO not supported
}
