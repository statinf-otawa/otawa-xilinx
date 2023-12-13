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

#include <otawa/etime/EdgeTimeBuilder.h>
#include <otawa/prop/DynIdentifier.h>
#include <otawa/loader/arm.h>
#include "timing.h"


namespace otawa { namespace xilinxR5 {
	extern p::id<int> INSTRUCTION_TIME; // instruction cost in cycles

	/*
	* This plugin contains several xilinxR5 analyzes for WCET computation
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
	* @ingroup xilinxR5
	*/

	class WCETDefByIT : public BBProcessor {
	public:
		static p::declare reg;

		WCETDefByIT(p::declare &r = reg) : BBProcessor(r), itime(5) { }

		virtual void configure(const PropList &props) {
			BBProcessor::configure(props);
			itime = INSTRUCTION_TIME(props);
		}

	protected:
		virtual void processBB(WorkSpace *fw, CFG *cfg, Block *bb) {
			if (!bb->isBasic())
				ipet::TIME(bb) = 0;
			else
				ipet::TIME(bb) = itime * bb->toBasic()->count();
		}

		virtual void collectStats(WorkSpace *ws) {
			record(new ipet::TimeStat(ws));
		}

		virtual void destroy(WorkSpace *ws, CFG *cfg, Block *b) {
			ipet::TIME(b).remove();
		}

	private:
		int itime;
	};

	p::declare WCETDefByIT::reg = p::init("otawa::xilinxR5::WCETDefByIT", Version(1, 0, 0))
		.base(BBProcessor::reg)
		.maker<WCETDefByIT>()
		.provide(ipet::BB_TIME_FEATURE);

	// This configuration property provides the time of an instruction in cycles.
	p::id<int> INSTRUCTION_TIME("otawa::xilinxR5::INSTRUCTION_TIME", 5);


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

					// Load and Store Unit
					if (node->stage() == stage[LSU]) {
						node->setLatency(inst_time->me_cost);

						// Dependencies support
						if (prev_mem) {
							new ParExeEdge(prev_mem, *node, ParExeEdge::SOLID);
						}
						prev_mem = *node;
						// TODO: Add support for data cache
						
					}
				}
				
			}
		}

	private:
		otawa::arm::Info* info;
		ParExeStage* stage[CNT];
		ParExePipeline *exec_f_fu, *exec_dpu_fu;
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
		}

		etime::EdgeTimeGraph* make(ParExeSequence* seq) override {
			ExeGraph* graph = new ExeGraph(workspace(), _microprocessor, ressources(), seq, _props);
			graph->build();
			return graph;
		}
		virtual void clean(ParExeGraph* graph) {
			delete graph;
		}
	private:
		PropList _props;
	};

	p::declare BBTimerXilinxR5::reg = p::init("otawa::xilinxR5::BBTimerXilinxR5", Version(1, 0, 0))
										.extend<etime::EdgeTimeBuilder>()
										.maker<BBTimerXilinxR5>();
	
	/* plugin hook */
	ProcessorPlugin plugin = sys::Plugin::make("otawa::xilinxR5", OTAWA_PROC_VERSION)
										.version(Version(1, 0, 0))
										.hook(OTAWA_PROC_NAME);
	ELM_PLUGIN(plugin, OTAWA_PROC_HOOK);

} // namespace xilinxR5
} // namespace otawa

