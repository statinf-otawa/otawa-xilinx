typedef enum {
	FE    = 0,
	DE    = 1,
	EXE   = 2,
	WR    = 3,
	CNT   = 4
} pipeline_stage_t;

typedef enum {
	NORMAL_REG = 0,
	LATE_REG = 1,
	EARLY_REG = 2,
	VERY_EARLY_REG = 3,
	UNDEFINED = 4
} operand_type_t;


const t::uint32
	NONE  = 0x00,
	MULTI = 0x01, // Not needed for now. gliss2|nmp file should maybe be updated first.
	LOAD  = 0x02,
	STORE = 0x04,
	SWP   = 0x10; // TODO: doc about this : URGENT

typedef struct {
	int ex_cost; // Is "cycle" value in Arm cortexr5_trm, page 425. 
	int result_latency; // Is "Result latency" value in Arm cortexr5_trm, page 425.
	int br_penalty; 
	operand_type_t operand_type;
	t::uint32 flags;
} xilinx_r5_time_t;


xilinx_r5_time_t time_parallel_data = {1, 1, 1, UNDEFINED, NONE                };
xilinx_r5_time_t time_data_normal 	= {1, 1, 1, NORMAL_REG, NONE               };
xilinx_r5_time_t time_data_shift 	= {1, 1, 1, EARLY_REG, NONE                };
xilinx_r5_time_t time_data_pc 		= {9, 1, 1, NORMAL_REG, NONE               };
xilinx_r5_time_t time_data_shift_pc	= {9, 1, 1, EARLY_REG, NONE                };
xilinx_r5_time_t time_ldr_normal 	= {3, 4, 1, VERY_EARLY_REG, LOAD           };
xilinx_r5_time_t time_ldr_unaligned	= {4, 5, 1, VERY_EARLY_REG, LOAD           }; // This needs to be rewritten. Separate amd1c from amd3c
xilinx_r5_time_t time_ldr_pc 		= {11, 11, 1, VERY_EARLY_REG, LOAD         }; // This needs to be rewritten. Separate amd1c from amd3c
xilinx_r5_time_t time_str_normal	= {3, 4, 1, LATE_REG, STORE                };
xilinx_r5_time_t time_ldm_x1 		= {1, 2, 1, VERY_EARLY_REG, LOAD           };
xilinx_r5_time_t time_ldm_xn 		= {4, 5, 1, VERY_EARLY_REG, LOAD | MULTI   }; // LDM (xn).	 We will need n value. if not possible, consider max=7
xilinx_r5_time_t time_ldm_xn_pc		= {1+8, 9, 1, VERY_EARLY_REG, LOAD | MULTI }; // LDM (xn, pc)
xilinx_r5_time_t time_stm_x1 		= {1, 2, 1, NORMAL_REG, STORE              }; // STM (x1)
xilinx_r5_time_t time_stm_xn		= {4, 5, 1, LATE_REG, STORE | MULTI        };
xilinx_r5_time_t time_swp 			= {2, 3, 1, VERY_EARLY_REG, SWP            };
xilinx_r5_time_t time_branch 		= {1, 1, 8, UNDEFINED, NONE                };
xilinx_r5_time_t time_branch_x	    = {1, 1, 9, UNDEFINED, NONE                };
xilinx_r5_time_t time_tbb_tbh       = {9, 9, 9, UNDEFINED, NONE                };
xilinx_r5_time_t time_mult	 		= {2, 3, 1, EARLY_REG, NONE                };
xilinx_r5_time_t time_mult_long		= {2, 3, 1, EARLY_REG, NONE                };
xilinx_r5_time_t time_unexec 		= {10, 10, 1, VERY_EARLY_REG, NONE         };
#include "xilinxR5_time.h"