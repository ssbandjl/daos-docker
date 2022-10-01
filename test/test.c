#include <stdio.h>


typedef __uint32_t uint32_t;

struct d_binheap_node {
	/** Index into the binary tree */
	uint32_t	chn_idx;
};

#define DBH_SHIFT	(9)
#define DBH_SIZE	(1U << DBH_SHIFT)	/* #ptrs per level */
#define DBH_MASK	(DBH_SIZE - 1)
#define DBH_NOB		(DBH_SIZE * sizeof(struct d_binheap_node *))


enum crt_init_flag_bits {
	/**
	 * When set enables the server mode which listens
	 * for incoming requests. Clients should not set this flag
	 */
	CRT_FLAG_BIT_SERVER		= 1U << 0,

	/**
	 * When set, disables automatic SWIM start-up at init time.
	 * Instead SWIM needs to be enabled via \ref crt_swim_init()
	 * call.
	 */
	CRT_FLAG_BIT_AUTO_SWIM_DISABLE	= 1U << 1,
};



int main(){
  // printf("DBH_SIZE: %d\n", DBH_SIZE);
  printf("CRT_FLAG_BIT_SERVER:%d\n", CRT_FLAG_BIT_SERVER);
  printf("CRT_FLAG_BIT_AUTO_SWIM_DISABLE:%d\n", CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
  uint32_t	 init_flags = 0;
  init_flags |= (CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);   
  printf("init_flags:%d\n", init_flags);
}