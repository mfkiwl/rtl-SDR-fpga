#include <stdio.h>
#include <sys/mman.h> 
#include <fcntl.h> 
#include <unistd.h>
#include <stdint.h>

#define RADIO_PERIPH_ADDRESS       0x43C00000
#define MAP_SIZE                   4096
#define TARGET_WORDS               480000U  // 48 kHz × 10 s
#define FIFO_BASE_ADDRESS          0x43C10000

// these offsets are in 32-bit words, not bytes:
//   XLLF_RDFO_OFFSET = 0x1C bytes → 0x1C/4 = 7
//   XLLF_RDFD_OFFSET = 0x20 bytes → 0x20/4 = 8
#define RX_FIFO_OCCUPANCY_OFFSET   7  
#define RX_FIFO_DATA_OFFSET        8  

// the below code uses a device called /dev/mem to get a pointer to a physical
// address.  We will use this pointer to read/write the custom peripheral
volatile unsigned int * get_a_pointer(unsigned int phys_addr)
{

	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC); 
	void *map_base = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, phys_addr); 
	volatile unsigned int *radio_base = (volatile unsigned int *)map_base; 
	return (radio_base);
}



int main()
{
	unsigned int count = 0;
    printf("hello I am going to read 10 seconds worth of data now…\n");
	

	volatile unsigned int *periph_base = get_a_pointer(FIFO_BASE_ADDRESS);	
	unsigned int word;
	unsigned int FIFO_COUNT_REG;

	FIFO_COUNT_REG =*(periph_base + RX_FIFO_OCCUPANCY_OFFSET);
	while( (count < TARGET_WORDS) )
	{
		FIFO_COUNT_REG =*(periph_base + RX_FIFO_OCCUPANCY_OFFSET);
		
		if ( FIFO_COUNT_REG > 0)
		{			
            word = *(periph_base + RX_FIFO_DATA_OFFSET);
            // printf("FIFO_COUNT_REG = %u\n", FIFO_COUNT_REG);
			(void)word;  // or store/process it
			
			count ++;
			
			// printf("Count = %u\n", count);
			
		}

	}


    printf("Finished! Read done.\n");
    return 0;
}
