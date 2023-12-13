/*
 *	xilinx_r5 module implementation
 *
 *	This file is part of OTAWA
 *	Copyright (c) 2017, IRIT UPS.
 *
 *	OTAWA is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	OTAWA is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with OTAWA; if not, write to the Free Software
 *	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <otawa/branch/features.h>
#include <otawa/hard/BHT.h>
#include <otawa/proc/DynProcessor.h>
#include <otawa/cache/features.h>
#include <otawa/cfg.h>
#include <otawa/hard/CacheConfiguration.h>
#include <otawa/hard/Memory.h>
#include <otawa/icache/features.h>
#include <otawa/ilp/expr.h>
#include <otawa/ipet.h>
#include <otawa/proc/BBProcessor.h>
#include <otawa/proc/ProcessorPlugin.h>
#include <otawa/stats/BBStatCollector.h>
#include <otawa/prop/Identifier.h>
#include <otawa/prop/Property.h>
#include <otawa/prop/PropList.h>
#include <otawa/etime/EdgeTimeBuilder.h>
#include <otawa/loader/arm.h>
#include <otawa/parexegraph/BasicGraphBBTime.h>
#include <otawa/parexegraph/ParExeGraph.h>
#include <otawa/parexegraph/ParExeProc.h>
#include <otawa/etime/EdgeTimeBuilder.h>
#include <otawa/loader/gliss.h>
#include <otawa/prop/DynIdentifier.h>
#include <otawa/loader/arm.h>
#include <arm/api.h>
#include <otawa/cache/features.h>

/*** timings ***/
typedef enum {
	NO = 0,
	FE = 1,
	EXE = 2,
	LSU = 3,
	CNT = 3
} pipeline_stage_t;

const t::uint32
	MULTI = 0x01,
	LOAD = 0x02,
	STORE = 0x04,
	MULT = 0x08,
	SWP = 0x10;

typedef struct {
	pipeline_stage_t prod;
	pipeline_stage_t branch;
	int ex_cost; // result latency
	int me_cost;
	t::uint32 flags;
} xilinx_r5_time_t;
// TODO: Rewrite the next 18 lines
xilinx_r5_time_t time_parallel_data = { EXE, NO, 1, 1, MULTI};		    // Parallel Data
xilinx_r5_time_t time_data_normal 	= { EXE, NO, 1, 1, 0 };				// Data Op (normal)	... doc 7,428 
xilinx_r5_time_t time_data_shift 	= { EXE, NO, 2, 1, 0 };				// Data Op (shift)	2c in EXE
xilinx_r5_time_t time_data_pc 		= { NO, EXE, 1, 1, 0 };
xilinx_r5_time_t time_data_shift_pc	= { NO, EXE, 9, 1, 0 };
xilinx_r5_time_t time_ldr_normal 	= { LSU, NO, 1, 1, LOAD };			// LDR (normal)		register produced in LSU, possible bus contention
xilinx_r5_time_t time_ldr_unaligned	= { LSU, NO, 2, 1, LOAD };			// LDR (unaligned)	register produced in LSU, 2c in LSU, possible bus contention on 1c
xilinx_r5_time_t time_ldr_pc 		= { NO, LSU, 1, 1, LOAD };			// LDR (to pc)		4c in LSU, (cur.LSU, next.FE), possible bus contention on 1c
xilinx_r5_time_t time_str_normal	= { NO, NO, 1, 1, STORE };			// STR				possible bus contention on LSU
xilinx_r5_time_t time_ldm_x1 		= { LSU, NO, 1, 1, LOAD };			// LDM	(x1)		2c in LSU, load on 1c, possible bus contention
xilinx_r5_time_t time_ldm_xn 		= { LSU, NO, 1, 1, LOAD | MULTI };	// LDM (xn)			nc in LSU, load on 1c, possible bus contention
xilinx_r5_time_t time_ldm_xn_pc		= { LSU, LSU, 1, 1, LOAD | MULTI };	// LDM (xn, pc)		nc + 4 in LSU, load on 1c, possible bus contention, (cur.LSU, next.FE)
xilinx_r5_time_t time_stm_x1 		= { NO, NO, 1, 1, STORE };			// STM (x1)			2c in LSU, possible bus contention
xilinx_r5_time_t time_stm_xn			= { NO, NO, 1, 1, STORE | MULTI };	// STM (xn)			nc in LSU, possible bus contention
xilinx_r5_time_t time_swp 			= { LSU, NO, 1, 1, SWP };			// SWP (normal)		2c in LSU, possible bus contention
xilinx_r5_time_t time_branch 		= { NO, EXE, 1, 1, 0 };				// B, SWI			2c in EXE
xilinx_r5_time_t time_mult	 		= { EXE, NO, 5, 1, 0 };				// MUL, MULA		2 + m in EXE
xilinx_r5_time_t time_mult_long		= { EXE, NO, 6, 1, 0 };				// MUL long			3 + m
xilinx_r5_time_t time_unexec 		= { NO, NO, 1, 1, 0 };				// conditional instruction not executed
#include "r5_time.h"


