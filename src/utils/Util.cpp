#include "Util.h"
#include <intrin.h>
#include <windows.h>

std::string getCPUName() {
  int CPUInfo[4] = {-1};
  char CPUBrandString[0x41];
  __cpuid(CPUInfo, 0x80000000);
  unsigned int nExIds = CPUInfo[0];

  memset(CPUBrandString, 0, sizeof(CPUBrandString));

  // Get the information associated with each extended ID.
  for (unsigned int i = 0x80000000; i <= nExIds; ++i) {
    __cpuid(CPUInfo, i);
    // Interpret CPU brand string.
    if (i == 0x80000002)
      memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
    else if (i == 0x80000003)
      memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
    else if (i == 0x80000004)
      memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
  }

  CPUBrandString[0x40] = 0;
  return std::string(CPUBrandString);
}
