// TODO: Rewrite all instruction timing. Re-read the doc: URGENT
typedef enum {
	FE    = 0,
	DE    = 1,
	EXE_1 = 2,
	EXE_2 = 3,
	WR    = 4,
	CNT   = 5
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
	MULTI = 0x01, // Not needed for now. gliss2|nmp file should maybe be updated first. TODO:  Re-read the doc: URGENT
	LOAD  = 0x02,
	STORE = 0x04,
	SWP   = 0x10; // TODO: doc about this : URGENT

typedef struct {
	int ex_cost; // Is "cycle" value in Arm cortexr5_trm, page 425. "This is the minimum number of cycles required to issue an instruction. Issue cycles that produce memory
				 // accesses to the cache are included"
	int result_latency; // Is "Result latency" value in Arm cortexr5_trm, page 425. TODO: this is not maybe required. re-read the doc. URGENT!!!
	int br_penalty; // TODO: find where  it is defined in datasheet
	operand_type_t operand_type;
	t::uint32 flags;
} xilinx_r5_time_t;

// TODO: Rewrite the next 18 lines
// TODO: branch instructions need to be rewritten with more granularity
xilinx_r5_time_t time_parallel_data = {/* EXE, NO, */ 1, 1, 1, /*1,*/UNDEFINED, MULTI        };		    // Parallel Data
xilinx_r5_time_t time_data_normal 	= {/* EXE, NO, */ 1, 1, 1, /*1,*/NORMAL_REG, NONE            };				// Data Op (normal)	... doc 7,428 
xilinx_r5_time_t time_data_shift 	= {/* EXE, NO, */ 2, 1, 1, /*1,*/EARLY_REG, NONE            };				// Data Op (shift)	2c in EXE
xilinx_r5_time_t time_data_pc 		= {/* NO, EXE, */ 1, 1, 1, /*1,*/NORMAL_REG, NONE            };
xilinx_r5_time_t time_data_shift_pc	= {/* NO, EXE, */ 9, 1, 1, /*1,*/EARLY_REG, NONE            };
xilinx_r5_time_t time_ldr_normal 	= {/* EXE, NO, */ 1, 1, 1, /*1,*/VERY_EARLY_REG, LOAD         };			// LDR (normal)		register produced in EXE, possible bus contention
xilinx_r5_time_t time_ldr_unaligned	= {/* EXE, NO, */ 2, 1, 1, /*1,*/UNDEFINED, LOAD         };			// LDR (unaligned)	register produced in EXE, 2c in EXE, possible bus contention on 1c
xilinx_r5_time_t time_ldr_pc 		= {/* NO, EXE, */ 1, 1, 1, /*1,*/VERY_EARLY_REG, LOAD         };			// LDR (to pc)		4c in EXE, (cur.EXE, next.FE), possible bus contention on 1c
xilinx_r5_time_t time_str_normal	= {/* NO, NO,  */ 1, 1, 1, /*1,*/LATE_REG, STORE        };			// STR				possible bus contention on EXE
xilinx_r5_time_t time_ldm_x1 		= {/* EXE, NO, */ 1, 1, 1, /*1,*/VERY_EARLY_REG, LOAD         };			// LDM	(x1)		2c in EXE, load on 1c, possible bus contention
xilinx_r5_time_t time_ldm_xn 		= {/* EXE, NO, */ 1, 1, 1, /*1,*/VERY_EARLY_REG, LOAD | MULTI };	// LDM (xn)			nc in EXE, load on 1c, possible bus contention. We will need N value. if not possible, consider max=4
xilinx_r5_time_t time_ldm_xn_pc		= {/* EXE, EXE,*/ 1, 1, 1, /*1,*/VERY_EARLY_REG, LOAD | MULTI };	// LDM (xn, pc)		nc + 4 in EXE, load on 1c, possible bus contention, (cur.EXE, next.FE)
xilinx_r5_time_t time_stm_x1 		= {/* NO, NO,  */ 1, 1, 1, /*1,*/NORMAL_REG, STORE        };			// STM (x1)			2c in EXE, possible bus contention
xilinx_r5_time_t time_stm_xn		= {/* NO, NO,  */ 1, 1, 1, /*1,*/LATE_REG, STORE | MULTI};	// STM (xn)			nc in EXE, possible bus contention
xilinx_r5_time_t time_swp 			= {/* EXE, NO, */ 1, 1, 1, /*1,*/VERY_EARLY_REG, SWP          };			// SWP (normal)		2c in EXE, possible bus contention
xilinx_r5_time_t time_branch 		= {/* NO, EXE, */ 1, 1, 1, /*1,*/UNDEFINED, NONE            };				// B, SWI			2c in EXE
xilinx_r5_time_t time_mult	 		= {/* EXE, NO, */ 5, 1, 1, /*1,*/EARLY_REG, NONE            };				// MUL, MULA		2 + m in EXE
xilinx_r5_time_t time_mult_long		= {/* EXE, NO, */ 6, 1, 1, /*1,*/EARLY_REG, NONE            };				// MUL long			3 + m
xilinx_r5_time_t time_unexec 		= {/* NO, NO,  */ 1, 1, 1, /*1,*/VERY_EARLY_REG, NONE            };				// conditional instruction not executed
#include "xilinxR5_time.h"