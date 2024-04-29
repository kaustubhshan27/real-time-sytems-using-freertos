#include "scheduler.h"

#define MAX_SEMAPHORES_PER_TASK 5

#define schedUSE_TCB_ARRAY 1

/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode; 		/* Function pointer to the code that will be run periodically. */
	const char *pcName; 			/* Name of the task. */
	UBaseType_t uxStackDepth; 		/* Stack size of the task. */
	void *pvParameters; 			/* Parameters to the task function. */
	UBaseType_t uxPriority; 		/* Priority of the task. */
	TaskHandle_t *pxTaskHandle;		/* Task handle for the task. */
	TickType_t xReleaseTime;		/* Release time of the task. */
	TickType_t xRelativeDeadline;	/* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline;	/* Absolute deadline of the task. */
	TickType_t xPeriod;				/* Task period in milliseconds. */
	TickType_t xLastWakeTime; 		/* Last time stamp when the task started running. */
	TickType_t xMaxExecTime;		/* Worst-case execution time of the task. */
	TickType_t xExecTime;			/* Current execution time of the task. */
	BaseType_t xResourceAcquired;   /* pdFALSE if resource not acquired, pdTRUE if acquired. */

	BaseType_t xWorkIsDone; 		/* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xPriorityIsSet;	/* pdTRUE if the priority is assigned. */
		BaseType_t xInUse; 			/* pdFALSE if this extended TCB is empty. */
	#endif

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		BaseType_t xExecutedOnce;	/* pdTRUE if the task has executed once. */
		BaseType_t xDeadlineExceeded; 	/* pdTRUE when task exceeds its deadline. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TickType_t xAbsoluteUnblockTime;	/* The task will be unblocked at this time if it is blocked by the scheduler task. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		BaseType_t xMaxExecTimeExceeded; 	/* pdTRUE when execTime exceeds maxExecTime. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

	#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 && configUSE_MUTEXES == 1 )
		SemaphoreHandle_t xAcquiredSemaphores[MAX_SEMAPHORES_PER_TASK];	/* Array to maintain a list of semaphores acquired by the task. */
		BaseType_t xSemaphoreCount;										/* Number of semaphores currently acquired by the task. */
	#endif
	
} SchedTCB_t;

#if( schedUSE_TCB_ARRAY == 1 )
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle );
	static void prvInitTCBArray( void );
	/* Find index for an empty entry in xTCBArray. Return -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void );
	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex );
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode( void *pvParameters );
static void prvCreateAllTasks( void );

/* Create a separate function to assign priorities for DM */
#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	static void prvSetFixedPriorities( void );	
#endif /* schedSCHEDULING_POLICY */

#if( schedUSE_SCHEDULER_TASK == 1 )
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB );
	static void prvSchedulerFunction( void );
	static void prvCreateSchedulerTask( void );
	static void prvWakeScheduler( void );
	static void prvDeleteAndRecreateTask( SchedTCB_t *pxTCB );

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB );
		static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount );				
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		static void prvExecTimeExceedHook( SchedTCB_t *pxCurrentTask );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
#endif /* schedUSE_SCHEDULER_TASK */

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 && configUSE_MUTEXES == 1 )
	BaseType_t xTaskResourceTake( SemaphoreHandle_t xSemaphore );
	BaseType_t xTaskResourceGive( SemaphoreHandle_t xSemaphore );
#endif

#if( schedUSE_TCB_ARRAY == 1 )
	/* Array for extended TCBs. */
	static SchedTCB_t xTCBArray[ schedMAX_NUMBER_OF_PERIODIC_TASKS ] = { 0 };
	/* Counter for number of periodic tasks. */
	static BaseType_t xTaskCounter = 0;
#endif /* schedUSE_TCB_ARRAY */

#if( schedUSE_SCHEDULER_TASK )
	static TickType_t xSchedulerWakeCounter = 0;
	static TaskHandle_t xSchedulerHandle = NULL;
#endif /* schedUSE_SCHEDULER_TASK */

#define SCHEDULER_OVERHEAD_DUMMY_LOOP	0

