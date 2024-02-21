/*
 *	xilinx_A9 module implementation
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
#include "A9CycleTiming.h"
#include "armCortexR5_A9_operand.h"
#include "arm_properties.h"

#define FUs_NUM_STAGE 2

namespace otawa { namespace xilinx {
    using namespace elm::io;

    typedef enum {
        FE    = 0,
        IQ    = 1,
        DE    = 2,
        REGR  = 3,
        EXE   = 4,
        WB    = 5,
        CNT   = 6
    } pipeline_stage_t;
    
    class A9ExeGraph: public etime::EdgeTimeGraph {
	public:
		
		A9ExeGraph(WorkSpace* ws,
                 ParExeProc* proc, 
                 Vector<Resource* >* hw_resources, 
				 ParExeSequence* seq,
                 const PropList& props,
                 FileOutput* out, 
                 elm::Vector<Address>* unknown_inst_address) : etime::EdgeTimeGraph(ws, proc, hw_resources, seq, props), 
                                                                exec_f(0), exec_alu_mul(0), exec_ldst(0), _out(out), 
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

        /*
        * This is to ensure that only one multiplication instruction can be executed at a same time.
        */
        void addEdgesForMulOrder() {
            string mul_dep("Mul order");
            ParExeInst* prev_inst = nullptr;
            for (InstIterator inst(this); inst(); inst++) {
                if (prev_inst && inst->inst()->isMul()) {
                    new ParExeEdge(prev_inst->firstFUNode(), inst->firstFUNode(), ParExeEdge::SOLID, 0, mul_dep);
                    new ParExeEdge(prev_inst->lastFUNode(), inst->lastFUNode(), ParExeEdge::SOLID, 0, mul_dep);
                    prev_inst = *inst;
                }
            }
        }

        void addEdgesForPipelineOrder() override {
            ParExeGraph::addEdgesForPipelineOrder();
			// Add latency penalty to Exec-FU nodes
			for (InstIterator inst(this); inst(); inst++) {
				ot::time cost = getInstExCost(inst->inst());
				if (cost > 1)
                    inst->firstFUNode()->setLatency(cost - 1);
                else
				    inst->lastFUNode()->setLatency(0);
			}
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
							delay = br_misprediction_penalty;
							if (delay >= 2) 
								new ParExeEdge(prev_inst->lastFUNode(), inst->fetchNode(), ParExeEdge::SOLID, delay - 2, "Branch prediction");
						// }
					}
				}
				prev_inst = *inst;
			}
        }

        
        void addEdgesForDataDependencies() override {
            ParExeStage* exec_stage = _microprocessor->execStage();
			
            // for each functional unit
			for (int j = 0; j < exec_stage->numFus(); j++) {
				ParExeStage* fu_stage = exec_stage->fu(j)->firstStage();
                
                // for each stage in the functional unit
				for (int k=0; k < fu_stage->numNodes(); k++) {
					ParExeNode* node = fu_stage->node(k);
					ParExeInst* inst = node->inst();

                    // for each instruction producing a used data
					for (ParExeInst::ProducingInstIterator producer(inst); producer(); producer ++) {                      
                        // Find the stage node producing the data
						ParExeNode* producing_node = nullptr;
						if (!producer->inst()->isLoad())
							producing_node = producer->lastFUNode();
						else
							producing_node = findMemStage(*producer);
                        if (producing_node) {
                            ot::time stall_duration = getInstResultLatency(producer->inst()) - getInstExCost(producer->inst());
                            new ParExeEdge(producing_node, inst->firstFUNode(), ParExeEdge::SOLID, stall_duration, "Data Dependency");
                        }

                    }
                }
            }
        }

        void build() override {
            // Look for FUs
			for (ParExePipeline::StageIterator pipeline_stage(_microprocessor->pipeline()); pipeline_stage(); pipeline_stage++) {
                
                if (pipeline_stage->name() == "Fetch") {
					stage[FE] = *pipeline_stage;
                } else if (pipeline_stage->name() == "IQ") {
                    stage[IQ] = *pipeline_stage;
                } else if (pipeline_stage->name() == "Decode") {
					stage[DE] = *pipeline_stage;
				} else if (pipeline_stage->name() == "RegR") {
                    stage[REGR] = *pipeline_stage;
                } else if (pipeline_stage->name() == "EXE") {
					stage[EXE] = *pipeline_stage;
					for (int i = 0; i < pipeline_stage->numFus(); i++) {
						ParExePipeline* fu = pipeline_stage->fu(i);
						if (fu->firstStage()->name().startsWith("EXEC_F")) {
							exec_f = fu;
						} else if (fu->firstStage()->name().startsWith("EXEC_ALU_M")) {
							exec_alu_mul = fu;
						} else if (fu->firstStage()->name().startsWith("EXEC_LDS")) {
							exec_ldst = fu;
						} else
							ASSERTP(false, fu->firstStage()->name());
						
					}
				} else if (pipeline_stage->name() == "WB") {
					stage[WB] = *pipeline_stage;
				}
            }
            ASSERTP(stage[FE], "No 'Fetch' stage found");
            ASSERTP(stage[IQ], "No 'IQ' stage found");
			ASSERTP(stage[DE], "No 'Decode' stage found");
            ASSERTP(stage[REGR], "No 'Register rename stage' stage found");
			ASSERTP(stage[EXE], "No 'EXE' stage found");
			ASSERTP(stage[WB], "No 'Write back' stage found");
			ASSERTP(exec_f, "No FPU fu found");
			ASSERTP(exec_f->numStages() == FUs_NUM_STAGE, "Wrong number of stages of FPU")
			ASSERTP(exec_alu_mul, "No ALU/MUL fu found");
			ASSERTP(exec_alu_mul->numStages() == FUs_NUM_STAGE, "Wrong number of stages of ALU/MUL")
			ASSERTP(exec_ldst, "No LD/ST fu found");
			ASSERTP(exec_ldst->numStages() == FUs_NUM_STAGE, "Wrong number of stages of LD/ST")

            // Build the execution graph 
            // ParExeGraph::build();
			createSequenceResources();
			createNodes();
			addEdgesForPipelineOrder();
			addEdgesForFetch();
			addEdgesForProgramOrder();
			addEdgesForMemoryOrder();
			addEdgesForDataDependencies();
            addEdgesForMulOrder();
            dumpUnknowInst();
        }

        private:
            otawa::arm::Info* info;
            const hard::Memory* mem;
            ParExeStage* stage[CNT];
            ParExePipeline *exec_f, *exec_alu_mul, *exec_ldst;
            FileOutput* _out = nullptr;
            elm::Vector<Address>* _unknown_inst_address = nullptr;
            const ot::time br_misprediction_penalty = 10; // This is an overestimation since the official documentation does not provide this inforamtion

            /*
                Attempts to decode an instruction and return the corresponding behavior "cycle timing behavior".
                "cycle timing behavior" of instructions are provided at the section B of 
                'Cortex™ -A9, Revision: r4p1, Technical Reference Manual'
                
                    inst: Instruction decode.
            */
            xilinx_a9_time_t* getInstCycleTiming(Inst* inst) {
                void* inst_info = info->decode(inst);
                xilinx_a9_time_t* inst_cycle_timing = xilinxA9Time(inst_info);
                info->free(inst_info);
                return inst_cycle_timing;
            }

            t::uint32 getInstNReg(Inst* inst) {
                void* inst_info = info->decode(inst);
                t::uint32 inst_n_reg = armV7_NReg(inst_info);
                info->free(inst_info);
                return inst_n_reg;
            }

            ot::time getInstExCost(Inst* inst) {
                if (inst->isMulti() && inst->isMem())
                    return 1 + (getInstNReg(inst) / 2); // We consider 'Aligned on a 64-bit boundary' = No
                else
                    return getInstCycleTiming(inst)->ex_cost;
            }

            ot::time getInstResultLatency(Inst* inst) {
                ot::time latency = getInstCycleTiming(inst)->result_latency; 
                // Th eresult latency in getInstCycleTiming(inst)->result_latency' is 
                // the latency of the first load/store register
                if (inst->isMulti() && inst->isMem())
                    return latency + getInstNReg(inst) - 1;
                return latency;
            }

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

    };

    class BBTimerXilinxA9: public etime::EdgeTimeBuilder {
	public:
		static p::declare reg;
		BBTimerXilinxA9(void): etime::EdgeTimeBuilder(reg) { }

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
			A9ExeGraph* graph = new A9ExeGraph(workspace(), _microprocessor, ressources(), seq, _props, log_stream, unknown_inst_address);
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

	

	p::declare BBTimerXilinxA9::reg = p::init("otawa::xilinx::BBTimerXilinxA9", Version(1, 0, 0))
										.extend<etime::EdgeTimeBuilder>()
										.require(otawa::hard::CACHE_CONFIGURATION_FEATURE)
										.maker<BBTimerXilinxA9>();
	

} // namespace xilinx
} // namespace otawa
