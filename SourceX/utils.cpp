#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

bool GFX_IsRetroFW20(void)
{
    struct stat buffer;
    
    return (stat("/proc/jz/ipu_ratio", &buffer) == 0); 
}