#if( schedUSE_TCB_ARRAY == 1 )
	/* Returns index position in xTCBArray of TCB with same task handle as parameter. */
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle )
	{
		static BaseType_t xIndex = 0;	/* The index value is preserved across different instances of the function call, searching the array starts from the last place it left off. */
		BaseType_t xIterator;

		for( xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++ )
		{
			if( pdTRUE == xTCBArray[ xIndex ].xInUse && *xTCBArray[ xIndex ].pxTaskHandle == xTaskHandle )
			{
				return xIndex;
			}
		
			xIndex++;
			if( schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex )
			{
				xIndex = 0;
			}
		}
		return -1;
	}

	/* Initializes xTCBArray. */
	static void prvInitTCBArray( void )
	{
		UBaseType_t uxIndex;
		for( uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
		{
			xTCBArray[ uxIndex ].xInUse = pdFALSE;
		}
	}

	/* Find index for an empty entry in xTCBArray. Returns -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void )
	{
		/* your implementation goes here:
			1. Scan xTCBArray to identify the first index at which the xInUse member is False. 
			2. Return the index if found. 
			3. If no TCB element is free, return -1. 
		*/
		static BaseType_t xIndex = 0;
		BaseType_t xIterator;

		for( xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++ )
		{
			if( pdFALSE == xTCBArray[ xIndex ].xInUse )
			{
				return xIndex;
			}

			xIndex++;
			if( schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex )
			{
				xIndex = 0;
			}	
		}
		return -1;
	}

	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex )
	{
		configASSERT( xIndex >= 0 && xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS )
		configASSERT( xTCBArray[ xIndex ].xInUse == pdTRUE )
		/* your implementation goes here:
			1. Set the xInUse member to False for the TCB specified by the index. 
		*/
		xTCBArray[ xIndex ].xInUse = pdFALSE;
	}
	
#endif /* schedUSE_TCB_ARRAY */


/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode( void *pvParameters )
{
	SchedTCB_t *pxThisTask;	
	TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();  

    /* your implementation goes here:
		1. Check if xCurrentTaskHandle is not NULL by raising an assertion on the condition. 
	*/
	configASSERT( xCurrentTaskHandle != NULL );
	
	/* your implementation goes here:
		1. Call prvGetTCBIndexFromHandle() to obtain the index from xCurrentTaskHandle.
		2. Assign the address of the corresponding TCB to pxThisTask.
	*/
	BaseType_t xCurrentTaskIndex = prvGetTCBIndexFromHandle( xCurrentTaskHandle );
	configASSERT( xCurrentTaskIndex != -1 );

	pxThisTask = &xTCBArray[ xCurrentTaskIndex ];

    /* If required, use the handle to obtain further information about the task. */
    
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		/* your implementation goes here:
			1. Set pxThisTask->xExecutedOnce to pdTRUE.
		*/
		pxThisTask->xExecutedOnce = pdTRUE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
    
	if( 0 == pxThisTask->xReleaseTime )
	{
		pxThisTask->xLastWakeTime = xSystemStartTime;
	}
	else
	{
		/* your implementation goes here:
			1. Delay the task until its release time before it executes for the first time.
		*/
		xTaskDelayUntil( &pxThisTask->xLastWakeTime, pxThisTask->xReleaseTime );
	}

	TaskStatus_t xTaskDetails;

	for( ; ; )
	{	
		// Serial.print( pxThisTask->pcName );
		// Serial.println( ": running" );

		/* Get task priority info. */
		// vTaskGetInfo(NULL, &xTaskDetails, pdFALSE, eRunning);
		// Serial.print( "BP=" );
		// Serial.print( xTaskDetails.uxBasePriority );
		// Serial.print( "; CP=" );
		// Serial.println( xTaskDetails.uxCurrentPriority );

		/* your implementation goes here: 
			1. Set xWorkIsDone to pdFALSE;
		*/
		pxThisTask->xWorkIsDone = pdFALSE;

		/* Execute the task function specified by the user. */
		pxThisTask->pvTaskCode( pvParameters );

		pxThisTask->xExecTime = 0;   
        
		/* your implementation goes here: 
			1. Set xWorkIsDone to pdTRUE.
			2. After each execution, put the current task to sleep until its next activation period using xTaskDelayUntil(). 
		*/
		pxThisTask->xWorkIsDone = pdTRUE;
		xTaskDelayUntil( &pxThisTask->xLastWakeTime, pxThisTask->xPeriod );
	}
}

