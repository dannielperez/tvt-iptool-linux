#include <stddef.h>

int
NET_SDK_Init(void)
{
  return 1;
}

int
NET_SDK_Cleanup(void)
{
  return 1;
}

unsigned int
NET_SDK_GetLastError(void)
{
  return 0;
}

int
NET_SDK_ModifyDeviceNetInfo(void *network_info)
{
  return network_info != NULL;
}
