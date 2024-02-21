typedef struct {
    int ex_cost;
    int result_latency;
    bool unknown        = false;
} xilinx_a9_time_t;


xilinx_a9_time_t A9_time_mov_normal            = {1, 1};
xilinx_a9_time_t A9_time_mov_reg_shift         = {2, 2};
xilinx_a9_time_t A9_time_mov_imm_shift         = {1, 1};
xilinx_a9_time_t A9_time_simpl_data_normal     = {1, 1};
xilinx_a9_time_t A9_time_simpl_data_imm_shift  = {2, 2};
xilinx_a9_time_t A9_time_simpl_data_reg_shift  = {3, 3};
xilinx_a9_time_t A9_time_huge_data_normal      = {2, 2};
xilinx_a9_time_t A9_time_huge2_data_normal     = {3, 3};
xilinx_a9_time_t A9_time_ussat16_data_normal   = {1, 1};
xilinx_a9_time_t A9_time_ussat_data_normal     = {3, 3};
xilinx_a9_time_t A9_time_pkhtb_pkhbt           = {2, 2};
xilinx_a9_time_t A9_time_su_data_normal        = {1, 1};
xilinx_a9_time_t A9_time_usxtb_data_normal     = {2, 2};
xilinx_a9_time_t A9_time_usxtab_data_normal    = {3, 3};
xilinx_a9_time_t A9_time_ubbfx_data_normal     = {2, 2};
xilinx_a9_time_t A9_time_other_data_normal     = {1, 1};
xilinx_a9_time_t A9_time_single_ldr            = {1, 3};
xilinx_a9_time_t A9_time_single_ldrb_ldrh      = {2, 5};
xilinx_a9_time_t A9_time_multiple_ldr          = {0, 3}; // the real value of ex_cost and result_latency are respectively : len(RegList)/2 +1 and 3 + len(RegList) - 1
xilinx_a9_time_t A9_time_single_str            = {1, 3};
xilinx_a9_time_t A9_time_multiple_str          = {0, 3}; // the real value of ex_cost and result_latency are respectively : len(RegList)/2 +1 and 3 + len(RegList) - 1
xilinx_a9_time_t A9_time_single_strb_strh      = {2, 5};
xilinx_a9_time_t A9_time_simple_mult           = {2, 4};
xilinx_a9_time_t A9_time_mul_x                 = {3, 5};
xilinx_a9_time_t A9_time_branch                = {5, 5}; // This is overestimated because, the doc has not provided explicit information about cycle timing of branch instructions
xilinx_a9_time_t A9_time_unknown 		       = {15, 15, true};
#include "armCortexA9_time.h"