/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, UBaseType_t uxStackDepth, void *pvParameters, UBaseType_t uxPriority,
		TaskHandle_t *pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick )
{
	taskENTER_CRITICAL();
	SchedTCB_t *pxNewTCB;
	
	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex = prvFindEmptyElementIndexTCB();
		configASSERT( xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS );
		configASSERT( xIndex != -1 );
		pxNewTCB = &xTCBArray[ xIndex ];	
	#endif /* schedUSE_TCB_ARRAY */

	/* Intialize all items. */
		
	pxNewTCB->pvTaskCode = pvTaskCode;
	pxNewTCB->pcName = pcName;
	pxNewTCB->uxStackDepth = uxStackDepth;
	pxNewTCB->pvParameters = pvParameters;
	pxNewTCB->uxPriority = uxPriority;		/* Task priorities are assigned in prvSetFixedPriorities(). */
	pxNewTCB->pxTaskHandle = pxCreatedTask;
	pxNewTCB->xReleaseTime = xPhaseTick;
	pxNewTCB->xPeriod = xPeriodTick;
	
    /* your implementation goes here: Initialize the following - 
		1. pxNewTCB->xRelativeDeadline = xDeadlineTick
		2. pxNewTCB->xMaxExecTime = xMaxExecTimeTick
		3. pxNewTCB->xExecTime = 0
		4. pxNewTCB->xWorkIsDone = pdFALSE
		5. pxNewTCB->xLastWakeTime = xSystemStartTime
		6. pxNewTCB->xResourceAcquired = pdFALSE
	*/
	pxNewTCB->xRelativeDeadline = xDeadlineTick;
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	pxNewTCB->xExecTime = 0;
	pxNewTCB->xWorkIsDone = pdFALSE;
	pxNewTCB->xLastWakeTime = xSystemStartTime;	/* Initializing xLastWakeTime to have a reference value before it is used in xTaskDelayUntil(). */
	pxNewTCB->xResourceAcquired = pdFALSE;

	/* Initialize semaphore related per task information. */
	for ( BaseType_t xIndex = 0; xIndex < xTaskCounter; xIndex++ ) 
	{
        pxNewTCB->xAcquiredSemaphores[xIndex] = NULL;
    }
    pxNewTCB->xSemaphoreCount = 0;

	#if( schedUSE_TCB_ARRAY == 1 )
		pxNewTCB->xInUse = pdTRUE;
		pxNewTCB->xPriorityIsSet = pdFALSE;
	#endif /* schedUSE_TCB_ARRAY */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		/* member initialization */
		/* your implementation goes here: 
			1. pxNewTCB->xExecutedOnce = pdFALSE 
		*/
		pxNewTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */	

	#if( schedUSE_TCB_ARRAY == 1 )
		xTaskCounter++;	
	#endif /* schedUSE_TCB_SORTED_LIST */
	taskEXIT_CRITICAL();
}

