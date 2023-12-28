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
			mem = hard::MEMORY_FEATURE.get(ws);
			ASSERTP(mem, "Memory feature not found");
			cout << mem->worstWriteTime() << " " << mem->worstReadTime() << endl;
			const hard::CacheConfiguration *cconf = hard::CACHE_CONFIGURATION_FEATURE.get(ws);
			// if(!cconf)
			// 	throw Exception("no cache");
			// const hard::Cache *dcache = cconf->dataCache();
			// if (!dcache)
			// 	throw Exception("no data cache");
		}

		void addEdgesForFetch() override {
			ParExeGraph::addEdgesForFetch();

			ParExeInst* prev_inst = 0;
			for (InstIterator inst(this); inst(); inst++) {
				void* inst_info = info->decode(inst->inst());
				xilinx_r5_time_t* inst_time = xilinx_r5_time(inst_info);
				info->free(inst_info);

				// Update fetch edges with misprediction branch penalties
				if (prev_inst && prev_inst->inst()->isControl()) {
					ot::time delay;
					if (prev_inst->inst()->topAddress() != inst->inst()->address()) {  
						if (!prev_inst->inst()->target()) { // Should be true for indirect branches. TODO: check it
							delay = inst_time->br_penalty;
							if (delay >= 2) 
								new ParExeEdge(prev_inst->execNode(), inst->fetchNode(), ParExeEdge::SOLID, delay - 2, "Branch prediction");
						}
					}
				}
				prev_inst = *inst;
			}
		}

		void addEdgesForPipelineOrder() override {
			for (InstIterator inst(this); inst(); inst++) {
				void* inst_info = info->decode(inst->inst());
				xilinx_r5_time_t* inst_time = xilinx_r5_time(inst_info);
				info->free(inst_info);

				ParExeNode* prev_node = nullptr;
				// Add edges that represent the order of stages in the pipeline
				for (ParExeInst::NodeIterator node(*inst); node(); node++) {
					if (prev_node)
						new ParExeEdge(prev_node, *node, ParExeEdge::SOLID, 0, "pipeline order");
					if (node->stage()->name().startsWith("EXEC_")) {  // execution stage 
						node->setLatency(inst_time->ex_cost); // TODO: find why sometimes, the node latency is increased: ben
						cout << node->latency() << " " << node->stage()->name() <<endl;
					}
					prev_node = *node;
				}
			}
		}

		void addEdgesForMemoryOrder() override {
			// Call the default implementation
			ParExeGraph::addEdgesForMemoryOrder();

			// Now add edges between consecutive reads
		
			// """ The Cortex-R5 processor has an in-order pipeline, so any non-cached read blocks,
			// preventing any subsequent read or write from starting until the current read is complete. """"
			// 'write after read' are already handled by the default implementation.
			// The next lines will add the support of 'read after read'.
			
			static string memory_order = "memory order";
			auto stage = _microprocessor->execStage();

			// looking in turn each FU
			for (int i=0 ; i<stage->numFus() ; i++) {
				ParExeStage* fu_stage = stage->fu(i)->firstStage();
				ParExeNode* previous_load = nullptr;

				// look for each node of this FU
				for (int j=0 ; j<fu_stage->numNodes() ; j++){
					ParExeNode* node = fu_stage->node(j);
					// found a load instruction
					if (node->inst()->inst()->isLoad()) {
						// if any, add dependency on the previous load
						if (previous_load)
							new ParExeEdge(previous_load, node, ParExeEdge::SOLID, 0, memory_order);
						
						// current node becomes the new previous load
						for (InstNodeIterator last_node(node->inst()); last_node() ; last_node++)
							if (last_node->stage()->category() == ParExeStage::FU)
								previous_load = *last_node;
					}
				}
			}
		}

		void build(void) override {			
			// Look for FUs
			stage[NO] = 0;
			for(ParExePipeline::StageIterator pipeline_stage(_microprocessor->pipeline()); pipeline_stage(); pipeline_stage++) {
				if(pipeline_stage->name() == "PreFetch") {
					stage[FE] = *pipeline_stage;
				} else if(pipeline_stage->name() == "EXE") {
					// stage[EXE] = *pipeline_stage;
					for(int i = 0; i < pipeline_stage->numFus(); i++) {
						ParExePipeline *fu = pipeline_stage->fu(i);
						if (fu->firstStage()->name().startsWith("EXEC_F")) {
							exec_f_fu = fu;
							stage[EXE] = fu->firstStage();
						}
						else if (fu->firstStage()->name().startsWith("EXEC_DPU")) {
							exec_dpu_fu = fu;
							stage[EXE] = fu->firstStage();
						}
						else if (fu->firstStage()->name().startsWith("EXEC_LSU")) {
							exec_lsu_fu = fu;
							stage[EXE] = fu->firstStage();
						}
						else
							ASSERTP(false, fu->firstStage()->name());
						
					}
				} else if(pipeline_stage->name() == "WR") {
					stage[WR] = *pipeline_stage;
				}
			}
			ASSERTP(stage[FE], "No 'Prefetch' stage found");
			ASSERTP(stage[EXE], "No 'EXE' stage found");
			ASSERTP(stage[WR], "No 'Write back' stage found"); // This is maybe not required
			ASSERTP(exec_f_fu, "No FPU fu found");
			ASSERTP(exec_dpu_fu, "No DPU fu found");
			ASSERTP(exec_lsu_fu, "No LSU fu found");

			// Build the execution graph 
			createSequenceResources();
			createNodes();
			addEdgesForPipelineOrder();
			addEdgesForFetch();
			addEdgesForProgramOrder();
			addEdgesForMemoryOrder();
			addEdgesForDataDependencies(); // TODO: rewrite and add latency values. data cache support pending
			addEdgesForQueues();
		}

		
	private:
		otawa::arm::Info* info;
		const hard::Memory *mem;
		ParExeStage* stage[CNT];
		ParExePipeline *exec_f_fu, *exec_dpu_fu, *exec_lsu_fu;
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

