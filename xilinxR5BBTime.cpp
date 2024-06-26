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
#include <otawa/events/StandardEventBuilder.h>
#include <otawa/etime/EdgeTimeBuilder.h>
#include <otawa/prop/DynIdentifier.h>
#include <elm/sys/Path.h>
#include <otawa/loader/arm.h>
#include <elm/io/FileOutput.h>
#include <elm/data/Vector.h>
#include "R5CycleTiming.h"
#include "armCortexR5_A9_operand.h"
#include "arm_properties.h"

#define OCM_ACCESS_LATENCY 50
#define FUs_NUM_STAGE 2

namespace otawa { namespace xilinx {
    using namespace elm::io;
	// extern p::id<bool> WRITE_LOG;
	
    typedef enum {
        FE    = 0,
        DE    = 1,
        EXE   = 2,
        WR    = 3,
        CNT   = 4
    } pipeline_stage_t;

	
	class R5FExeGraph: public etime::EdgeTimeGraph {
	public:
		
		R5FExeGraph(WorkSpace* ws,
                 ParExeProc* proc, 
                 Vector<Resource* >* hw_resources, 
				 ParExeSequence* seq,
                 const PropList &props,
                 FileOutput* out, 
                 elm::Vector<Address>* unknown_inst_address) : etime::EdgeTimeGraph(ws, proc, hw_resources, seq, props), 
                                                                exec_dpu_fu(0), exec_f_fu(0), exec_lsu_fu(0), _out(out), 
                                                                _unknown_inst_address(unknown_inst_address) {
			
			// Try to find arm loader with arm information
			DynIdentifier<arm::Info* > id("otawa::arm::Info::ID");
			info = id(_ws->process());
			if (!info)
				throw Exception("ARM loader with otawa::arm::INFO is required !");
			// Get memory configuration
			mem = hard::MEMORY_FEATURE.get(ws);
			ASSERTP(mem, "Memory feature not found");
		}

		void addEdgesForFetch() override {
			ParExeGraph::addEdgesForFetch();

			ParExeInst* prev_inst = 0;
			for (InstIterator inst(this); inst(); inst++) {
				// Update fetch edges with misprediction branch penalties
				if (prev_inst && prev_inst->inst()->isControl()) {
					ot::time delay;
					if (prev_inst->inst()->topAddress() != inst->inst()->address()) {  
						// if (!prev_inst->inst()->target()) {
							delay = getInstCycleTiming(prev_inst->inst())->br_penalty;
							if (delay >= 2) 
								new ParExeEdge(prev_inst->lastFUNode(), inst->fetchNode(), ParExeEdge::SOLID, delay - 2, "Branch prediction");
						// }
					}
				}
				prev_inst = *inst;
			}
		}