/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete( TaskHandle_t xTaskHandle )
{
	/* your implementation goes here */
	/* Line 279: 
		1. Check if xTaskHandle is not NULL by raising an assertion on the condition.
		2. Obtain the corresponding xTaskHandle index using prvGetTCBIndexFromHandle().
		3. Using the obtained index, use prvDeleteTCBFromArray() to mark the TCB as unused.
		4. Decrement xTaskCounter.
	*/
	configASSERT( xTaskHandle != NULL );

	BaseType_t xIndex = prvGetTCBIndexFromHandle( xTaskHandle );
	configASSERT( xIndex != -1 );

	prvDeleteTCBFromArray( xIndex );
	vTaskDelete( xTaskHandle );
	xTaskCounter--;
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks( void )
{
	SchedTCB_t *pxTCB;

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex;
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
			configASSERT( pdTRUE == xTCBArray[ xIndex ].xInUse );
			pxTCB = &xTCBArray[ xIndex ];
			/* your implementation goes here */
			/* Line 299: 
				1. xTaskCreate((TaskFunction_t)prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle)
			*/
			BaseType_t xReturnValue = xTaskCreate( (TaskFunction_t)prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );
			configASSERT( xReturnValue != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY )
		}	
	#endif /* schedUSE_TCB_ARRAY */
}

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	/* Initiazes fixed priorities of all periodic tasks with respect to RMS policy. */
	static void prvSetFixedPriorities( void )
	{
		BaseType_t xIter, xIndex;
		TickType_t xShortest, xPreviousShortest = 0;
		SchedTCB_t *pxShortestTaskPointer, *pxTCB;

		#if( schedUSE_SCHEDULER_TASK == 1 )
			BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY; 
		#else
			BaseType_t xHighestPriority = configMAX_PRIORITIES;
		#endif /* schedUSE_SCHEDULER_TASK */

		for( xIter = 0; xIter < xTaskCounter; xIter++ )
		{
			xShortest = portMAX_DELAY;

			/* search for shortest period */
			for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
			{
				/* your implementation goes here: 
					1. Iterate through xTCBArray and find the next shortest priority.
				*/
				pxTCB = &xTCBArray[xIndex];
				configASSERT( pxTCB->xInUse == pdTRUE );

				if ( pxTCB->xPriorityIsSet == pdFALSE )
				{
					#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS )
						if ( pxTCB->xPeriod <= xShortest )
						{
							xShortest = pxTCB->xPeriod;
							pxShortestTaskPointer = pxTCB;
						}
					#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
						if ( pxTCB->xRelativeDeadline <= xShortest )
						{
							xShortest = pxTCB->xRelativeDeadline;
							pxShortestTaskPointer = pxTCB;
						}
					#endif /* schedSCHEDULING_POLICY */
				}
			}
		
			/* set highest priority to task with xShortest period (the highest priority is configMAX_PRIORITIES - 1) */
			/* your implementation goes here: 
				1. Assign the priority according to the period.
				2. Assign the next shortest priority found to xPreviousShortest. This will help in the next iteration.
			*/
			pxShortestTaskPointer->uxPriority = xHighestPriority;
			pxShortestTaskPointer->xPriorityIsSet = pdTRUE;

			if( xPreviousShortest != xShortest )
			{
				xHighestPriority--;
			}
		
			xPreviousShortest = xShortest;
		}
	}
#endif /* schedSCHEDULING_POLICY */

/* Called when a deadline of a periodic task is missed.
 * Deletes the periodic task that has missed it's deadline and recreate it.
 * The periodic task is released during next period. */
static void prvDeleteAndRecreateTask( SchedTCB_t *pxTCB )
{
	/* Delete the pxTask and recreate it. */
	/* your implementation goes here:
		1. vTaskDelete(*pxTCB->pxTaskHandle)
	*/
	vTaskDelete( *pxTCB->pxTaskHandle );
	pxTCB->xExecTime = 0;
	prvPeriodicTaskRecreate( pxTCB );	
		
	/* Need to reset next WakeTime for correct release. */
	/* your implementation goes here: 
		1. Update pxTCB->xReleaseTime for the recreated task pointing to the next instance.
		2. Reset xLastWakeTime to xSystemStartTime (alias for time 0 as per the system).
		3. Update pxTCB->xAbsoluteDeadline for the recreated task pointing to the next instance.
	*/
	pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;
	pxTCB->xLastWakeTime = xSystemStartTime;
	pxTCB->xAbsoluteDeadline = pxTCB->xReleaseTime + pxTCB->xRelativeDeadline;	
}

