/* Generated by gliss-attr ($(date)) copyright (c) 2009 IRIT - UPS */

#include <$(proc)/api.h>
#include <$(proc)/id.h>
#include <$(proc)/macros.h>
#include <$(proc)/grt.h>

#ifdef __cplusplus
extern "C" {
#endif

xilinx_a9_time_t *A9_time_return;
typedef void (*fun_t)($(proc)_inst_t *inst);
#define SET_TIME(x)	A9_time_return = &x

/*** function definition ***/

static void xilinx_a9_time_UNKNOWN($(proc)_inst_t *inst) {
	SET_TIME(A9_time_unknown);
}

$(foreach instructions)
static void xilinx_a9_time_$(IDENT)($(proc)_inst_t *inst) {
$(cortexA9_time)
};

$(end)


/*** function table ***/
static fun_t time_funs[] = {
	xilinx_a9_time_UNKNOWN$(foreach instructions),
	xilinx_a9_time_$(IDENT)$(end)
};

/**
 * Get the xilinx_a9 timing.
 * @return xilinx_a9 timing.
 */
xilinx_a9_time_t *xilinxA9Time(void *_inst) {
	arm_inst_t *inst = static_cast<arm_inst_t *>(_inst);
	time_funs[inst->ident](inst);
	return A9_time_return;
}

#ifdef __cplusplus
}
#endif
