"""Build/run actual servo C with fake clock/PCA/storage. Never contacts hardware.
Requires explicit permission. Usage: python tools/check_timeline_host.py --cc <native C compiler>
"""
import argparse, subprocess, tempfile, os, shutil, sys
sys.dont_write_bytecode = True
from check_ble_transport import find_msvc_environment
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'STM32CubeIDE/Application/User/Core'
STUBS={
'main.h': '#pragma once\n#include <stdint.h>\ntypedef enum {HAL_OK,HAL_ERROR,HAL_BUSY} HAL_StatusTypeDef;\n',
'config.h':'#pragma once\n#define DISABLE_I2C_PCA9685 0\n',
'FreeRTOS.h':'#pragma once\n#include <stdint.h>\ntypedef uint32_t TickType_t;\n#define configTICK_RATE_HZ 1000U\n#define pdMS_TO_TICKS(x) (x)\n#define taskENTER_CRITICAL() ((void)0)\n#define taskEXIT_CRITICAL() ((void)0)\n',
 'task.h':'#pragma once\n#include "FreeRTOS.h"\nTickType_t xTaskGetTickCount(void);\n',
 'cmsis_os2.h':'#pragma once\n#include <stdint.h>\n#define osFlagsWaitAny 0\nuint32_t osThreadFlagsSet(void *,uint32_t);\nuint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);\nvoid osDelay(uint32_t);\n',
 'app_freertos.h':'#pragma once\nextern void *servoTaskHandle;\n',
 'timer_if.h':'#pragma once\n#include <stdint.h>\nuint32_t TIMER_IF_GetTimerValue(void);\nuint32_t TIMER_IF_Convert_ms2Tick(uint32_t);\n',
}
def main():
 p=argparse.ArgumentParser();p.add_argument('--cc');a=p.parse_args()
 env=None;cc=a.cc or shutil.which('gcc') or shutil.which('clang')
 if not cc and os.name=='nt':
  env=find_msvc_environment()
  if env:cc=shutil.which('cl.exe',path=next(v for k,v in env.items() if k.upper()=='PATH'))
 if not cc:p.error('No host compiler found')
 with tempfile.TemporaryDirectory(prefix='timeline-host-') as directory:
  d=Path(directory)
  for name,source in STUBS.items(): (d/name).write_text(source)
  source=(APP/'comm/comm_interface.c').read_text()
  parser=source.split('static uint8_t comm_ParseTimelineId',1)[1].split('packetSize_t parser_ProcessOne',1)[0]
  (d/'timeline_id_parser.inc').write_text('static uint8_t comm_ParseTimelineId'+parser)
  exe=d/'timeline-test.exe'
  if Path(cc).name.lower() in ('cl','cl.exe'):
   command=[cc,'/nologo','/std:c11','/W4','/I'+str(d),'/I'+str(APP),str(ROOT/'tools/timeline_host_test.c'),'/Fe:'+str(exe),'/Fo:'+str(d/'test.obj')]
  else:command=[cc,'-std=c11','-Wall','-Wextra','-I'+str(d),'-I'+str(APP),str(ROOT/'tools/timeline_host_test.c'),'-o',str(exe)]
  subprocess.run(command,check=True,env=env)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