#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )

	/* Recreates a deleted task that still has its information left in the task array (or list). */
	static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB )
	{
		/* your implementation goes here: 
			1. xTaskCreate((TaskFunction_t)prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle)
		*/
		BaseType_t xReturnValue = xTaskCreate( (TaskFunction_t) prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );

		configASSERT( xReturnValue != pdFAIL );

		/* your implementation goes here:
			1. pxNewTCB->xExecutedOnce = pdFALSE
			2. Reset pxTCB->xMaxExecTimeExceeded because by the time the task is recreated it's possible that the WCET time of the previous instance has exceeded.
			which is not true. 
		*/
		pxTCB->xExecutedOnce = pdFALSE;
		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
			pxTCB->xMaxExecTimeExceeded = pdFALSE;
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	}

	/* Checks whether given task has missed deadline or not. */
	static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{ 
		/* check whether deadline is missed. */     		

		/* your implementation goes here:
			1. If pxTCB->xAbsoluteDeadline < xTickCount, the task has missed its deadline.
			2. If the task has missed its deadline, call prvDeleteAndRecreateTask().
		*/
		configASSERT( pxTCB != NULL );
		if ( pxTCB->xWorkIsDone == pdFALSE && pxTCB->xExecutedOnce == pdTRUE )
		{
			pxTCB->xAbsoluteDeadline = pxTCB->xLastWakeTime + pxTCB->xRelativeDeadline;
			if ( pxTCB->xAbsoluteDeadline < xTickCount )
			{
				pxTCB->xDeadlineExceeded = pdTRUE;
				Serial.print( pxTCB->pcName );

				BaseType_t xHigherPriorityTaskWoken;
        		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
        		xTaskResumeFromISR( xSchedulerHandle );
			}
		}	
	}	
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */


