/* Generated by gliss-attr ($(date)) copyright (c) 2009 IRIT - UPS */

#include <$(proc)/api.h>
#include <$(proc)/id.h>
#include <$(proc)/macros.h>
#include <$(proc)/grt.h>

#ifdef __cplusplus
extern "C" {
#endif

static elm::t::uint32 op_return;
typedef void (*fun_t)($(proc)_inst_t* inst);
#define SET_OP(x)	op_return = x

/*** function definition ***/

static void armV7_op_UNKNOWN($(proc)_inst_t* inst) {
	SET_OP(1);
}

$(foreach instructions)
static void armV7_op_$(IDENT)($(proc)_inst_t* inst) {
	$(arm7_op)
};

$(end)


/*** function table ***/
static fun_t n_op_funs[] = {
	armV7_op_UNKNOWN$(foreach instructions),
	armV7_op_$(IDENT)$(end)
};

/**
 * Get the armV7 op.
 * @return armV7 op.
 */
static elm::t::uint32 armV7_NReg(void* _inst) {
	arm_inst_t* inst = static_cast<arm_inst_t*>(_inst);
	n_op_funs[inst->ident](inst);
	return op_return;
}

#ifdef __cplusplus
}
#endif
