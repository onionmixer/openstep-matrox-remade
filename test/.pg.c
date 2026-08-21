#include <stdio.h>
#include <mach/mach.h>
int main(void)
{
    printf("vm_page_size = %d\n", (int)vm_page_size);
    printf("getpagesize() = %d\n", getpagesize());
    return 0;
}