#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )

	/* Called if a periodic task has exceeded its worst-case execution time.
	 * Deletes the periodic task that has missed it's deadline and recreate it.
	 * The periodic task is released during next period. */
	static void prvExecTimeExceedHook( SchedTCB_t *pxCurrentTask )
	{
        pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
        
        BaseType_t xHigherPriorityTaskWoken;
        vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
        xTaskResumeFromISR( xSchedulerHandle );
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Called by the scheduler task. Checks all tasks for any enabled
	 * Timing Error Detection feature. */
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB )
	{
		/* your implementation goes here */

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )						
			/* check if task missed deadline */
			if ( pxTCB->xDeadlineExceeded == pdTRUE && ( pxTCB->xWorkIsDone == pdTRUE || pxTCB->xResourceAcquired == pdFALSE ) )
			{
				Serial.print( pxTCB->pcName );
				Serial.println( ": deadline missed!" );
				prvDeleteAndRecreateTask( pxTCB );
				pxTCB->xDeadlineExceeded = pdFALSE;
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        	if( pxTCB->xMaxExecTimeExceeded == pdTRUE && ( pxTCB->xWorkIsDone == pdTRUE || pxTCB->xResourceAcquired == pdFALSE ) )
        	{
				Serial.print( pxTCB->pcName );
				Serial.println( ": WCET exceed!" );
				prvDeleteAndRecreateTask( pxTCB );
            	pxTCB->xMaxExecTimeExceeded = pdFALSE;
        	}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		return;
	}

	/* Function code for the scheduler task. */
	static void prvSchedulerFunction( void *pvParameters )
	{
		for( ; ; )
		{
			// 100 ms = 6 ticks = 62 Hz = Scheduler Period
			// Serial.println(configTICK_RATE_HZ);
			
     		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				TickType_t xTickCount = xTaskGetTickCount();
        		
				/* your implementation goes here:
					1. Iterate through the TCB arrray and check deadline misses for each task.
					2. Call prvSchedulerCheckTimingError() to check for timing errors. 
				*/
				for( BaseType_t xIndex = 0; xIndex < xTaskCounter; xIndex++ )
				{
					prvSchedulerCheckTimingError( xTickCount, &xTCBArray[ xIndex ] );
				}
			
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			#if ( SCHEDULER_OVERHEAD_DUMMY_LOOP == 1 )
				Serial.println("s");
				/* Dummy loop to analyze scheduler overhead (approximately 10ms). */
				volatile BaseType_t i, j;
				for (i = 0; i < 1000; i++) { }
				Serial.println("f");
			#endif

			ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}

	/* Creates the scheduler task. */
	static void prvCreateSchedulerTask( void )
	{
		xTaskCreate( (TaskFunction_t) prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle );                             
	}
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Wakes up (context switches to) the scheduler task. */
	static void prvWakeScheduler( void )
	{
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		xTaskResumeFromISR( xSchedulerHandle );    
	}

	/* Called every software tick. */
	void vApplicationTickHook( void )
	{            				
		/* Use the task handle to indentify the task instead of task priority because the priority may change due to 
		   priority inheritance. */
		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();
		BaseType_t xCurrentTaskIndex = prvGetTCBIndexFromHandle( xCurrentTaskHandle );
		SchedTCB_t *pxCurrentTask = &xTCBArray[ xCurrentTaskIndex ];
		
		if( xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() )
		{
			pxCurrentTask->xExecTime++;
     
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
            	if( pxCurrentTask->xMaxExecTime <= pxCurrentTask->xExecTime )
            	{
               		if( pdFALSE == pxCurrentTask->xMaxExecTimeExceeded )
                	{
                    	prvExecTimeExceedHook( pxCurrentTask );
                	}
            	}
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
                if( pdFALSE == pxCurrentTask->xDeadlineExceeded )
                {
                    prvCheckDeadline( pxCurrentTask, xTaskGetTickCountFromISR() );
                }
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )    
			xSchedulerWakeCounter++;
 
			if( xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD )
			{
				xSchedulerWakeCounter = 0;     
				prvWakeScheduler();
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	}
#endif /* schedUSE_SCHEDULER_TASK */

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 && configUSE_MUTEXES == 1 )
	/* Acquires the semaphore. */
	BaseType_t xTaskResourceTake( SemaphoreHandle_t xSemaphore )
	{
		configASSERT ( xSemaphore != NULL );

		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();
		BaseType_t xCurrentTaskIndex = prvGetTCBIndexFromHandle( xCurrentTaskHandle );
		SchedTCB_t *pxThisTask = &xTCBArray[ xCurrentTaskIndex ];

		BaseType_t status = xSemaphoreTake( xSemaphore, portMAX_DELAY );

		/* If the mutex is acquired successfully, the task holds the resource */
		if ( status == pdTRUE )
		{
			// Serial.print( pxThisTask->pcName );
			// Serial.println( " AM" );

			pxThisTask->xResourceAcquired = pdTRUE;

			configASSERT( pxThisTask->xSemaphoreCount < MAX_SEMAPHORES_PER_TASK );
			BaseType_t xIndex;
			for ( xIndex = 0; xIndex < xTaskCounter; xIndex++ ) 
			{
        		if ( pxThisTask->xAcquiredSemaphores[xIndex] == NULL )
				{
					break;
				}
    		}
			pxThisTask->xAcquiredSemaphores[xIndex] = xSemaphore;
			pxThisTask->xSemaphoreCount++;
		}

		return status;
	}

	/* Releases the semaphore. */
	BaseType_t xTaskResourceGive( SemaphoreHandle_t xSemaphore )
	{
		configASSERT ( xSemaphore != NULL );

		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();
		BaseType_t xCurrentTaskIndex = prvGetTCBIndexFromHandle( xCurrentTaskHandle );
		SchedTCB_t *pxThisTask = &xTCBArray[ xCurrentTaskIndex ];

		BaseType_t status = xSemaphoreGive( xSemaphore );

		/* If the mutex is released successfully, the task no longer hold the resource */
		if ( status == pdTRUE )
		{
			// Serial.print( pxThisTask->pcName );
			// Serial.println( " RM" );

			configASSERT( pxTCB->xSemaphoreCount > 0 );
			BaseType_t xIndex;
			for ( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
			{
        		if ( pxThisTask->xAcquiredSemaphores[xIndex] == xSemaphore )
				{
					break;
				}
    		}
			pxThisTask->xAcquiredSemaphores[xIndex] = NULL;
			pxThisTask->xSemaphoreCount--;

			if ( pxThisTask->xSemaphoreCount == 0 )
			{
				pxThisTask->xResourceAcquired = pdFALSE;
			}
		}

		return status;
	}	
#endif

/* This function must be called before any other function call from this module. */
void vSchedulerInit( void )
{
	#if( schedUSE_TCB_ARRAY == 1 )
		prvInitTCBArray();
	#endif /* schedUSE_TCB_ARRAY */
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart( void )
{
	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
		prvSetFixedPriorities();	
	#endif /* schedSCHEDULING_POLICY */

	#if( schedUSE_SCHEDULER_TASK == 1 )
		prvCreateSchedulerTask();
	#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();
	  
	xSystemStartTime = xTaskGetTickCount();
	
	vTaskStartScheduler();
}