		void addEdgesForPipelineOrder() override {
			ParExeGraph::addEdgesForPipelineOrder();
			// Add latency penalty to Exec-FU nodes
			for (InstIterator inst(this); inst(); inst++) {
				// get cycle_time_info of inst
				xilinx_r5_time_t* inst_cycle_timing = getInstCycleTiming(inst->inst());
				if (inst_cycle_timing->ex_cost > 1)
                    inst->firstFUNode()->setLatency(inst_cycle_timing->ex_cost - 1);
                else
				    inst->lastFUNode()->setLatency(0);
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
		
		void addEdgesForDataDependencies() override {
			string data_dep("");
			ParExeStage* exec_stage = _microprocessor->execStage();
			// for each functional unit
			for (int j = 0; j < exec_stage->numFus(); j++) {
				ParExeStage* fu_stage = exec_stage->fu(j)->firstStage();
				
				// for each stage in the functional unit
				for (int k=0; k < fu_stage->numNodes(); k++) {
					ParExeNode* node = fu_stage->node(k);
					ParExeInst* inst = node->inst();

					// get cycle_time_info of the instruction
					xilinx_r5_time_t* inst_cycle_timing = getInstCycleTiming(inst->inst());

					// Get the dependency type of the instruction  and also calculate the ajustment value (offset)
					// of the result latency of the instruction procuding the used data
					operand_type_t reg_type = inst_cycle_timing->operand_type;
					ParExeNode* requiring_node = nullptr;
					t::int32 offset = 0;
					if (reg_type == NORMAL_REG) {
						data_dep = "NR Dependency";
						// the used data is required at EXE_2 stage
						requiring_node = inst->lastFUNode();
					} else if (reg_type == LATE_REG) {
						data_dep = "LR Dependency";
						// Subtract one cycle from the Result Latency of the instruction producing this register
						offset = -1;
						// the used data is not required until the start of wr stage
						requiring_node = findWrSstage(inst);
					} else if (reg_type == EARLY_REG) {
						data_dep = "ER Dependency";
						// Add one cycle to the Result Latency of the instruction producing this register
						offset = 1;
						// the used data is required at EXE_1 stage
						requiring_node = node;
					} else if (reg_type == VERY_EARLY_REG) {
						data_dep = "VER Dependency";
						// "Add two cycles to the Result Latency of the instruction producing this register ..."
						// But we will just overestimate here by adding 2 cycles all times
						offset = 2;
						// the used data is required at Issue(EXE_1) stage.
						requiring_node = node;
					} else if (reg_type == UNDEFINED) {
						// do nothing for now
					}

					// for each instruction producing a used data
					for (ParExeInst::ProducingInstIterator prod(inst); prod(); prod ++) {

						// get cycle_time_info of the producer instruction
						xilinx_r5_time_t* producer_cycle_timing = getInstCycleTiming(prod->inst());
						// Calculate the stall duration of the stage
						int stall_duration = producer_cycle_timing->result_latency - producer_cycle_timing->ex_cost + offset;
						// Find the stage node producing the data
						ParExeNode* producing_node = nullptr;
						if (!prod->inst()->isLoad())
							producing_node = prod->lastFUNode();
						else
							producing_node = findMemStage(*prod);
						
						// In the case of negative value of stall duration, we need to reduce the latency value of the producer node
						if (stall_duration < 0) {
							// producing_node->setLatency(producing_node->latency() - elm::abs(stall_duration));
							stall_duration = 0;
						}
						// create the edge
						if (producing_node != nullptr && requiring_node != nullptr) 
							new ParExeEdge(producing_node, requiring_node, ParExeEdge::SOLID, stall_duration, data_dep);
					}
				}
			}
		}
		/*
			For all instructions performing a read or write, 
			this function adds a penalty representing the cost of 
			accessing memory considering 'data-cache miss' 
		*/
		void addLatenciesForDataCacheMiss() {

			for (InstIterator inst(this); inst(); inst++) {
				// get cycle_time_info of inst
				xilinx_r5_time_t* inst_cycle_timing = getInstCycleTiming(inst->inst());
				if (inst_cycle_timing->flags & (STORE|LOAD)) {
					ot::time latency = getCostOfMemAccess(inst->inst()); // TOOD: rewrite getCostOfMemAccess
					ParExeNode* first_fu_node = inst->firstFUNode();
					first_fu_node->setLatency(first_fu_node->latency() + latency);
				}
			}
		}
		/*
			Write to the log file, some info about the instructions whose
			cycle timing info has not been found.
		*/
		void dumpUnknowInst() {
			if (_out == nullptr)
				return;
			for (InstIterator inst(this); inst(); inst++) {
				if (!getInstCycleTiming(inst->inst())->unknown)
					continue;
				
				auto addr = inst->inst()->address();
				if (_unknown_inst_address->contains(addr))
					continue;
				_unknown_inst_address->add(addr);
				*_out << addr << "; " << inst->inst() << endl;
			}
		}

		void build(void) override {			
			// Look for FUs
			for (ParExePipeline::StageIterator pipeline_stage(_microprocessor->pipeline()); pipeline_stage(); pipeline_stage++) {
				if (pipeline_stage->name() == "PreFetch") {
					stage[FE] = *pipeline_stage;
				} else if (pipeline_stage->name() == "Decode") {
					stage[DE] = *pipeline_stage;
				} else if (pipeline_stage->name() == "EXE") {
					// _microprocessor->setExecStage(*pipeline_stage);
					stage[EXE] = *pipeline_stage;
					for (int i = 0; i < pipeline_stage->numFus(); i++) {
						ParExePipeline* fu = pipeline_stage->fu(i);
						if (fu->firstStage()->name().startsWith("EXEC_F")) {
							exec_f_fu = fu;
						} else if (fu->firstStage()->name().startsWith("EXEC_DPU")) {
							exec_dpu_fu = fu;
						} else if (fu->firstStage()->name().startsWith("EXEC_LSU")) {
							exec_lsu_fu = fu;
						} else
							ASSERTP(false, fu->firstStage()->name());
						
					}
				} else if (pipeline_stage->name() == "Write") {
					stage[WR] = *pipeline_stage;
				} 

			}
			ASSERTP(stage[FE], "No 'Prefetch' stage found");
			ASSERTP(stage[DE], "No 'Decode' stage found");
			ASSERTP(stage[EXE], "No 'EXE' stage found");
			ASSERTP(stage[WR], "No 'Write back' stage found");
			ASSERTP(exec_f_fu, "No FPU fu found");
			ASSERTP(exec_f_fu->numStages() == FUs_NUM_STAGE, "Wrong number of stages of FPU")
			ASSERTP(exec_dpu_fu, "No DPU fu found");
			ASSERTP(exec_dpu_fu->numStages() == FUs_NUM_STAGE, "Wrong number of stages of DPU")
			ASSERTP(exec_lsu_fu, "No LSU fu found");
			ASSERTP(exec_lsu_fu->numStages() == FUs_NUM_STAGE, "Wrong number of stages of LSU")
			

			// Build the execution graph 
			createSequenceResources();
			createNodes();
			addEdgesForPipelineOrder();
			addEdgesForFetch();
			addEdgesForProgramOrder();
			addEdgesForMemoryOrder();
			addEdgesForDataDependencies();
			// addLatenciesForDataCacheMiss();
			dumpUnknowInst();
		}

		
	private:
		otawa::arm::Info* info;
		const hard::Memory* mem;
		ParExeStage* stage[CNT];
		ParExePipeline *exec_f_fu, *exec_dpu_fu, *exec_lsu_fu;
		FileOutput* _out = nullptr;
		elm::Vector<Address>* _unknown_inst_address = nullptr;
		/*
			Find the Mem stage of an instruction.
			    inst: Concerned instruction.
		*/
		ParExeNode* findMemStage(ParExeInst* inst) {
			for (ParExeInst::NodeIterator node(inst); node(); node++) {
					if (node->stage() == _microprocessor->memStage())
						return *node;
			}
			return nullptr;
		}

		/*
			Find the WB stage of an instruction.
			    inst: Concerned instruction.
		*/
		ParExeNode* findWrSstage(ParExeInst* inst) {
			for (ParExeInst::NodeIterator node(inst); node(); node++) {
					if (node->stage() == stage[WR])
						return *node;
			}
			return nullptr;
		}
		/*
			Attempts to decode an instruction and return the corresponding behavior "cycle timing behavior".
			"cycle timing behavior" of instructions are provided at the section B of 
			'Cortex ™ -R5 and Cortex-R5F Revision: r1p1 Technical Reference Manual'
			
			    inst: Instruction decode.
		*/
		xilinx_r5_time_t* getInstCycleTiming(Inst* inst) {
			void* inst_info = info->decode(inst);
			xilinx_r5_time_t* inst_cycle_timing = xilinxR5Time(inst_info);
			info->free(inst_info);
			return inst_cycle_timing;
		}

		t::uint32 getInstNReg(Inst* inst) {
			void* inst_info = info->decode(inst);
			t::uint32 inst_n_reg = armV7_NReg(inst_info);
			info->free(inst_info);
			return inst_n_reg;
		}

		ot::time getCostOfMemAccess(Inst* inst) { // TODO: not good enough
			xilinx_r5_time_t* inst_ct = getInstCycleTiming(inst);
			bool write = inst_ct->flags & STORE;
			const hard::Bank* bank = mem->get(inst->address());
			return ot::time(OCM_ACCESS_LATENCY * getInstNReg(inst));
		}


	};


	class BBTimerXilinxR5: public etime::EdgeTimeBuilder {
	public:
		static p::declare reg;
		BBTimerXilinxR5(void): etime::EdgeTimeBuilder(reg) { }

	protected:
		virtual void configure(const PropList& props) {
			etime::EdgeTimeBuilder::configure(props);
			write_log = WRITE_LOG(props);
			_props = props;
		}
		void setup(WorkSpace* ws) override {
			etime::EdgeTimeBuilder::setup(ws);
			const hard::CacheConfiguration* cache_config = hard::CACHE_CONFIGURATION_FEATURE.get(ws);
			if (!cache_config)
				throw ProcessorException(*this, "no cache");

			if (!cache_config->hasDataCache())
				throw ProcessorException(*this, "no data cache");

			if (!cache_config->hasInstCache())
				throw ProcessorException(*this, "no instruction cache");

			if (cache_config->isUnified())
				throw ProcessorException(*this, "unified L1 cache not supported");
			if (write_log) {
				sys::Path log_file_path = sys::Path(ws->process()->program()->name() + ".log");
				bool write_header = (log_file_path.exists()) ? false : true;
				log_stream = new FileOutput(log_file_path, true);
				if (write_header)
					*log_stream << "########################################################" << endl
								<< "# Static analysis on " << ws->process()->program()->name() << endl
								<< "# Overestimated instructions" << endl
								<< "# Address (hex); Instruction" << endl
								<< "########################################################" << endl;
				else
					*log_stream << endl; // sep
				
				unknown_inst_address = new elm::Vector<Address>();
			}
		}

		etime::EdgeTimeGraph* make(ParExeSequence* seq) override {
			R5FExeGraph* graph = new R5FExeGraph(workspace(), _microprocessor, ressources(), seq, _props, log_stream, unknown_inst_address);
			graph->build();
			return graph;
		}


		virtual void clean(ParExeGraph* graph) {
			log_stream->flush();
			delete graph;
		}
	private:
		PropList _props;
		FileOutput* log_stream = nullptr;
		bool write_log = 0;
		elm::Vector<Address>* unknown_inst_address = nullptr;
	};

	

	p::declare BBTimerXilinxR5::reg = p::init("otawa::xilinx::BBTimerXilinxR5", Version(1, 0, 0))
										.extend<etime::EdgeTimeBuilder>()
										.require(otawa::hard::CACHE_CONFIGURATION_FEATURE)
										.maker<BBTimerXilinxR5>();
	

} // namespace xilinx
} // namespace otawa