namespace otawa { namespace xilinx_r5 {
		using namespace ilp;
		extern p::id<int> INSTRUCTION_TIME;

		/*
		 * This plugin contains several xilinx_r5 analyzes for WCET computation
		 */

		/**
		 *			 WCETDefByIT Analysis:
		 * Compute the time of blocks considering that each instruction takes the same amount of time (default to 5 cycles)
		 * or as defined by the configuration property @ref INSTRUCTION_TIME.
		 *
		 * Required features:
		 * @li @ref otawa::COLLECTED_CFG_FEATURE
		 *
		 * Provided features:
		 * @li @ref ipet::BB_TIME_FEATURE
		 *
		 * Configuration properties:
		 * @li @ref INSTRUCTION_TIME
		 *
		 * @ingroup xilinx_r5
		 */

		class WCETDefByIT : public BBProcessor
		{
		public:
			static p::declare reg;

			WCETDefByIT(p::declare &r = reg) : BBProcessor(r), itime(5) {}

			virtual void configure(const PropList &props)
			{
				BBProcessor::configure(props);
				itime = INSTRUCTION_TIME(props);
			}

		protected:
			virtual void processBB(WorkSpace *fw, CFG *cfg, Block *bb)
			{
				if (!bb->isBasic())
					ipet::TIME(bb) = 0;
				else
					ipet::TIME(bb) = itime * bb->toBasic()->count();
			}

			virtual void collectStats(WorkSpace *ws)
			{
				record(new ipet::TimeStat(ws));
			}

			virtual void destroy(WorkSpace *ws, CFG *cfg, Block *b)
			{
				ipet::TIME(b).remove();
			}

		private:
			int itime;
};

p::declare WCETDefByIT::reg = p::init("otawa::xilinx_r5::WCETDefByIT", Version(1, 0, 0))
	.base(BBProcessor::reg)
	.maker<WCETDefByIT>()
	.provide(ipet::BB_TIME_FEATURE);

// This configuration property provides the time of an instruction in cycles.

p::id<int> INSTRUCTION_TIME("otawa::xilinx_r5::INSTRUCTION_TIME", 5);


class ExeGraph: public etime::EdgeTimeGraph {
public:

	ExeGraph(WorkSpace *ws, ParExeProc *proc, Vector<Resource *> *hw_resources, 
				ParExeSequence *seq, const PropList &props) : etime::EdgeTimeGraph(ws, proc, hw_resources, seq, props), exec_dpu_fu(0), exec_f_fu(0) {
		
		// Try to find arm loader with arm information
		DynIdentifier<arm::Info *> id("otawa::arm::Info::ID");
		info = id(_ws->process());
		if(!info)
			throw Exception("ARM loader with otawa::arm::INFO is required !");
	}

