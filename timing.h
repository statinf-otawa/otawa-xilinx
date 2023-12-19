// TODO: Rewrite all instruction timing
typedef enum {
	NO  = 0,
	FE  = 1,
	EXE = 2,
	WR  = 3,
	CNT = 3
} pipeline_stage_t;

// const t::uint32
// 	MULTI = 0x01,
// 	LOAD = 0x02,
// 	STORE = 0x04,
// 	MULT = 0x08,
// 	SWP = 0x10;

typedef struct {
	int ex_cost; // Is "cycle" value in Arm cortexr5_trm, page 425
	int me_cost; // Is "memory cycle" value in Arm cortexr5_trm, page 425
	int result_latency; // Is "Result latency" value in Arm cortexr5_trm, page 425
	int br_penalty; // TODO: find where  it is defined in datasheet
} xilinx_r5_time_t;

// TODO: Rewrite the next 18 lines
xilinx_r5_time_t time_parallel_data = {/* EXE, NO, */ 1, 1, 1, 1/*, MULTI        */};		    // Parallel Data
xilinx_r5_time_t time_data_normal 	= {/* EXE, NO, */ 1, 1, 1, 1/*, 0            */};				// Data Op (normal)	... doc 7,428 
xilinx_r5_time_t time_data_shift 	= {/* EXE, NO, */ 2, 1, 1, 1/*, 0            */};				// Data Op (shift)	2c in EXE
xilinx_r5_time_t time_data_pc 		= {/* NO, EXE, */ 1, 1, 1, 1/*, 0            */};
xilinx_r5_time_t time_data_shift_pc	= {/* NO, EXE, */ 9, 1, 1, 1/*, 0            */};
xilinx_r5_time_t time_ldr_normal 	= {/* EXE, NO, */ 1, 1, 1, 1/*, LOAD         */};			// LDR (normal)		register produced in EXE, possible bus contention
xilinx_r5_time_t time_ldr_unaligned	= {/* EXE, NO, */ 2, 1, 1, 1/*, LOAD         */};			// LDR (unaligned)	register produced in EXE, 2c in EXE, possible bus contention on 1c
xilinx_r5_time_t time_ldr_pc 		= {/* NO, EXE, */ 1, 1, 1, 1/*, LOAD         */};			// LDR (to pc)		4c in EXE, (cur.EXE, next.FE), possible bus contention on 1c
xilinx_r5_time_t time_str_normal	= {/* NO, NO,  */ 1, 1, 1, 1/*, STORE        */};			// STR				possible bus contention on EXE
xilinx_r5_time_t time_ldm_x1 		= {/* EXE, NO, */ 1, 1, 1, 1/*, LOAD         */};			// LDM	(x1)		2c in EXE, load on 1c, possible bus contention
xilinx_r5_time_t time_ldm_xn 		= {/* EXE, NO, */ 1, 1, 1, 1/*, LOAD | MULTI */};	// LDM (xn)			nc in EXE, load on 1c, possible bus contention
xilinx_r5_time_t time_ldm_xn_pc		= {/* EXE, EXE,*/ 1, 1, 1, 1/*, LOAD | MULTI */};	// LDM (xn, pc)		nc + 4 in EXE, load on 1c, possible bus contention, (cur.EXE, next.FE)
xilinx_r5_time_t time_stm_x1 		= {/* NO, NO,  */ 1, 1, 1, 1/*, STORE        */};			// STM (x1)			2c in EXE, possible bus contention
xilinx_r5_time_t time_stm_xn		= {/* NO, NO,  */ 1, 1, 1, 1/*, STORE | MULTI*/};	// STM (xn)			nc in EXE, possible bus contention
xilinx_r5_time_t time_swp 			= {/* EXE, NO, */ 1, 1, 1, 1/*, SWP          */};			// SWP (normal)		2c in EXE, possible bus contention
xilinx_r5_time_t time_branch 		= {/* NO, EXE, */ 1, 1, 1, 1/*, 0            */};				// B, SWI			2c in EXE
xilinx_r5_time_t time_mult	 		= {/* EXE, NO, */ 5, 1, 1, 1/*, 0            */};				// MUL, MULA		2 + m in EXE
xilinx_r5_time_t time_mult_long		= {/* EXE, NO, */ 6, 1, 1, 1/*, 0            */};				// MUL long			3 + m
xilinx_r5_time_t time_unexec 		= {/* NO, NO,  */ 1, 1, 1, 1/*, 0            */};				// conditional instruction not executed
#include "xilinxR5_time.h"