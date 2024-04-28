#include "scheduler.h"

TaskHandle_t xHandle1 = NULL;
TaskHandle_t xHandle2 = NULL;
TaskHandle_t xHandle3 = NULL;
TaskHandle_t xHandle4 = NULL;
TaskHandle_t xHandle5 = NULL;

SemaphoreHandle_t xSemHandle1 = NULL;
SemaphoreHandle_t xSemHandle2 = NULL;

BaseType_t WhoIsPrinting = 0;

#define USE_PRIORITY_CEILING_PROTOCOL 0

TickType_t t1 = 27;
TickType_t t2 = 27;
TickType_t t3 = 17;
TickType_t t4 = 57;
TickType_t t5 = 57;

void loop() {}

static void testFunc1( void *pvParameters )
{
  TickType_t ExecTime = *(TickType_t*)pvParameters;
  TickType_t start =  xTaskGetTickCount();
  TickType_t current = start;
  TickType_t IncCnt =  start + 1;
  while(IncCnt - start <= ExecTime) {
    if(xTaskGetTickCount() - current >= 1) {
      IncCnt++;
      if(WhoIsPrinting != 1){
        Serial.println("T1");
        Serial.print("P1=");
        Serial.println(uxTaskPriorityGet(NULL));
        WhoIsPrinting = 1;
      }
      current = xTaskGetTickCount();
      if((IncCnt - start) == 10) {
        xTaskResourceTake(xSemHandle1);
      }
      if((IncCnt - start) == 20) {
        xTaskResourceGive(xSemHandle1);
      }
    }
  }
}

static void testFunc2( void *pvParameters )
{
  TickType_t ExecTime = *(TickType_t*)pvParameters;
  TickType_t start =  xTaskGetTickCount();
  TickType_t current = start;
  TickType_t IncCnt =  start + 1;
  while(IncCnt - start <= ExecTime){
    if(xTaskGetTickCount() - current >= 1){
      IncCnt++;
      if(WhoIsPrinting != 2){
        Serial.println("T2");
        Serial.print("P2=");
        Serial.println(uxTaskPriorityGet(NULL));
        WhoIsPrinting = 2;
      }
      current = xTaskGetTickCount();
      if((IncCnt - start) == 10){
        xTaskResourceTake(xSemHandle2);
      }
      if((IncCnt - start) == 20){
        xTaskResourceGive(xSemHandle2);
      }
    }
  }
}


static void testFunc3( void *pvParameters )
{
  TickType_t ExecTime = *(TickType_t*)pvParameters;
  TickType_t start =  xTaskGetTickCount();
  TickType_t current = start;
  TickType_t IncCnt =  start + 1;
  while(IncCnt - start <= ExecTime){
    if(xTaskGetTickCount() - current >= 1){
      IncCnt++;
      if(WhoIsPrinting != 3){
        Serial.println("T3");
        Serial.print("P3=");
        Serial.println(uxTaskPriorityGet(NULL));
        WhoIsPrinting = 3;
      }
      current = xTaskGetTickCount();
    }
  }
}


static void testFunc4( void *pvParameters )
{
  TickType_t ExecTime = *(TickType_t*)pvParameters;
  TickType_t start =  xTaskGetTickCount();
  TickType_t current = start;
  TickType_t IncCnt =  start + 1;
  while(IncCnt - start <= ExecTime){
    if(xTaskGetTickCount() - current >= 1){
      IncCnt++;
      if(WhoIsPrinting != 4){
        Serial.println("T4");
        Serial.print("P4=");
        Serial.println(uxTaskPriorityGet(NULL));
        WhoIsPrinting = 4;
      }
      current = xTaskGetTickCount();
      if((IncCnt - start) == 10){
       xTaskResourceTake(xSemHandle1);
      }
      if((IncCnt - start) == 25){
       xTaskResourceTake(xSemHandle2);
      }
      if((IncCnt - start) == 40){
        xTaskResourceGive(xSemHandle2);
      }
      if((IncCnt - start) == 50){
        xTaskResourceGive(xSemHandle1);
      }
    }
  }
}

static void testFunc5( void *pvParameters )
{
  TickType_t ExecTime = *(TickType_t*)pvParameters;
  TickType_t start =  xTaskGetTickCount();
  TickType_t current = start;
  TickType_t IncCnt =  start + 1;
  while(IncCnt - start <= ExecTime){
    if(xTaskGetTickCount()-current >= 1){
      IncCnt++;
      if(WhoIsPrinting != 5){
        Serial.println("T5");
        Serial.print("P5=");
        Serial.println(uxTaskPriorityGet(NULL));
        WhoIsPrinting = 5;
      }
      current = xTaskGetTickCount();
      if((IncCnt - start) == 10){
       xTaskResourceTake(xSemHandle2);
      }
      if((IncCnt - start) == 50){
        xTaskResourceGive(xSemHandle2);
      }
    }
  }
}

int main( void )
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB, on LEONARDO, MICRO, YUN, and other 32u4 based boards.
  }		

	vSchedulerInit();

  xSemHandle1 = xSemaphoreCreateMutex();
  xSemHandle2 = xSemaphoreCreateMutex();
  if( xSemHandle1 == NULL || xSemHandle2 == NULL )
  {
    Serial.println( "Unsuccessful creation of mutexes!" );
  }

  #if ( USE_PRIORITY_CEILING_PROTOCOL == 1 )
    vInitializePriorityCeiling(xSemHandle1, 5);
    vInitializePriorityCeiling(xSemHandle2, 4);
  #endif

  // xPhaseTick, xPeriodTick, xMaxExecTimeTick, xDeadlineTick
  vSchedulerPeriodicTaskCreate(testFunc1, "t1", configMINIMAL_STACK_SIZE, &t1, 1, &xHandle1, 70, 200, 40, 130);
  vSchedulerPeriodicTaskCreate(testFunc2, "t2", configMINIMAL_STACK_SIZE, &t2, 2, &xHandle2, 50, 200, 40, 150);
  vSchedulerPeriodicTaskCreate(testFunc3, "t3", configMINIMAL_STACK_SIZE, &t3, 3, &xHandle3, 40, 200, 30, 160);
  vSchedulerPeriodicTaskCreate(testFunc4, "t4", configMINIMAL_STACK_SIZE, &t4, 4, &xHandle4, 20, 200, 70, 180);
  vSchedulerPeriodicTaskCreate(testFunc5, "t5", configMINIMAL_STACK_SIZE, &t5, 5, &xHandle5, 0, 200, 70, 200);

	vSchedulerStart();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached. */
	
	for( ;; );
}