	void build(void) override {
		otawa::ParExeGraph::build();
		
		// Look for FU
		stage[NO] = 0;
		for(ParExePipeline::StageIterator pipeline_stage(_microprocessor->pipeline()); pipeline_stage(); pipeline_stage++) {
			if(pipeline_stage->name() == "PreFetch") {
				stage[FE] = *pipeline_stage;
			} else if(pipeline_stage->name() == "EXE") {
				stage[EXE] = *pipeline_stage;
				for(int i = 0; i < pipeline_stage->numFus(); i++) {
					ParExePipeline *fu = pipeline_stage->fu(i);
					if (fu->firstStage()->name().startsWith("EXEC_F"))
						exec_f_fu = fu;
					else if (fu->firstStage()->name().startsWith("EXEC_DPU"))
						exec_dpu_fu = fu;
					else
						ASSERTP(false, fu->firstStage()->name());
				}
			} else if(pipeline_stage->name() == "LSU") {
				stage[LSU] = *pipeline_stage;
			}
		}
		ASSERT(exec_f_fu);
		ASSERT(exec_dpu_fu);
		ASSERTP(stage[FE], "No 'Prefetch' stage found");
		ASSERTP(stage[EXE], "No 'EXE' stage found");
		ASSERTP(stage[LSU], "No 'Load and Store Unit' stage found");

		// Register usage
		const hard::Register* pc = _ws->process()->platform()->getPC();
		int rn = _ws->process()->platform()->regCount();
		ParExeNode* regs[rn];
		elm::array::set(regs, rn, static_cast<ParExeNode *>(0));

		ParExeNode *branch = 0, *prev_mem = 0, *prev = 0;
		bool prev_is_mult = false, prev_is_mem = false;
		for(InstIterator inst(this); inst(); inst++) {
			void* inst_info = info->decode(inst->inst());
			xilinx_r5_time_t *inst_time = xilinx_r5_time(inst_info);
			info->free(inst_info);

			// Process previous branch
			if(branch && branch->inst()->inst()->topAddress() != inst->inst()->topAddress()) {
				new ParExeEdge(branch, inst->firstNode(), ParExeEdge::SOLID);
				branch = 0;
			}			

			// Look in the stage
			for(ParExeInst::NodeIterator node(*inst); node(); node++) {

				// On branch stage
				if(node->stage() == stage[inst_time->branch])
					branch = *node;
				else
					branch = 0;
				// On EX stage
				if(node->stage() == stage[EXE]) {
					node->setLatency(inst_time->ex_cost);
					// look execution dependencies in EX stage
					const Array< hard::Register* >& read_reg = inst->inst()->readRegs();
					for(int i = 0; i < read_reg.count(); i++)
						if(read_reg[i] != pc && regs[read_reg[i]->platformNumber()])
							new ParExeEdge(regs[read_reg[i]->platformNumber()], *node, ParExeEdge::SOLID);
				}

				// On producing stage
				if(node->stage() == stage[inst_time->prod]) {
					const Array< hard::Register* >& written_reg = inst->inst()->writtenRegs();
					for(int i = 0; i < written_reg.count(); i++)
						if(written_reg[i] != pc) {
							regs[written_reg[i]->platformNumber()] = *node;
						}
				}
# if 1
				// Load and Store Unit
				if (node->stage() == stage[LSU]) {
					node->setLatency(inst_time->me_cost);

					// Dependencies support
					if (prev_mem) {
						new ParExeEdge(prev_mem, *node, ParExeEdge::SOLID);
					}
					prev_mem = *node;
					// if ((inst_time->flags & LOAD) || (inst_time->flags & STORE))
					// 	node->setLatency(node->latency() + elm::ones(info->multiMask(inst->inst())) - 1);
				}
# endif
			}
			
		}
	}

private:
	otawa::arm::Info *info;
	ParExeStage* stage[CNT];
	ParExePipeline* exec_f_fu, *exec_dpu_fu;
};


class BBTimerXilinxR5: public etime::EdgeTimeBuilder {
public:
	static p::declare reg;
	BBTimerXilinxR5(void): etime::EdgeTimeBuilder(reg) { }

protected:
	virtual void configure(const PropList& props) {
		etime::EdgeTimeBuilder::configure(props);
		_props = props;
	}
	void setup(WorkSpace* ws) override {
		etime::EdgeTimeBuilder::setup(ws);

		// icache = hard::CACHE_CONFIGURATION_FEATURE.get(ws)->instCache();
		// if(!icache)
		// 	throw ProcessorException(*this, "no instruction cache available");
		// dcache = hard::CACHE_CONFIGURATION_FEATURE.get(ws)->dataCache();
		// if(!dcache)
		// 	throw ProcessorException(*this, "no data cache available");
	}
    // etime::EdgeTimeGraph *make(ParExeSequence *seq) override {
	// 	PropList props;
	// 	etime::EdgeTimeGraph *graph = new etime::EdgeTimeGraph(workspace(), _microprocessor, ressources(), seq, props);
	// 	graph->build();
	// 	return graph;
	// }
	etime::EdgeTimeGraph *make(ParExeSequence *seq) override {
		ExeGraph* graph = new ExeGraph(workspace(), _microprocessor, ressources(), seq, _props);
		graph->build();
		return graph;
	}
	virtual void clean(ParExeGraph *graph) {
		delete graph;
	}
private:
	const hard::Cache *icache, *dcache;
	PropList _props;
};

p::declare BBTimerXilinxR5::reg = p::init("otawa::xilinx_r5::BBTimerXilinxR5", Version(1, 0, 0))
							.extend<etime::EdgeTimeBuilder>()
							.require(hard::MEMORY_FEATURE)
	.require(hard::CACHE_CONFIGURATION_FEATURE)
	// .require(ipet::ASSIGNED_VARS_FEATURE)
	// .require(ipet::INST_CACHE_SUPPORT_FEATURE)
							.maker<BBTimerXilinxR5>();
/* plugin hook */
ProcessorPlugin plugin = sys::Plugin::make("otawa::xilinx_r5", OTAWA_PROC_VERSION)
	.version(Version(1, 0, 0))
	.hook(OTAWA_PROC_NAME);
ELM_PLUGIN(plugin, OTAWA_PROC_HOOK);

} // namespace xilinx_r5
} // namespace otawa

